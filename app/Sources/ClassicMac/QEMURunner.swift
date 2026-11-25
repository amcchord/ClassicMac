import Foundation
import SwiftUI
import AppKit
import Darwin

enum CommandResult {
    case success(String)
    case failure(String)
}

// Thread-safe accumulator for subprocess output read on a background queue.
final class DataBox: @unchecked Sendable {
    private let lock = NSLock()
    private var data = Data()

    func append(_ chunk: Data) {
        lock.lock()
        data.append(chunk)
        lock.unlock()
    }

    func string() -> String {
        lock.lock()
        defer { lock.unlock() }
        return String(data: data, encoding: .utf8) ?? ""
    }
}

// QEMU recognizes the Finder menu bar directly in guest VRAM and atomically
// hands its startup-only instruction clock back to real time. This bounded
// host fallback guarantees an unusual theme or unsupported guest never stays
// instruction-timed after startup.
final class BootClockHandoff: @unchecked Sendable {
    private let socketPath: String
    private let queue = DispatchQueue.global(qos: .userInitiated)
    private let lock = NSLock()
    private var workItem: DispatchWorkItem?
    private var cancelled = false

    init(socketPath: String) {
        self.socketPath = socketPath
    }

    func start(delay: TimeInterval = 15.0) {
        lock.lock()
        defer { lock.unlock() }
        guard !cancelled else { return }
        workItem?.cancel()
        let path = socketPath
        let item = DispatchWorkItem { [weak self] in
            guard let self, !self.isCancelled else { return }
            _ = HMPClient.command("classicmac-boot-complete",
                                  socketPath: path)
        }
        workItem = item
        queue.asyncAfter(deadline: .now() + delay, execute: item)
    }

    func cancel() {
        lock.lock()
        cancelled = true
        let item = workItem
        workItem = nil
        lock.unlock()
        item?.cancel()
    }

    private var isCancelled: Bool {
        lock.lock()
        defer { lock.unlock() }
        return cancelled
    }

    func run() {
        start()
    }
}

// Launches and tracks emulator processes, and builds qemu-img and
// qemu-system-m68k / qemu-system-ppc command lines from a VMConfig.
@MainActor
final class QEMUManager: ObservableObject {
    static let shared = QEMUManager()

    @Published private(set) var runningIDs: Set<UUID> = []
    @Published private(set) var pausedIDs: Set<UUID> = []
    @Published private(set) var browserURLs: [UUID: URL] = [:]
    // The latest screen capture of each machine, refreshed while it runs and
    // kept after shutdown so the library shows what was last on screen.
    @Published private(set) var previews: [UUID: NSImage] = [:]
    @Published private(set) var coplandHaltedIDs: Set<UUID> = []
    /// The host renderer drawing each running ATI Mac's 3D ("" when 3D is off).
    @Published private(set) var atiRenderers: [UUID: String] = [:]
    @Published var lastError: AppError?
    @Published private(set) var pendingStopID: UUID?

    private var coplandControls: [UUID: Pipe] = [:]
    private var processes: [UUID: Process] = [:]
    private var browserServers: [UUID: BrowserDisplayServer] = [:]
    private var printServers: [UUID: PrintServer] = [:]
    private var qmpMonitors: [UUID: QMPEventMonitor] = [:]
    private var previewTimers: [UUID: Timer] = [:]
    private var bootClockHandoffs: [UUID: BootClockHandoff] = [:]
    private var forcedStopWorkItems: [UUID: DispatchWorkItem] = [:]
    private var intentionalForceStopIDs: Set<UUID> = []
    private var osxRestartRequestedIDs: Set<UUID> = []
    private var osxShutdownTasks: [UUID: Task<Void, Never>] = [:]
    private var runningFamilies: [UUID: MachineFamily] = [:]
    private var runningNames: [UUID: String] = [:]

    func isRunning(_ id: UUID) -> Bool {
        runningIDs.contains(id)
    }

    func isPaused(_ id: UUID) -> Bool {
        pausedIDs.contains(id)
    }

    private func isOSX(_ id: UUID) -> Bool {
        if let family = runningFamilies[id] {
            return family == .powerMacOSX
        }
        return VMStore.shared.vms.first(where: { $0.id == id })?.machineFamily == .powerMacOSX
    }

    static func monitorSocketURL(for id: UUID) -> URL {
        // Unix domain socket paths must fit in sockaddr_un (< 104 bytes on macOS),
        // so the long Application Support path cannot be used. Keep it short.
        AppPaths.runtimeSocketDir.appendingPathComponent("\(id.uuidString).sock")
    }

    static func qmpSocketURL(for id: UUID) -> URL {
        AppPaths.runtimeSocketDir.appendingPathComponent("\(id.uuidString).qmp.sock")
    }

    // SHUTDOWN event reasons after which the VM should boot right back up:
    // a restart chosen inside the guest, or the app's own Restart command
    // (system_reset over the monitor socket). VMs run with
    // -action reboot=shutdown, so both arrive as a clean exit + this reason.
    // Besides avoiding mac99's broken in-place reset, that boundary lets the
    // app inspect an installer-written disk before choosing the next boot.
    private static let relaunchReasons: Set<String> = ["guest-reset", "host-qmp-system-reset"]

    func start(_ config: VMConfig) {
        start(config, reusing: nil, openDisplay: true)
    }

    private func start(
        _ config: VMConfig,
        reusing browserServer: BrowserDisplayServer?,
        openDisplay: Bool
    ) {
        if runningIDs.contains(config.id) {
            return
        }
        guard AppPaths.qemuIsAvailable(for: config.machineFamily) else {
            lastError = AppError(
                "Couldn't Start \u{201C}\(config.name)\u{201D}",
                "This copy of ClassicMac is missing its emulation engine. Reinstall ClassicMac to fix this. (Developers: run scripts/build-qemu.sh, then scripts/bundle-qemu.sh.)"
            )
            return
        }
        if let preflightError = QEMUManager.preflight(config) {
            lastError = preflightError
            return
        }
        VMCheckpointStore.createPreRecoveryBackupIfNeeded(for: config)
        QEMUManager.applyQuadraBootPRAM(for: config)

        let displayServer: BrowserDisplayServer?
        if config.useBrowserDisplay {
            do {
                displayServer = try browserServer ?? BrowserDisplayServer.start(for: config)
            } catch {
                lastError = AppError(
                    "Couldn't Start \u{201C}\(config.name)\u{201D}",
                    "The local browser display could not be started. \(error.localizedDescription)"
                )
                return
            }
        } else {
            displayServer = nil
        }

        // Remove any stale sockets so QEMU can bind fresh ones.
        try? FileManager.default.removeItem(at: QEMUManager.monitorSocketURL(for: config.id))
        try? FileManager.default.removeItem(at: QEMUManager.qmpSocketURL(for: config.id))
        if let displayServer {
            try? FileManager.default.removeItem(at: displayServer.endpoint.vncSocketURL)
        }

        if QEMUManager.usesClassicMacDisplay(config) {
            do {
                _ = try GuestWindows.shared.prepare(config)
            } catch {
                lastError = AppError(
                    "Couldn't Start \u{201C}\(config.name)\u{201D}",
                    "The Mac's screen could not be prepared. \(error.localizedDescription)"
                )
                return
            }
        }

        let printServer: PrintServer?
        if config.printingEnabled && config.machineFamily.supportsPrinting {
            GuestPrinterProvisioner.ensurePrinterIfApplicable(for: config)
            let server = PrintServer(
                targetPrinter: config.printerName,
                pdfOutputDir: HostPrinterManager.defaultPDFOutputDirectory(for: config)
            )
            server.onError = { [weak self] error in
                Task { @MainActor [weak self] in
                    self?.lastError = AppError(
                        "Couldn't Print from \u{201C}\(config.name)\u{201D}",
                        error.localizedDescription
                    )
                }
            }
            do {
                try server.start()
                printServer = server
            } catch {
                if let displayServer {
                    if browserServers[config.id] === displayServer {
                        stopBrowserDisplay(for: config.id)
                    } else {
                        displayServer.stop()
                    }
                }
                GuestWindows.shared.machineStopped(config.id)
                lastError = AppError(
                    "Couldn't Start \u{201C}\(config.name)\u{201D}",
                    "The virtual printer could not be started. \(error.localizedDescription)"
                )
                return
            }
        } else {
            printServer = nil
        }

        let process = Process()
        process.executableURL = AppPaths.qemuBinary(for: config.machineFamily)
        process.arguments = QEMUManager.buildArguments(for: config, printerPorts: printServer?.ports)
        process.currentDirectoryURL = config.folder
        intentionalForceStopIDs.remove(config.id)

        // The Quadra's SCSI CD driver notices live media changes, so publish
        // the bundled image to its running-window menu. Power Macs receive
        // Tools through a read-only Virtio disk at startup instead: Mac OS 9's
        // IDE driver accepts a live QEMU media change but never tells Finder
        // to mount it, which made the old Insert command look successful while
        // doing nothing in the guest.
        var environment = ProcessInfo.processInfo.environment
        environment["CLASSICMAC_VM_ID"] = config.id.uuidString
        if config.machineFamily == .quadra800, let toolsCD = AppPaths.toolsCD {
            environment["CLASSICMAC_TOOLS_CD"] = toolsCD.path
        } else {
            environment.removeValue(forKey: "CLASSICMAC_TOOLS_CD")
        }
        if config.machineFamily == .powerMac7500 {
            let control = CoplandMachine.controlPipe()
            coplandControls[config.id] = control
            process.standardInput = control
            process.standardOutput = FileHandle.nullDevice
            environment["CLASSICMAC_CONTROL"] = "1"
            environment["CLASSICMAC_MACHINE_NAME"] = config.name
            environment["CLASSICMAC_PREVIEW_PATH"] = Self.screenDumpURL(for: config.id).path
            environment["CLASSICMAC_STATUS_PATH"] = CoplandMachine.statusURL(for: config.id).path
            environment["DPPC_RTC"] = CoplandClock.initialDate(disk: config.diskImageURL)
            environment["CLASSICMAC_RTC_PATH"] = config.folder.appendingPathComponent("copland-rtc.bin").path
            try? FileManager.default.removeItem(at: CoplandMachine.statusURL(for: config.id))
        }
        process.environment = environment

        let stderrPipe = Pipe()
        process.standardError = stderrPipe
        let capturedError = DataBox()
        stderrPipe.fileHandleForReading.readabilityHandler = { handle in
            let chunk = handle.availableData
            if !chunk.isEmpty {
                capturedError.append(chunk)
            }
        }

        process.terminationHandler = { [weak self] proc in
            stderrPipe.fileHandleForReading.readabilityHandler = nil
            let message = capturedError.string()
            Task { @MainActor in
                guard let self = self else { return }
                let savedConfig = VMStore.shared.vms.first { $0.id == config.id } ?? config
                var nextConfig = savedConfig
                let monitor = self.qmpMonitors.removeValue(forKey: config.id)
                self.runningIDs.remove(config.id)
                self.pausedIDs.remove(config.id)
                self.atiRenderers[config.id] = nil
                if self.pendingStopID == config.id {
                    self.pendingStopID = nil
                }
                self.processes.removeValue(forKey: config.id)
                if let pipe = self.coplandControls.removeValue(forKey: config.id) {
                    try? pipe.fileHandleForWriting.close()
                }
                self.coplandHaltedIDs.remove(config.id)
                try? FileManager.default.removeItem(at: CoplandMachine.statusURL(for: config.id))
                self.forcedStopWorkItems.removeValue(forKey: config.id)?.cancel()
                self.osxShutdownTasks.removeValue(forKey: config.id)?.cancel()
                self.runningFamilies.removeValue(forKey: config.id)
                self.runningNames.removeValue(forKey: config.id)
                let wasOSXRestartRequested = self.osxRestartRequestedIDs.remove(config.id) != nil
                self.stopBootClockHandoff(for: config.id)
                self.stopPreviewUpdates(for: config.id)
                self.printServers.removeValue(forKey: config.id)?.stop()
                let wasIntentionalForceStop = self.intentionalForceStopIDs.remove(config.id) != nil
                if displayServer != nil {
                    try? FileManager.default.removeItem(
                        at: BrowserDisplayEndpoint.vncSocketURL(for: config.id)
                    )
                }
                if let session = GuestWindows.shared.session(for: config.id),
                   let cgImage = session.takeFrame(force: true) {
                    let finalImage = NSImage(
                        cgImage: cgImage,
                        size: NSSize(width: cgImage.width, height: cgImage.height)
                    )
                    if self.previews[config.id] == nil || MachinePreviewQuality.isUseful(finalImage) {
                        self.previews[config.id] = finalImage
                    }
                }
                self.persistPreview(config)
                if !wasIntentionalForceStop,
                   config.machineFamily == .powerMac7500,
                   proc.terminationReason == .exit, proc.terminationStatus == 75 {
                    VMCheckpointStore.markStopped(in: config.folder, clean: true, reason: "copland-restart")
                    self.start(savedConfig, reusing: nil, openDisplay: true)
                    return
                }
                let failedExit = proc.terminationReason == .exit && proc.terminationStatus != 0
                let crashedBySignal = proc.terminationReason == .uncaughtSignal && !wasIntentionalForceStop
                if failedExit || crashedBySignal {
                    let failReason = crashedBySignal ? "signal-\(proc.terminationStatus)" : "exit-\(proc.terminationStatus)"
                    VMCheckpointStore.markStopped(in: config.folder, clean: false, reason: failReason)
                    monitor?.cancel()
                    self.stopBrowserDisplay(for: config.id)
                    GuestWindows.shared.machineStopped(config.id)
                    var logBody = message.trimmingCharacters(in: .whitespacesAndNewlines)
                    if crashedBySignal {
                        let sig = proc.terminationStatus
                        let sigName = String(cString: strsignal(sig))
                        let header = "Terminated by signal \(sig) (\(sigName))."
                        logBody = logBody.isEmpty ? header : "\(header)\n\(logBody)"
                    } else if logBody.isEmpty {
                        logBody = "Exited with status \(proc.terminationStatus)."
                    }
                    self.lastError = AppError(
                        "\u{201C}\(config.name)\u{201D} Shut Down Unexpectedly",
                        "The Mac stopped on its own. Try starting it again.",
                        logURL: AppLog.write(logBody, machineName: config.name)
                    )
                    return
                }
                if wasIntentionalForceStop {
                    VMCheckpointStore.markStopped(in: config.folder, clean: false, reason: "force-stop")
                    monitor?.cancel()
                    self.stopBrowserDisplay(for: config.id)
                    GuestWindows.shared.machineStopped(config.id)
                    return
                }
                // An installer blesses the destination System Folder before
                // asking the Mac to restart or shut down. Once a clean CD run
                // reaches that point, make the saved startup choice follow the
                // newly bootable hard disk while leaving the disc inserted.
                if proc.terminationReason == .exit && proc.terminationStatus == 0 {
                    nextConfig = QEMUManager.configurationForNextBoot(
                        afterSuccessfulRun: config,
                        savedConfiguration: savedConfig
                    )
                    if nextConfig != savedConfig {
                        _ = VMStore.shared.save(nextConfig)
                    }
                }
                // A restart (from inside the guest or via the app's Restart
                // command) exits QEMU cleanly with a reset reason; boot the
                // machine right back up so it behaves like a real reboot.
                let reason = await monitor?.shutdownReasonAfterExit(timeout: 2)
                monitor?.cancel()
                VMCheckpointStore.markStopped(
                    in: config.folder,
                    clean: true,
                    reason: reason ?? (wasOSXRestartRequested ? "osx-restart" : "clean-exit")
                )
                if wasOSXRestartRequested || (reason != nil && QEMUManager.relaunchReasons.contains(reason!)) {
                    // Settings can change while the shutdown event arrives.
                    // Always restart with the latest saved media and options.
                    nextConfig = VMStore.shared.vms.first { $0.id == config.id } ?? nextConfig
                    self.start(
                        nextConfig,
                        reusing: displayServer,
                        openDisplay: !nextConfig.useBrowserDisplay
                    )
                } else {
                    self.stopBrowserDisplay(for: config.id)
                    GuestWindows.shared.machineStopped(config.id)
                }
            }
        }

        do {
            try process.run()
        } catch {
            printServer?.stop()
            printServers.removeValue(forKey: config.id)
            GuestWindows.shared.machineStopped(config.id)
            coplandControls.removeValue(forKey: config.id)
            if let displayServer {
                if browserServers[config.id] === displayServer {
                    stopBrowserDisplay(for: config.id)
                } else {
                    displayServer.stop()
                }
            }
            lastError = AppError(
                "Couldn't Start \u{201C}\(config.name)\u{201D}",
                "The Mac could not be started. \(error.localizedDescription)"
            )
            return
        }

        processes[config.id] = process
        if let printServer {
            printServers[config.id] = printServer
        }
        runningFamilies[config.id] = config.machineFamily
        runningNames[config.id] = config.name
        VMCheckpointStore.markStarted(in: config.folder)
        if let displayServer {
            browserServers[config.id] = displayServer
            browserURLs[config.id] = displayServer.url
        }
        runningIDs.insert(config.id)

        // The QEMU families report shutdown/restart intent over QMP (see
        // relaunchReasons above); watch the event stream for this run.
        if config.machineFamily != .powerMac7500 {
            let monitor = QMPEventMonitor(
                socketPath: QEMUManager.qmpSocketURL(for: config.id).path
            )
            monitor.start()
            qmpMonitors[config.id] = monitor
        }

        let bootingFromUserCD = config.bootFromCD &&
            config.cdImagePath?.isEmpty == false
        // ATI starts run in real time from power-on (no instruction clock).
        let acceleratedHardDiskBoot = config.machineFamily == .powerMacG4 &&
            !bootingFromUserCD && !config.useVirtioDisk && Self.effectiveGraphicsCard(for: config) == .standard
        if Self.effectiveGraphicsCard(for: config) == .atiRadeon {
            probeATIRenderer(for: config.id)
        }
        if acceleratedHardDiskBoot {
            startBootClockHandoff(for: config)
        }
        // Avoid copying three full framebuffers at 3, 6, and 9 seconds while
        // the CPU is busy starting Mac OS. QEMU's direct VRAM detector does
        // not need a screendump; ordinary library previews begin afterward.
        startPreviewUpdates(
            for: config,
            firstCaptureAfter: acceleratedHardDiskBoot ? 12.0 : 3.0
        )

        if openDisplay {
            if config.useBrowserDisplay {
                activate(config.id)
            } else if QEMUManager.usesClassicMacDisplay(config) {
                // The window opens at once on the startup screen.
                GuestWindows.shared.show(config.id)
            } else {
                // The native QEMU Cocoa window appears shortly after the
                // process spawns, so bring it forward twice as it settles.
                activate(config.id, afterDelay: 0.7)
                activate(config.id, afterDelay: 2.0)
            }
        }
    }

    // Returns the saved configuration that should be used on the next start.
    // A selected installer disc remains authoritative until a successful run
    // leaves a genuinely blessed HFS/HFS+ system on the hard disk.
    static func configurationForNextBoot(
        afterSuccessfulRun config: VMConfig,
        savedConfiguration: VMConfig? = nil
    ) -> VMConfig {
        let saved = savedConfiguration ?? config
        guard config.bootFromCD,
              config.cdImagePath?.isEmpty == false,
              saved.bootFromCD == config.bootFromCD,
              saved.cdImagePath == config.cdImagePath,
              MacDiskImage.hasBlessedSystemFolder(at: config.diskImageURL) else {
            return saved
        }
        var next = saved
        next.bootFromCD = false
        return next
    }

    // MARK: Machine display

    // Opens the optional VNC viewer or brings the native Cocoa window forward.
    func activate(_ id: UUID) {
        if GuestWindows.shared.session(for: id) != nil {
            GuestWindows.shared.show(id)
            return
        }
        if let url = browserURLs[id] {
            if !NSWorkspace.shared.open(url) {
                lastError = AppError(
                    "Couldn't Open the Mac's Screen",
                    "Open \(url.absoluteString) in a web browser on this Mac."
                )
            }
            return
        }

        guard let process = processes[id],
              let app = NSRunningApplication(
                processIdentifier: process.processIdentifier
              ) else { return }
        app.activate(from: .current, options: [])
    }

    private func activate(_ id: UUID, afterDelay delay: TimeInterval) {
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            self?.activate(id)
        }
    }

    private func stopBrowserDisplay(for id: UUID) {
        browserServers.removeValue(forKey: id)?.stop()
        browserURLs.removeValue(forKey: id)
    }

    // MARK: Screen previews

    /// Asks the running ATI card which host renderer draws its 3D
    /// (QOM x-gl-renderer on /machine/peripheral/ati0), once the monitor is up.
    private func probeATIRenderer(for id: UUID) {
        let socket = Self.monitorSocketURL(for: id).path
        Task.detached(priority: .utility) { [weak self] in
            for _ in 0..<20 {
                try? await Task.sleep(nanoseconds: 500_000_000)
                guard let reply = HMPClient.command(
                    "qom-get /machine/peripheral/\(ATIDisplay.deviceID) x-gl-renderer", socketPath: socket) else {
                    continue
                }
                let renderer = ATIDisplay.rendererName(fromHMP: reply)
                await MainActor.run { [weak self] in
                    self?.atiRenderers[id] = renderer
                }
                return
            }
        }
    }

    static func screenDumpURL(for id: UUID) -> URL {
        AppPaths.runtimeSocketDir.appendingPathComponent("\(id.uuidString).screen.ppm")
    }

    private func startBootClockHandoff(for config: VMConfig) {
        stopBootClockHandoff(for: config.id)
        let handoff = BootClockHandoff(
            socketPath: QEMUManager.monitorSocketURL(for: config.id).path
        )
        bootClockHandoffs[config.id] = handoff
        handoff.start()
    }

    private func stopBootClockHandoff(for id: UUID) {
        bootClockHandoffs.removeValue(forKey: id)?.cancel()
    }

    private func startPreviewUpdates(
        for config: VMConfig,
        firstCaptureAfter delay: TimeInterval = 3.0
    ) {
        stopPreviewUpdates(for: config.id)
        let timer = Timer(timeInterval: 3.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                self?.capturePreview(config)
            }
        }
        timer.fireDate = Date().addingTimeInterval(delay)
        RunLoop.main.add(timer, forMode: .common)
        previewTimers[config.id] = timer
    }

    private func stopPreviewUpdates(for id: UUID) {
        previewTimers.removeValue(forKey: id)?.invalidate()
        try? FileManager.default.removeItem(at: QEMUManager.screenDumpURL(for: id))
    }

    // Asks the running machine to dump its screen, then decodes the result.
    // Fire-and-forget: a failed or missed capture just keeps the previous one.
    private func capturePreview(_ config: VMConfig) {
        guard runningIDs.contains(config.id) else { return }
        if config.machineFamily == .powerMac7500 {
            if let image = PPMImage.load(Self.screenDumpURL(for: config.id)) {
                previews[config.id] = image
            }
            let status = try? String(contentsOf: CoplandMachine.statusURL(for: config.id), encoding: .utf8)
            if status == "halted\n" { coplandHaltedIDs.insert(config.id) }
            else { coplandHaltedIDs.remove(config.id) }
            return
        }
        let needsLivePreview = WindowManager.shared.mainWindow?.isVisible == true || previews[config.id] == nil
        if let session = GuestWindows.shared.session(for: config.id) {
            guard needsLivePreview else { return }
            if let cgImage = session.takeFrame(force: true) {
                previews[config.id] = NSImage(
                    cgImage: cgImage,
                    size: NSSize(width: cgImage.width, height: cgImage.height)
                )
            }
            return
        }
        guard needsLivePreview else { return }
        let socketPath = QEMUManager.monitorSocketURL(for: config.id).path
        let dumpURL = QEMUManager.screenDumpURL(for: config.id)
        DispatchQueue.global(qos: .utility).async { [weak self] in
            guard HMPClient.send("screendump \(dumpURL.path)", socketPath: socketPath) else { return }
            // The dump is written asynchronously after the command; give the
            // emulator a moment before reading it back.
            usleep(400_000)
            guard let image = PPMImage.load(dumpURL) else { return }
            Task { @MainActor [weak self] in
                self?.previews[config.id] = image
            }
        }
    }

    // Keeps the machine's last screen inside its .classic package so the
    // library can show it across launches.
    private func persistPreview(_ config: VMConfig) {
        guard let image = previews[config.id], let bundle = config.bundleURL else { return }
        let destination = bundle.appendingPathComponent("preview.png")
        let cgImage = image.cgImage(forProposedRect: nil, context: nil, hints: nil)
        let tiffData = cgImage == nil ? image.tiffRepresentation : nil
        DispatchQueue.global(qos: .userInitiated).async {
            let rep: NSBitmapImageRep
            if let cgImage {
                rep = NSBitmapImageRep(cgImage: cgImage)
            } else if let tiffData, let tiffRep = NSBitmapImageRep(data: tiffData) {
                rep = tiffRep
            } else {
                return
            }
            guard let png = rep.representation(using: .png, properties: [:]) else { return }
            try? png.write(to: destination, options: .atomic)
        }
    }

    func requestStop(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        pendingStopID = id
    }

    func cancelStop() {
        pendingStopID = nil
    }

    @MainActor
    func forceStop(_ id: UUID) {
        intentionalForceStopIDs.insert(id)
        osxRestartRequestedIDs.remove(id)
        osxShutdownTasks.removeValue(forKey: id)?.cancel()
        forcedStopWorkItems.removeValue(forKey: id)?.cancel()
        guard let process = processes[id] else { return }
        let pid = process.processIdentifier
        if process.isRunning {
            process.terminate()
        }
        guard pid > 0 else { return }
        let killWork = DispatchWorkItem { [weak self, weak process] in
            if process?.isRunning == true {
                kill(pid, SIGKILL)
            }
            Task { @MainActor [weak self] in
                self?.forcedStopWorkItems.removeValue(forKey: id)
            }
        }
        forcedStopWorkItems[id] = killWork
        DispatchQueue.main.asyncAfter(deadline: .now() + 3, execute: killWork)
    }

    @MainActor
    func stopAndWait(_ id: UUID, force: Bool, then completion: @escaping @MainActor () -> Void) {
        guard runningIDs.contains(id) else {
            completion()
            return
        }
        pendingStopID = nil
        if force {
            forceStop(id)
        } else {
            stop(id)
        }
        Task { @MainActor [weak self] in
            let escalateAt = Date().addingTimeInterval(16)
            let deadline = Date().addingTimeInterval(22)
            var escalated = force
            while let self, self.runningIDs.contains(id) {
                let now = Date()
                if !escalated && now >= escalateAt {
                    escalated = true
                    self.forceStop(id)
                }
                if now >= deadline {
                    break
                }
                try? await Task.sleep(nanoseconds: 100_000_000)
            }
            completion()
        }
    }

    /// Shuts every running Mac down safely (for Quit), then calls back once
    /// they have all stopped, or after 25 seconds.
    func shutDownAll(then completion: @escaping () -> Void) {
        pendingStopID = nil
        for id in runningIDs { stop(id) }
        let forceStopAt = Date().addingTimeInterval(15)
        let deadline = Date().addingTimeInterval(25)
        var escalated = false
        func poll() {
            if runningIDs.isEmpty {
                completion()
                return
            }
            let now = Date()
            if !escalated && now >= forceStopAt {
                escalated = true
                for id in runningIDs {
                    forceStop(id)
                }
            }
            if now > deadline {
                for id in runningIDs {
                    intentionalForceStopIDs.insert(id)
                    forcedStopWorkItems.removeValue(forKey: id)?.cancel()
                    if let process = processes[id], process.isRunning, process.processIdentifier > 0 {
                        kill(process.processIdentifier, SIGKILL)
                    }
                }
                completion()
                return
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { poll() }
        }
        poll()
    }

    func confirmStop() {
        guard let id = pendingStopID else { return }
        pendingStopID = nil
        if isOSX(id) {
            stopOSX(id, action: .shutDown, interactive: true)
        } else {
            stop(id)
        }
    }

    func confirmForceStop() {
        guard let id = pendingStopID else { return }
        pendingStopID = nil
        forceStop(id)
    }

    private func stop(_ id: UUID) {
        guard let process = processes[id] else { return }
        if isOSX(id) {
            stopOSX(id, action: .shutDown, interactive: false)
            return
        }
        if coplandControls[id] != nil {
            // Copland does not implement the ADB power-key dialog. Its own
            // Spaz > Shut Down command is the graceful path; the host command
            // is an explicitly confirmed power-off for a frozen preview.
            pausedIDs.remove(id)
            sendCopland("quit", to: id)
            let fallback = DispatchWorkItem { [weak self] in
                self?.forceStop(id)
            }
            forcedStopWorkItems[id]?.cancel()
            forcedStopWorkItems[id] = fallback
            DispatchQueue.main.asyncAfter(deadline: .now() + 15, execute: fallback)
            return
        }
        let socketPath = QEMUManager.monitorSocketURL(for: id).path
        let wasPaused = pausedIDs.remove(id) != nil

        // An immediate SIGTERM leaves a mounted HFS/HFS+ volume marked dirty.
        // Mac OS checks that volume on its next startup, adding several seconds
        // and risking data loss. The ADB power key opens the guest's native
        // one-button Shut Down dialog; Return confirms it and lets Mac OS flush
        // the disk before CUDA/PMU asks QEMU to exit.
        DispatchQueue.global(qos: .userInitiated).async { [weak self, weak process] in
            if wasPaused {
                _ = HMPClient.send("cont", socketPath: socketPath)
                usleep(100_000)
            }
            guard HMPClient.send("sendkey power", socketPath: socketPath) else {
                Task { @MainActor [weak self] in
                    self?.forceStop(id)
                }
                return
            }
            usleep(750_000)
            guard process?.isRunning == true,
                  HMPClient.send("sendkey ret", socketPath: socketPath) else {
                Task { @MainActor [weak self] in
                    if process?.isRunning == true {
                        self?.forceStop(id)
                    } else {
                        self?.forcedStopWorkItems.removeValue(forKey: id)?.cancel()
                    }
                }
                return
            }
        }

        // A crashed guest or an application blocking shutdown must not leave
        // the emulator stuck forever. This preserves the old forced-off
        // behavior as a bounded fallback, after giving Mac OS ample time to
        // finish an ordinary shutdown.
        let fallback = DispatchWorkItem { [weak self] in
            self?.forceStop(id)
        }
        forcedStopWorkItems[id]?.cancel()
        forcedStopWorkItems[id] = fallback
        DispatchQueue.main.asyncAfter(deadline: .now() + 15, execute: fallback)
    }

    private func stopOSX(_ id: UUID, action: OSXShutdownController.Action, interactive: Bool) {
        guard processes[id] != nil else { return }
        let wasPaused = pausedIDs.remove(id) != nil
        let socketPath = QEMUManager.monitorSocketURL(for: id).path
        let machineName = runningNames[id]
            ?? VMStore.shared.vms.first(where: { $0.id == id })?.name
            ?? "Mac OS X"
        let transport = OSXShutdownController.Transport.qemu(
            vmID: id,
            socketPath: socketPath,
            sessionProvider: { await MainActor.run { GuestWindows.shared.session(for: id) } }
        )
        osxShutdownTasks.removeValue(forKey: id)?.cancel()
        osxShutdownTasks[id] = Task { @MainActor [weak self] in
            let outcome = await OSXShutdownController.perform(
                action: action,
                wasPaused: wasPaused,
                transport: transport
            )
            guard let self else { return }
            self.osxShutdownTasks.removeValue(forKey: id)
            guard self.runningIDs.contains(id) else { return }
            switch outcome {
            case .confirmed:
                break
            case .unverified(let detail):
                if action == .restart {
                    self.osxRestartRequestedIDs.remove(id)
                }
                if interactive {
                    let verb = action == .restart ? "Restart" : "Shut Down"
                    self.lastError = AppError(
                        "Couldn't \(verb) \u{201C}\(machineName)\u{201D} Automatically",
                        "\(detail) Show the Mac window to \(verb.lowercased()) from the Apple menu, or choose Shut Down \u{2192} Force Stop."
                    )
                }
            case .monitorFailed:
                if action == .restart {
                    self.osxRestartRequestedIDs.remove(id)
                }
                if interactive {
                    self.lastError = AppError(
                        "Couldn't Control \u{201C}\(machineName)\u{201D}",
                        "The running Mac did not respond. If it stays unresponsive, choose Shut Down \u{2192} Force Stop."
                    )
                }
            }
        }
    }

    func pause(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        pausedIDs.insert(id)
        if coplandControls[id] != nil { sendCopland("pause", to: id); return }
        sendMonitor("stop", to: id, actionLabel: "Pause") { [weak self] in
            // Undo the optimistic state change so the UI matches reality.
            self?.pausedIDs.remove(id)
        }
    }

    func resume(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        pausedIDs.remove(id)
        if coplandControls[id] != nil { sendCopland("resume", to: id); return }
        sendMonitor("cont", to: id, actionLabel: "Resume") { [weak self] in
            self?.pausedIDs.insert(id)
        }
    }

    func reboot(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        if isOSX(id) {
            osxRestartRequestedIDs.insert(id)
            stopOSX(id, action: .restart, interactive: true)
            return
        }
        pausedIDs.remove(id)
        if coplandControls[id] != nil { sendCopland("restart", to: id); return }
        sendMonitor("system_reset", to: id, actionLabel: "Restart", onFailure: nil)
    }

    func forceQuitFrontmostApp(_ id: UUID) {
        guard runningIDs.contains(id), !pausedIDs.contains(id) else { return }
        sendMonitor(
            "sendkey meta_l-alt-esc 500",
            to: id,
            actionLabel: "Force Quit the Frontmost App",
            onFailure: nil
        )
    }

    func continueCopland(_ id: UUID) {
        guard coplandControls[id] != nil, !pausedIDs.contains(id) else { return }
        sendCopland("continue", to: id)
    }

    private func sendCopland(_ command: String, to id: UUID) {
        guard let pipe = coplandControls[id], processes[id]?.isRunning == true else { return }
        do { try pipe.fileHandleForWriting.write(contentsOf: Data((command + "\n").utf8)) }
        catch { lastError = AppError("Couldn't Control Copland", error.localizedDescription) }
    }

    // Sends a control command to the running machine. Failures (a dead or
    // unresponsive control socket) surface as an alert instead of silently
    // doing nothing.
    private func sendMonitor(_ command: String, to id: UUID, actionLabel: String, onFailure: (() -> Void)?) {
        let path = QEMUManager.monitorSocketURL(for: id).path
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let sent = HMPClient.send(command, socketPath: path)
            if !sent {
                Task { @MainActor [weak self] in
                    guard let self = self else { return }
                    onFailure?()
                    self.lastError = AppError(
                        "Couldn't \(actionLabel) the Machine",
                        "The running Mac did not respond. If it stays unresponsive, use Shut Down and start it again."
                    )
                }
            }
        }
    }

    // MARK: Launch preflight

    // Checks everything QEMU will need before spawning it, so failures surface
    // as clear messages instead of a cryptic emulator exit. Returns nil when
    // the machine is ready to boot. Also repairs what it safely can (a deleted
    // Quadra PRAM is recreated from the seed).
    private static func preflight(_ config: VMConfig) -> AppError? {
        let fm = FileManager.default
        let title = "Couldn't Start \u{201C}\(config.name)\u{201D}"

        if config.machineFamily == .powerMac7500 {
            guard config.diskImageName == "disk.img" else {
                return AppError(title, "Copland requires disk.img inside its machine package.")
            }
            do { try CoplandMachine.validateFirmware(in: config.folder) }
            catch { return AppError(title, error.localizedDescription) }
        }
        let missing = AppPaths.missingFirmware(for: config.machineFamily)
        if !missing.isEmpty {
            return AppError(
                title,
                "Part of ClassicMac's emulation engine is missing or damaged. Reinstall ClassicMac to fix this.",
                logURL: AppLog.write("Missing firmware for \(config.machineFamily.label): \(missing.joined(separator: ", "))",
                                     machineName: config.name)
            )
        }

        guard fm.fileExists(atPath: config.diskImageURL.path) else {
            return AppError(
                title,
                "The machine's hard disk is missing from \u{201C}\(config.folder.lastPathComponent)\u{201D}. If you moved or edited the machine file, restore its disk image."
            )
        }

        if config.machineFamily == .powerMacOSX {
            let flash = config.folder.appendingPathComponent("nvram.img")
            if !fm.fileExists(atPath: flash.path) {
                do {
                    try fm.copyItem(at: AppPaths.osxFirmwareDir.appendingPathComponent("macosx-flash-seed.img"), to: flash)
                } catch {
                    return AppError(title, "The Mac OS X startup memory could not be created.")
                }
            }
            let values = try? flash.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey])
            guard values?.isRegularFile == true, values?.isSymbolicLink == false, values?.fileSize == 16_384 else {
                return AppError(title, "The Mac OS X startup memory must be a 16 KB file named nvram.img.")
            }
            do {
                let nvramData = try OSXNVRAM.validate(at: flash)
                _ = try OSXNVRAM.rtcOffset(data: nvramData)
            } catch {
                return AppError(title, error.localizedDescription)
            }
        }

        if config.machineFamily.usesPRAMImage && !fm.fileExists(atPath: config.pramImageURL.path) {
            // A PRAM is tiny and recreatable; restore it rather than failing.
            if fm.fileExists(atPath: AppPaths.pramSeed.path) {
                try? fm.copyItem(at: AppPaths.pramSeed, to: config.pramImageURL)
            }
            if !fm.fileExists(atPath: config.pramImageURL.path) {
                return AppError(
                    title,
                    "Part of this machine's memory settings could not be restored. Try creating a new machine."
                )
            }
        }

        if let cdPath = config.cdImagePath, !cdPath.isEmpty, !fm.fileExists(atPath: cdPath) {
            return AppError(
                title,
                "The disc \u{201C}\(URL(fileURLWithPath: cdPath).lastPathComponent)\u{201D} could not be found. It may have been moved or deleted. Eject it in the machine's settings or choose it again."
            )
        }

        if let floppyPath = config.floppyImagePath,
           !floppyPath.isEmpty,
           !fm.fileExists(atPath: floppyPath) {
            return AppError(
                title,
                "The floppy disk \u{201C}\(URL(fileURLWithPath: floppyPath).lastPathComponent)\u{201D} could not be found. It may have been moved or deleted. Eject it in the machine's settings or choose it again."
            )
        }

        return nil
    }

    // Quadra 800 ROM selects the boot device from PRAM DefStartRec (0x78..0x7B)
    // rather than QEMU's -boot flag: SCSI ID 3 for CD, NuBus slot 0x0C sResource
    // 0xA0 for Virtio floppy, or SCSI ID 0 for the hard disk.
    static func applyQuadraBootPRAM(for config: VMConfig) {
        guard config.machineFamily == .quadra800,
              let handle = try? FileHandle(forUpdating: config.pramImageURL) else {
            return
        }
        defer { try? handle.close() }
        guard let size = try? handle.seekToEnd(), size >= 0x7C else {
            return
        }
        let bootBytes: [UInt8]
        if config.bootFromCD && config.cdImagePath?.isEmpty == false {
            bootBytes = [0xff, 0xff, 0xff, 0xdc]
        } else if config.bootFromFloppy && config.floppyImagePath?.isEmpty == false {
            bootBytes = [0x00, 0x00, 0x0c, 0xa0]
        } else {
            bootBytes = [0xff, 0xff, 0xff, 0xdf]
        }
        try? handle.seek(toOffset: 0x78)
        try? handle.write(contentsOf: Data(bootBytes))
    }

    // MARK: Argument construction

    nonisolated static func qemuEscape(_ s: String) -> String {
        s.replacingOccurrences(of: ",", with: ",,")
    }

    struct PrinterForwardPorts: Equatable, Sendable {
        let lpd: UInt16
        let ipp: UInt16
        let raw: UInt16
    }

    private static func nicArgument(
        model: String,
        networking: Bool,
        printerPorts: PrinterForwardPorts?
    ) -> String {
        guard let p = printerPorts else {
            return networking ? "user,model=\(model)" : "none"
        }
        let restrict = networking ? "" : ",restrict=on"
        return "user\(restrict),model=\(model)"
            + ",guestfwd=tcp:10.0.2.4:515-cmd:/usr/bin/nc 127.0.0.1 \(p.lpd)"
            + ",guestfwd=tcp:10.0.2.4:631-cmd:/usr/bin/nc 127.0.0.1 \(p.ipp)"
            + ",guestfwd=tcp:10.0.2.4:9100-cmd:/usr/bin/nc 127.0.0.1 \(p.raw)"
    }

    static func buildArguments(for config: VMConfig, printerPorts: PrinterForwardPorts? = nil) -> [String] {
        let display = BrowserDisplayEndpoint(vmID: config.id)
        switch config.machineFamily {
        case .quadra800:
            return buildQuadraArguments(for: config, display: display, printerPorts: printerPorts)
        case .powerMacG4:
            return buildPowerMacArguments(for: config, display: display, printerPorts: printerPorts)
        case .powerMacOSX:
            return buildOSXArguments(for: config, display: display, printerPorts: printerPorts)
        case .powerMac7500:
            return CoplandMachine.arguments(for: config)
        }
    }

    private static func buildOSXArguments(
        for config: VMConfig,
        display: BrowserDisplayEndpoint,
        printerPorts: PrinterForwardPorts? = nil
    ) -> [String] {
        let firmware = AppPaths.osxFirmwareDir
        let name = qemuEscape(config.name)
        let disk = qemuEscape(config.diskImageURL.path)
        let flashURL = config.folder.appendingPathComponent("nvram.img")
        let flash = qemuEscape(flashURL.path)
        let rtcBase = OSXNVRAM.rtcBase(at: flashURL)
        let width = config.width / 8 * 8
        let height = config.height
        let bootCD = config.bootFromCD && config.cdImagePath?.isEmpty == false
        var cd = "if=ide,index=2,media=cdrom,id=cd0,readonly=on"
        if let path = config.cdImagePath, !path.isEmpty {
            cd += ",file=\(qemuEscape(path)),format=raw"
        }
        var args = [
            "-L", firmware.path, "-bios", firmware.appendingPathComponent("openbios-ppc-osx").path,
            "-name", name, "-accel", "tcg,tb-size=512", "-cpu", "7400",
            "-m", String(config.ramMB),
            "-M", "mac99,via=pmu,usb=off,audiodev=snd0",
            "-audiodev", config.sound ? "coreaudio,id=snd0,out.buffer-length=50000" : "none,id=snd0",
            "-rtc", rtcBase,
            "-global", "macio-nvram.osx-flash=on",
            "-drive", "if=none,id=osxnv,file=\(flash),format=raw",
            "-global", "macio-nvram.drive=osxnv",
            "-drive", "file=\(disk),format=raw,media=disk,index=0",
            "-drive", cd, "-boot", bootCD ? "d" : "c",
            "-vga", "std", "-global", "VGA.romfile=",
            "-global", "VGA.vgamem_mb=64", "-global", "VGA.xres=\(width)",
            "-global", "VGA.yres=\(height)", "-g", "\(width)x\(height)x32",
            "-prom-env", "output-device=ttya",
            "-device", "pci-ohci,id=osx-usb", "-device", "usb-kbd,bus=osx-usb.0",
            "-device", config.tabletInput ? "usb-tablet,bus=osx-usb.0" : "usb-mouse,bus=osx-usb.0",
            "-nic", nicArgument(model: "sungem", networking: config.networking, printerPorts: printerPorts),
            "-serial", "none", "-action", "reboot=shutdown",
            "-monitor", "unix:\(qemuEscape(monitorSocketURL(for: config.id).path)),server=on,wait=off",
            "-qmp", "unix:\(qemuEscape(qmpSocketURL(for: config.id).path)),server=on,wait=off",
        ]
        if !bootCD {
            args += ["-global", "screamer.osx-codec=on"]
            args += ["-global", "screamer.dma-lead-us=0"]
            let osVersion = VMTemplateMetadata.load(from: config.folder)?.osVersion
            if !(osVersion?.hasPrefix("10.2") ?? false) {
                args += ["-global", "via-pmu.osx-sleep=on"]
            }
        }
        return args + displayArguments(for: config, browser: display)
    }

    private static func displayArguments(
        for config: VMConfig,
        browser display: BrowserDisplayEndpoint
    ) -> [String] {
        if config.useBrowserDisplay {
            return ["-display", display.qemuDisplay]
        }
        if usesClassicMacDisplay(config) {
            // ClassicMac draws the Mac's screen in its own Platinum window;
            // QEMU hands over the framebuffer and takes input on this socket.
            return ["-display", "classicmac,socket=\(qemuEscape(GuestDisplaySession.socketPath(for: config.id)))"]
        }
        if config.machineFamily == .powerMacOSX {
            return ["-display", "cocoa,swap-opt-cmd=off"]
        }
        return [
            "-display",
            "cocoa,swap-opt-cmd=off\(inputHelperOptions(for: config))"
        ]
    }

    /// Whether this Mac's screen appears in ClassicMac's own window.
    static func usesClassicMacDisplay(_ config: VMConfig) -> Bool {
        guard !config.useBrowserDisplay,
              config.machineFamily != .powerMac7500,
              !UserDefaults.standard.bool(forKey: "ClassicMacUseQEMUWindow") else {
            return false
        }
        if config.machineFamily == .powerMacOSX {
            return AppPaths.osxSupportsClassicMacDisplay
        }
        return true
    }

    // Extra Cocoa options for the classic input helpers (right-click as
    // Control+click, scroll wheel as arrow keys). Browser viewing implements
    // the same behavior in its local noVNC client.
    private static func inputHelperOptions(for config: VMConfig) -> String {
        config.classicInputHelpers
            ? ",right-click-ctrl=on,scroll-keys=on"
            : ""
    }

    // The dedicated Tools CD drive (id=tools0). It starts loaded when the
    // machine's settings ask for it; otherwise the Mac menu can fill its
    // empty tray while the machine is running.
    private static func toolsDriveSpec(
        for config: VMConfig,
        iface: String,
        index: Int? = nil,
        loadMedia: Bool = true
    ) -> String {
        var options = ["if=\(iface)"]
        if let index {
            options.append("index=\(index)")
        }
        options += ["media=cdrom", "id=tools0"]
        if loadMedia, config.toolsCDInserted, let toolsCD = AppPaths.toolsCD {
            options += ["file=\(qemuEscape(toolsCD.path))", "format=raw"]
        }
        options.append("readonly=on")
        return options.joined(separator: ",")
    }

    /// The card a start actually uses. Installer and CD starts stay on the
    /// standard card, as RetroBoot does: the CD route is qualified there.
    static func effectiveGraphicsCard(for config: VMConfig) -> GraphicsCard {
        let bootingFromUserCD = config.bootFromCD && config.cdImagePath?.isEmpty == false
        return config.machineFamily == .powerMacG4 && !bootingFromUserCD ? config.graphicsCard : .standard
    }

    // The Power Mac's display card and its boot framebuffer.
    static func graphicsCardArguments(for config: VMConfig, bootingFromUserCD: Bool) -> [String] {
        var args: [String] = []
        switch effectiveGraphicsCard(for: config) {
        case .standard:
            // Keep the host-resize registers available for the native window
            // and compatibility with the bundled qemu_vga.ndrv. Browser viewing
            // normally scales the configured framebuffer without changing it.
            // "-vga std" must be explicit: QEMU treats a -global for the VGA
            // driver as a user-configured display and would otherwise skip
            // creating the default one. 64 MB of VRAM covers 3840x2160 at
            // 32-bit (the default 16 MB tops out below 4K).
            args += ["-vga", "std"]
            args += ["-global", "VGA.host-resize=on"]
            args += ["-global", "VGA.vgamem_mb=64"]
            // Packed 1/2/4-bpp modes (ppcvid/vga-packed-depths.patch) let the
            // bundled qemu_vga.ndrv offer Black & White, 4 and 16 colors in
            // the Monitors control panel alongside 256/thousands/millions.
            args += ["-global", "VGA.packed-lowbpp=on"]
            // The optimized display path does not need QEMU to write-protect
            // and dirty-track each framebuffer page. Disabling those repeated
            // TCG traps is particularly important for QuickDraw's
            // framebuffer-heavy BitBlt and shape operations. Shadowed
            // low-color modes keep normal tracking.
            args += ["-global", "VGA.untracked-vram=on"]
            // Move the classic Mac cursor through QEXT and let Cocoa composite
            // it, avoiding framebuffer redraws for pointer motion. Older guest
            // drivers ignore the advertised feature and keep their software
            // cursor.
            args += ["-global", "VGA.hardware-cursor=on"]
            if !bootingFromUserCD && !config.useVirtioDisk {
                // Let QEMU recognize Finder directly in guest VRAM and perform
                // the clock handoff without full-frame screenshots or a VNC
                // client. BootClockHandoff above remains a 15-second fallback.
                args += ["-global", "VGA.classicmac-boot-handoff=on"]
            }
            // OpenBIOS sizes the framebuffer from -g at boot. QEMU's mac99
            // firmware interface names the classic direct-color modes 15 and
            // 32 bpp; the UI presents those as Thousands and Millions.
            let depth = config.depth == ColorDepth.millions.rawValue ? 32 : 15
            args += ["-g", "\(config.width)x\(config.height)x\(depth)"]
        case .atiRadeon:
            // The emulated R100 Radeon. No card ROM ("romfile=" is required:
            // QEMU's x86 vgabios-ati.bin is not bundled): the bundled OpenBIOS
            // builds its device node and boot mode. The emulated monitor's
            // EDID offers exactly the chosen size, which ClassicMac Display
            // (Tools) or RetroBoot Display (the Library's ATI-ready Macs)
            // selects during startup. "ati0" names the QOM status path.
            let size = ATIDisplay.size(fitting: config.width, height: config.height)
            var device = "ati-vga,id=\(ATIDisplay.deviceID),model=r100,vgamem_mb=32,romfile=,guest_hwcursor=on"
            if let hz = ATIDisplay.refresh(width: size.width, height: size.height) {
                device += ",xres=\(size.width),yres=\(size.height),refresh_rate=\(hz * 1000)"
            }
            args += ["-vga", "none", "-device", device]
            args += ["-prom-env", "classicmac-display=\(size.width)x\(size.height)"]
            args += ["-prom-env", "retroboot-display=\(size.width)x\(size.height)"]
            // ATI's driver starts every session at 640 x 480.
            args += ["-g", "\(ATIDisplay.startupSize.width)x\(ATIDisplay.startupSize.height)x32"]
        }
        return args
    }

    // qemu-system-ppc -M mac99: a New World Power Mac that boots Mac OS 8.5
    // through 9.2.2 (and early OS X) via the bundled OpenBIOS firmware, so no
    // Apple ROM is involved. Storage is IDE, networking is the sungem NIC.
    // Sound comes from the screamer (AWACS) device ported onto our QEMU build;
    // the guest driver attaches because the bundled OpenBIOS advertises the
    // davbus/awacs nodes.
    private static func buildPowerMacArguments(
        for config: VMConfig,
        display: BrowserDisplayEndpoint,
        printerPorts: PrinterForwardPorts? = nil
    ) -> [String] {
        var args: [String] = []

        // Folder sharing needs the classicvirtio ndrvloader to run before the
        // OS: it installs the virtio NDRVs and then continues the normal boot.
        // Sharing stays inactive while booting an installer CD so the install
        // environment never sees the host folder. Tablet input uses the same
        // loader, but remains enabled. G3 compatibility starts also use the
        // loader so the Mac OS 8.5/8.6 ROM can reach a read-only Virtio mirror
        // before its IDE driver is active. Default G4/Mac OS 9 starts use the
        // ordinary IDE CD directly so Installer keeps its native source path.
        let bootingFromUserCD = config.bootFromCD && config.cdImagePath?.isEmpty == false
        let bootingG3CompatibilityCD = bootingFromUserCD && (!config.useG4CPU || config.useVirtioDisk)
        let deferToolsMedia = bootingFromUserCD
        let sharing = config.hasSharedFolder && !bootingFromUserCD
        let tablet = config.tabletInput
        let legacy = config.useVirtioDisk
        // On Power Macs the Tools HFS image is a read-only Virtio block disk.
        // It uses the same startup NDRV loader as folder sharing and mounts as
        // a normal desktop volume without relying on IDE hot-plug detection.
        let toolsViaVirtio = !deferToolsMedia && config.toolsCDInserted &&
            AppPaths.toolsCD != nil
        let needsNdrvLoader = sharing || tablet || bootingG3CompatibilityCD ||
            toolsViaVirtio || config.useVirtioDisk || effectiveGraphicsCard(for: config) == .atiRadeon

        // InputSprocket needs a real HID mouse; the seamless Virtio tablet only
        // updates the classic cursor. The engine selects HID for 3D games.
        if legacy {
            args += ["-nodefaults", "-global", "cuda.idle-irq-ms=50"]
        } else {
            args += [
                "-device",
                config.classicInputHelpers
                    ? "usb-mouse,id=classicmac-game-mouse"
                    : "usb-mouse,id=classicmac-game-mouse,wheel-per-report=1"
            ]
        }

        // Input configuration. QEMU treats whichever pointing device the guest
        // touched last as "the mouse", and the cocoa display only releases the
        // cursor while that device is absolute.
        //
        // Mac OS 8.5 and 8.6 predate the KeyLargo PMU used by QEMU's default
        // mac99 profile, so all guests use the CUDA/ADB profile. Mac OS 9 can
        // opt into the faster 7400/G4 model; retail Mac OS 8.5 and 8.6 (ROM
        // 1.4/1.6) retain the original G3 identity advertised by OpenBIOS. The ADB
        // mouse is also a working captured fallback until the optional Virtio
        // tablet driver loads.
        // Large PowerPC games generate far more translated code than QEMU's
        // small default TCG cache can retain. Quake III reaches roughly
        // 195 MB during a single arena; a 512 MB cache prevents translation
        // churn across longer sessions and multiple maps.
        args += ["-accel", "tcg,tb-size=512"]
        let ati = effectiveGraphicsCard(for: config) == .atiRadeon
        if ati { args += ["-device", "classicmac-display-control"] }
        if !bootingFromUserCD && !ati && !legacy {
            // Mac OS 9 spends much of hard-disk startup polling timers for
            // hardware that mac99 does not expose. Instruction timing lets
            // those waits finish without sleeping on the host. The Finder
            // watcher above then invokes QEMU's atomic real-time handoff, so
            // applications, games, sound, and the system clock run normally.
            // The in-VRAM Finder detector belongs to the standard card, so
            // ATI starts run in real time from power-on, as on RetroBoot.
            args += ["-icount", "shift=4,sleep=off"]
        }
        args += ["-M", legacy ? "mac99,via=cuda,usb=off,audiodev=snd0" : "mac99,via=cuda,audiodev=snd0"]
        args += ["-cpu", config.useG4CPU ? "7400" : "g3"]
        args += ["-m", String(config.ramMB)]
        args += ["-L", AppPaths.pcBiosDir.path]
        // Native Cocoa is the default. The opt-in browser viewer instead uses
        // a private Unix VNC socket and a loopback-only WebSocket/noVNC page.
        args += displayArguments(for: config, browser: display)
        args += graphicsCardArguments(for: config, bootingFromUserCD: bootingFromUserCD)
        // Cached host I/O can complete a MacIO DBDMA command before the Mac OS
        // 9.2.x Installer arms its synchronous wait. Installer starts retain
        // the 1 ms safeguard that closes that race. A normal hard-disk start
        // completes immediately, avoiding thousands of main-loop timers on
        // the Finder boot path.
        let ideDMACompletionDelay = bootingFromUserCD ? 1_000_000 : 0
        args += [
            "-global",
            "macio-ide.dma-completion-delay-ns=\(ideDMACompletionDelay)"
        ]
        if !bootingFromUserCD && !legacy {
            args += ["-global", "macio-ide.pio-status-drain=on"]
        }
        // Route the OpenBIOS firmware console to the (disconnected) serial
        // port so the firmware text screens never appear. Together with the
        // bundled OpenBIOS's console background being repainted black (see
        // scripts/build-qemu.sh), the display stays black until the Mac OS
        // boot screen takes over.
        args += ["-prom-env", "output-device=ttya"]
        args += ["-name", qemuEscape(config.name)]

        // Audio through the screamer at 44.1 kHz. When sound is off, route to
        // the null backend so nothing touches the host audio device.
        if config.sound {
            args += ["-audiodev", "coreaudio,id=snd0,out.buffer-length=50000"]
        } else {
            args += ["-audiodev", "none,id=snd0"]
        }

        // HMP monitor on a unix socket so the app can pause/resume/reboot.
        args += ["-monitor", "unix:\(qemuEscape(QEMUManager.monitorSocketURL(for: config.id).path)),server=on,wait=off"]

        // An in-place system reset hangs the mac99 machine (a longstanding
        // QEMU limitation: the guest never comes back, leaving a black
        // screen), so turn any reset request into a clean QEMU exit instead.
        // The QMP socket lets the app see why QEMU exited (SHUTDOWN event
        // reason); on a reset reason it relaunches the VM, which makes
        // Restart behave like a real reboot. A guest Shut Down still just
        // stops the machine.
        args += ["-action", "reboot=shutdown"]
        args += ["-qmp", "unix:\(qemuEscape(QEMUManager.qmpSocketURL(for: config.id).path)),server=on,wait=off"]

        let escapedDiskPath = qemuEscape(config.diskImageURL.path)
        if config.useVirtioDisk {
            // Pin the startup device: OpenBIOS reuses virtio0 for every
            // block device, so an auxiliary disk must not select the boot disk.
            args += ["-drive", "file=\(escapedDiskPath),format=raw,if=none,id=startup0"]
            if !bootingG3CompatibilityCD {
                args += ["-device", "virtio-blk-pci,drive=startup0,addr=0xf"]
            }
            if !bootingFromUserCD {
                args += ["-prom-env", "boot-device=/pci@f2000000/scsi@f/disk:\\\\:tbxi"]
            }
        } else {
            args += ["-drive", "file=\(escapedDiskPath),format=raw,media=disk"]
        }

        // Keep a user-facing disc tray present on the optical IDE channel,
        // even when it starts empty, so the running window's Disc menu can
        // insert an image later. Give the primary optical position to a
        // selected startup disc; normal hard-disk starts reserve it for Tools.
        let userDiscIndex = bootingFromUserCD ? 2 : 3
        var userDisc = "if=ide,index=\(userDiscIndex),media=cdrom,id=cd0,readonly=on"
        let escapedCDPath = config.cdImagePath.map(qemuEscape)
        if let escapedCDPath, !escapedCDPath.isEmpty {
            userDisc += ",file=\(escapedCDPath),format=raw"
        }
        if !legacy || escapedCDPath?.isEmpty == false { args += ["-drive", userDisc] }
        if bootingG3CompatibilityCD {
            // Mac OS 8.5/8.6 cannot use mac99's KeyLargo IDE controller while
            // the ROM is starting. Mirror the selected disc read-only through
            // Virtio for startup while retaining the ordinary IDE source that
            // Installer needs after the system is running.
            args += [
                "-blockdev",
                "driver=file,node-name=classicmac-cd-file,filename=\(escapedCDPath!),read-only=on",
                "-blockdev",
                "driver=raw,node-name=classicmac-cd-boot,file=classicmac-cd-file,read-only=on",
                "-device",
                legacy ? "virtio-blk-pci,drive=classicmac-cd-boot,addr=0xf" : "virtio-blk-pci,drive=classicmac-cd-boot",
                "-prom-env",
                legacy ? "boot-device=/pci@f2000000/scsi@f/disk:\\\\:tbxi" : "boot-device=virtio0:\\\\:tbxi"
            ]
            if config.useVirtioDisk {
                // Keep the compatibility CD on the selected, explicit PCI path.
                args += ["-device", "virtio-blk-pci,drive=startup0,addr=0x10"]
            }
        } else if bootingFromUserCD {
            // Mac OS 9 on the default G4 profile starts and installs reliably
            // from KeyLargo IDE. Keeping the image on this one path avoids a
            // duplicate desktop volume and preserves Installer's native I/O.
            args += ["-boot", "d"]
        } else if legacy, let escapedCDPath, !escapedCDPath.isEmpty {
            // Mirror an inserted CD read-only through Virtio on the legacy
            // 8.5/8.6 profile, where KeyLargo IDE optical drives do not mount.
            args += [
                "-blockdev",
                "driver=file,node-name=classicmac-cd-file,filename=\(escapedCDPath),read-only=on",
                "-blockdev",
                "driver=raw,node-name=classicmac-cd,file=classicmac-cd-file,read-only=on",
                "-device",
                "virtio-blk-pci,drive=classicmac-cd,addr=0x11"
            ]
        }

        // Keep the compatibility IDE tray present but empty. Mac OS 9 fails to
        // notice live changes to this QEMU IDE tray, so the actual Tools image
        // is exposed below through the reliable Virtio block path instead.
        let toolsDiscIndex = bootingFromUserCD ? 3 : 2
        if !legacy {
            args += ["-drive", toolsDriveSpec(
                for: config, iface: "ide", index: toolsDiscIndex, loadMedia: false
            )]
        }

        if toolsViaVirtio, let toolsCD = AppPaths.toolsCD {
            let escapedToolsPath = qemuEscape(toolsCD.path)
            args += [
                "-blockdev",
                "driver=file,node-name=classicmac-tools-file,filename=\(escapedToolsPath),read-only=on",
                "-blockdev",
                "driver=raw,node-name=classicmac-tools,file=classicmac-tools-file,read-only=on",
                "-device",
                legacy ? "virtio-blk-pci,drive=classicmac-tools,addr=0x10" : "virtio-blk-pci,drive=classicmac-tools"
            ]
        }

        // User-mode networking through the mac99 onboard sungem ethernet.
        let nicModel = legacy ? "rtl8139" : "sungem"
        args += ["-nic", nicArgument(model: nicModel, networking: config.networking, printerPorts: printerPorts)]
        if legacy && (config.networking || printerPorts != nil) {
            args += ["-global", "rtl8139.romfile=", "-global", "rtl8139.tx-delay-ns=1000000"]
        }

        // Shared folder via virtio-9p-pci and the classicvirtio ndrvloader.
        // The loader is placed in guest RAM by QEMU's generic loader device and
        // executed by OpenBIOS in place of the default boot command. Tablet
        // input uses the same loader to install the virtio-tablet-pci driver.
        if needsNdrvLoader {
            args += ["-device", "loader,addr=0x4000000,file=\(qemuEscape(AppPaths.ndrvLoader.path))"]
            args += ["-prom-env", "boot-command=init-program go"]
            if tablet {
                // Automatic slot 18 shares Sungem's interrupt when ATI and
                // Tools are present. Mac OS 9 can stall during startup.
                // On the legacy 8.5/8.6 profile, skip slot 18 (rtl8139 IRQ 29)
                // and pin Tools/CD/9P/Tablet to 0x10/0x11/0x13/0x14.
                let tabletDevice: String
                if legacy {
                    tabletDevice = "virtio-tablet-pci,addr=0x14"
                } else if ati && toolsViaVirtio && (config.networking || printerPorts != nil) {
                    tabletDevice = "virtio-tablet-pci,addr=0x13"
                } else {
                    tabletDevice = "virtio-tablet-pci"
                }
                args += ["-device", tabletDevice]
            }
            if sharing {
                let shareDevice = legacy
                    ? "virtio-9p-pci,addr=0x13,fsdev=share0,mount_tag=\(qemuEscape(config.sharedVolumeName))"
                    : "virtio-9p-pci,fsdev=share0,mount_tag=\(qemuEscape(config.sharedVolumeName))"
                args += ["-device", shareDevice]
                let escapedPath = qemuEscape(config.sharedFolderPath!)
                args += ["-fsdev", "local,id=share0,security_model=none,path=\(escapedPath)"]
            }
        }

        return args
    }

    private static func buildQuadraArguments(
        for config: VMConfig,
        display: BrowserDisplayEndpoint,
        printerPorts: PrinterForwardPorts? = nil
    ) -> [String] {
        var args: [String] = []

        let sharing = config.hasSharedFolder
        let tablet = config.tabletInput

        // The nubus-virtio-mmio card carries the classicvirtio folder-sharing,
        // tablet, and removable floppy drivers. Keep it present even when the
        // floppy starts empty so the running Mac menu can insert an image.
        let needsVirtioCard = sharing || tablet ||
            config.machineFamily.supportsFloppyDisk

        // Framebuffer selection.
        // - No virtio card + enhanced: machine creates the qfb (fb=qemu) and -g applies.
        // - Virtio card + enhanced: the virtio card must precede the framebuffer card,
        //   so the machine creates no framebuffer (fb=none) and we add nubus-qfb as
        //   a -device after nubus-virtio-mmio (its size comes from device options).
        // - Not enhanced: leave the built-in macfb (fb=mac default) and -g applies.
        var machine = "q800"
        let qfbAsDevice = needsVirtioCard && config.useEnhancedFramebuffer
        if config.useEnhancedFramebuffer {
            if needsVirtioCard {
                machine += ",fb=none"
            } else {
                machine += ",fb=qemu"
            }
        }
        // Route the Apple Sound Chip to a named audiodev so we can silence it.
        machine += ",audiodev=snd0"
        args += ["-M", machine]

        args += ["-m", String(config.ramMB)]
        args += ["-bios", AppPaths.quadraROM.path]
        args += ["-L", AppPaths.pcBiosDir.path]
        args += displayArguments(for: config, browser: display)
        // -g only applies to machine-created framebuffers; when the qfb is added as
        // a device its size is set via device options instead.
        if !qfbAsDevice {
            args += ["-g", "\(config.width)x\(config.height)x\(config.depth)"]
        }
        args += ["-name", qemuEscape(config.name)]

        // Audio. The Apple Sound Chip is patched (see qfb/asc-silence.patch) to
        // always feed the backend silence when idle, so a live CoreAudio backend
        // no longer hums/buzzes when the Mac is quiet. A generous output buffer
        // guards against underrun crackle at the ASC's low 22 kHz sample rate.
        // When sound is off we route to the null backend so nothing touches the
        // host audio device at all.
        if config.sound {
            args += ["-audiodev", "coreaudio,id=snd0,out.buffer-length=50000"]
        } else {
            args += ["-audiodev", "none,id=snd0"]
        }

        // HMP monitor on a unix socket so the app can pause/resume/reboot.
        args += ["-monitor", "unix:\(qemuEscape(QEMUManager.monitorSocketURL(for: config.id).path)),server=on,wait=off"]

        // End the process at the reset boundary so the app can inspect the
        // hard disk's blessing and choose CD or disk for the relaunch. This is
        // also used for ordinary Quadra restarts, mirroring the mac99 path.
        args += ["-action", "reboot=shutdown"]
        args += ["-qmp", "unix:\(qemuEscape(QEMUManager.qmpSocketURL(for: config.id).path)),server=on,wait=off"]

        // PRAM (stores screen resolution + boot order across reboots).
        args += ["-drive", "file=\(qemuEscape(config.pramImageURL.path)),format=raw,if=mtd"]

        // Main SCSI hard disk at ID 0.
        args += ["-device", "scsi-hd,scsi-id=0,drive=hd0"]
        args += ["-drive", "file=\(qemuEscape(config.diskImageURL.path)),media=disk,format=raw,if=none,id=hd0"]

        // User-facing CD-ROM at SCSI ID 3. Keep its tray available even when
        // empty so the running window's Disc menu can insert an image.
        args += ["-device", "scsi-cd,scsi-id=3,drive=cd0"]
        var userDisc = "media=cdrom,if=none,id=cd0,readonly=on"
        if let cdPath = config.cdImagePath, !cdPath.isEmpty {
            userDisc += ",file=\(qemuEscape(cdPath)),format=raw"
            if config.bootFromCD {
                args += ["-boot", "d"]
            }
        }
        args += ["-drive", userDisc]

        // Dedicated second CD drive (SCSI ID 4) for the ClassicMac Tools CD;
        // see the Power Mac builder for the rationale.
        args += ["-device", "scsi-cd,scsi-id=4,drive=tools0"]
        args += ["-drive", toolsDriveSpec(for: config, iface: "none")]

        // Writable raw floppy images use the bundled classicvirtio block
        // driver. An empty optical-style backend lets QEMU create a drive with
        // no initial medium; the patched removable VirtIO device then reports
        // insert/eject capacity changes to classic Mac OS.
        var floppy = "if=none,id=fd0"
        if let floppyPath = config.floppyImagePath, !floppyPath.isEmpty {
            floppy += ",file=\(qemuEscape(floppyPath)),format=raw"
        } else {
            floppy += ",media=cdrom,readonly=off"
        }
        args += ["-drive", floppy]

        // User-mode networking through the Quadra's built-in SONIC ethernet.
        // The q800 machine creates the onboard dp8393x and binds it to nd_table[0],
        // so networking is configured with -nic (not a separate -device).
        args += ["-nic", nicArgument(model: "dp83932", networking: config.networking, printerPorts: printerPorts)]

        // Shared folder via the classicvirtio NuBus virtio transport. The
        // nubus-virtio-mmio card must be created before the nubus-qfb framebuffer
        // so it lands in the earlier NuBus slot. The same card also provides
        // tablet input when enabled.
        if needsVirtioCard {
            args += ["-device", "nubus-virtio-mmio,romfile=\(qemuEscape(AppPaths.declROM.path))"]
            args += ["-device", "virtio-blk-device,drive=fd0,removable=on"]
            if qfbAsDevice {
                args += ["-device", "nubus-qfb,width=\(config.width),height=\(config.height),depth=\(config.depth)"]
            }
            if tablet {
                args += ["-device", "virtio-tablet-device"]
            }
            if sharing {
                args += ["-device", "virtio-9p-device,fsdev=share0,mount_tag=\(qemuEscape(config.sharedVolumeName))"]
                let escapedPath = qemuEscape(config.sharedFolderPath!)
                args += ["-fsdev", "local,id=share0,security_model=none,path=\(escapedPath)"]
            }
        }

        return args
    }

    // MARK: qemu-img helpers

    nonisolated static func createRawImage(at url: URL, sizeArgument: String) -> CommandResult {
        if FileManager.default.fileExists(atPath: url.path) {
            return .success("Image already exists")
        }
        // Raw images retain maximum compatibility with classic disk drivers.
        // Explicitly disable preallocation so even a 120 GB virtual disk only
        // consumes host storage as sectors are actually written.
        return runQemuImg([
            "create", "-f", "raw", "-o", "preallocation=off",
            url.path, sizeArgument
        ])
    }

    @discardableResult
    nonisolated static func runQemuImg(_ arguments: [String]) -> CommandResult {
        let binary = AppPaths.qemuImgBinary
        guard FileManager.default.isExecutableFile(atPath: binary.path) else {
            return .failure("The disk tool is missing from this copy of ClassicMac. Reinstall ClassicMac to fix this.")
        }
        let process = Process()
        process.executableURL = binary
        process.arguments = arguments
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        do {
            try process.run()
        } catch {
            return .failure(error.localizedDescription)
        }
        process.waitUntilExit()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        let output = String(data: data, encoding: .utf8) ?? ""
        if process.terminationStatus == 0 {
            return .success(output)
        }
        return .failure(output)
    }
}
