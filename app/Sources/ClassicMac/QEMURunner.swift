import Foundation
import SwiftUI
import AppKit

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
    private let started = ProcessInfo.processInfo.systemUptime
    private let lock = NSLock()
    private var cancelled = false

    init(socketPath: String) {
        self.socketPath = socketPath
    }

    func cancel() {
        lock.lock()
        cancelled = true
        lock.unlock()
    }

    private var isCancelled: Bool {
        lock.lock()
        defer { lock.unlock() }
        return cancelled
    }

    private func wait(until target: TimeInterval) -> Bool {
        while ProcessInfo.processInfo.systemUptime - started < target {
            if isCancelled { return false }
            usleep(25_000)
        }
        return !isCancelled
    }

    func run() {
        guard wait(until: 15.0) else { return }
        _ = HMPClient.command("classicmac-boot-complete",
                              socketPath: socketPath)
    }
}

// Launches and tracks emulator processes, and builds qemu-img and
// qemu-system-m68k / qemu-system-ppc command lines from a VMConfig.
@MainActor
final class QEMUManager: ObservableObject {
    static let shared = QEMUManager()

    @Published private(set) var runningIDs: Set<UUID> = []
    @Published private(set) var pausedIDs: Set<UUID> = []
    // The latest screen capture of each machine, refreshed while it runs and
    // kept after shutdown so the library shows what was last on screen.
    @Published private(set) var previews: [UUID: NSImage] = [:]
    @Published var lastError: AppError?
    @Published private(set) var pendingStopID: UUID?

    private var processes: [UUID: Process] = [:]
    private var qmpMonitors: [UUID: QMPEventMonitor] = [:]
    private var previewTimers: [UUID: Timer] = [:]
    private var bootClockHandoffs: [UUID: BootClockHandoff] = [:]
    private var forcedStopWorkItems: [UUID: DispatchWorkItem] = [:]

    func isRunning(_ id: UUID) -> Bool {
        runningIDs.contains(id)
    }

    func isPaused(_ id: UUID) -> Bool {
        pausedIDs.contains(id)
    }

    static func monitorSocketURL(for id: UUID) -> URL {
        // Unix domain socket paths must fit in sockaddr_un (< 104 bytes on macOS),
        // so the long Application Support path cannot be used. Keep it short.
        let dir = URL(fileURLWithPath: "/tmp/ClassicMac", isDirectory: true)
        AppPaths.ensureDirectory(dir)
        return dir.appendingPathComponent("\(id.uuidString).sock")
    }

    static func qmpSocketURL(for id: UUID) -> URL {
        let dir = URL(fileURLWithPath: "/tmp/ClassicMac", isDirectory: true)
        AppPaths.ensureDirectory(dir)
        return dir.appendingPathComponent("\(id.uuidString).qmp.sock")
    }

    // SHUTDOWN event reasons after which the VM should boot right back up:
    // a restart chosen inside the guest, or the app's own Restart command
    // (system_reset over the monitor socket). Power Mac VMs run with
    // -action reboot=shutdown, so both arrive as a clean exit + this reason
    // instead of an in-place reset, which hangs the mac99 machine.
    private static let relaunchReasons: Set<String> = ["guest-reset", "host-qmp-system-reset"]

    func start(_ config: VMConfig) {
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

        // Remove any stale sockets so QEMU can bind fresh ones.
        try? FileManager.default.removeItem(at: QEMUManager.monitorSocketURL(for: config.id))
        try? FileManager.default.removeItem(at: QEMUManager.qmpSocketURL(for: config.id))

        let process = Process()
        process.executableURL = AppPaths.qemuBinary(for: config.machineFamily)
        process.arguments = QEMUManager.buildArguments(for: config)
        process.currentDirectoryURL = config.folder

        // The Quadra's SCSI CD driver notices live media changes, so publish
        // the bundled image to its running-window menu. Power Macs receive
        // Tools through a read-only Virtio disk at startup instead: Mac OS 9's
        // IDE driver accepts a live QEMU media change but never tells Finder
        // to mount it, which made the old Insert command look successful while
        // doing nothing in the guest.
        var environment = ProcessInfo.processInfo.environment
        if config.machineFamily == .quadra800, let toolsCD = AppPaths.toolsCD {
            environment["CLASSICMAC_TOOLS_CD"] = toolsCD.path
        } else {
            environment.removeValue(forKey: "CLASSICMAC_TOOLS_CD")
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
                let monitor = self.qmpMonitors.removeValue(forKey: config.id)
                self.runningIDs.remove(config.id)
                self.pausedIDs.remove(config.id)
                if self.pendingStopID == config.id {
                    self.pendingStopID = nil
                }
                self.processes.removeValue(forKey: config.id)
                self.forcedStopWorkItems.removeValue(forKey: config.id)?.cancel()
                self.stopBootClockHandoff(for: config.id)
                self.stopPreviewUpdates(for: config.id)
                self.persistPreview(config)
                if proc.terminationStatus != 0 && proc.terminationReason == .exit {
                    monitor?.cancel()
                    self.lastError = AppError(
                        "\u{201C}\(config.name)\u{201D} Shut Down Unexpectedly",
                        "The Mac stopped on its own. Try starting it again.",
                        logURL: AppLog.write(message, machineName: config.name)
                    )
                    return
                }
                guard let monitor = monitor else { return }
                // A restart (from inside the guest or via the app's Restart
                // command) exits QEMU cleanly with a reset reason; boot the
                // machine right back up so it behaves like a real reboot.
                let reason = await monitor.shutdownReasonAfterExit(timeout: 2)
                monitor.cancel()
                if let reason = reason, QEMUManager.relaunchReasons.contains(reason) {
                    self.start(config)
                }
            }
        }

        do {
            try process.run()
        } catch {
            lastError = AppError(
                "Couldn't Start \u{201C}\(config.name)\u{201D}",
                "The Mac could not be started. \(error.localizedDescription)"
            )
            return
        }

        processes[config.id] = process
        runningIDs.insert(config.id)

        // Power Mac VMs report shutdown/restart intent over QMP (see
        // relaunchReasons above); watch the event stream for this run.
        if config.machineFamily == .powerMacG4 {
            let monitor = QMPEventMonitor(socketPath: QEMUManager.qmpSocketURL(for: config.id).path)
            monitor.start()
            qmpMonitors[config.id] = monitor
        }

        let bootingFromUserCD = config.bootFromCD &&
            config.cdImagePath?.isEmpty == false
        let acceleratedHardDiskBoot = config.machineFamily == .powerMacG4 &&
            !bootingFromUserCD
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

        // Bring the machine window to the front once it exists. The window
        // appears a moment after the process spawns, so try twice.
        activate(config.id, afterDelay: 0.7)
        activate(config.id, afterDelay: 2.0)
    }

    // MARK: Machine window

    // Brings the machine's window (a separate helper app process) to the front.
    func activate(_ id: UUID) {
        guard let process = processes[id],
              let app = NSRunningApplication(processIdentifier: process.processIdentifier) else {
            return
        }
        app.activate(from: .current, options: [])
    }

    private func activate(_ id: UUID, afterDelay delay: TimeInterval) {
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            self?.activate(id)
        }
    }

    // MARK: Screen previews

    private static func screenDumpURL(for id: UUID) -> URL {
        let dir = URL(fileURLWithPath: "/tmp/ClassicMac", isDirectory: true)
        AppPaths.ensureDirectory(dir)
        return dir.appendingPathComponent("\(id.uuidString).screen.ppm")
    }

    private func startBootClockHandoff(for config: VMConfig) {
        stopBootClockHandoff(for: config.id)
        let handoff = BootClockHandoff(
            socketPath: QEMUManager.monitorSocketURL(for: config.id).path
        )
        bootClockHandoffs[config.id] = handoff
        DispatchQueue.global(qos: .userInitiated).async {
            handoff.run()
        }
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
        let socketPath = QEMUManager.monitorSocketURL(for: config.id).path
        let dumpURL = QEMUManager.screenDumpURL(for: config.id)
        DispatchQueue.global(qos: .utility).async { [weak self] in
            guard HMPClient.send("screendump \(dumpURL.path)", socketPath: socketPath) else { return }
            // The dump is written asynchronously after the command; give the
            // emulator a moment before reading it back.
            usleep(400_000)
            guard let image = PPMImage.load(dumpURL) else { return }
            Task { @MainActor in
                self?.previews[config.id] = image
            }
        }
    }

    // Keeps the machine's last screen inside its .classic package so the
    // library can show it across launches.
    private func persistPreview(_ config: VMConfig) {
        guard let image = previews[config.id], let bundle = config.bundleURL else { return }
        guard let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) else {
            return
        }
        try? png.write(to: bundle.appendingPathComponent("preview.png"))
    }

    func requestStop(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        pendingStopID = id
    }

    func cancelStop() {
        pendingStopID = nil
    }

    func confirmStop() {
        guard let id = pendingStopID else { return }
        pendingStopID = nil
        stop(id)
    }

    private func stop(_ id: UUID) {
        guard let process = processes[id] else { return }
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
                Task { @MainActor in
                    process?.terminate()
                    self?.forcedStopWorkItems.removeValue(forKey: id)?.cancel()
                }
                return
            }
            usleep(750_000)
            guard process?.isRunning == true,
                  HMPClient.send("sendkey ret", socketPath: socketPath) else {
                Task { @MainActor in
                    if process?.isRunning == true {
                        process?.terminate()
                    }
                    self?.forcedStopWorkItems.removeValue(forKey: id)?.cancel()
                }
                return
            }
        }

        // A crashed guest or an application blocking shutdown must not leave
        // the emulator stuck forever. This preserves the old forced-off
        // behavior as a bounded fallback, after giving Mac OS ample time to
        // finish an ordinary shutdown.
        let fallback = DispatchWorkItem { [weak self, weak process] in
            if process?.isRunning == true {
                process?.terminate()
            }
            Task { @MainActor in
                self?.forcedStopWorkItems.removeValue(forKey: id)
            }
        }
        forcedStopWorkItems[id]?.cancel()
        forcedStopWorkItems[id] = fallback
        DispatchQueue.main.asyncAfter(deadline: .now() + 15, execute: fallback)
    }

    func pause(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        pausedIDs.insert(id)
        sendMonitor("stop", to: id, actionLabel: "Pause") { [weak self] in
            // Undo the optimistic state change so the UI matches reality.
            self?.pausedIDs.remove(id)
        }
    }

    func resume(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        pausedIDs.remove(id)
        sendMonitor("cont", to: id, actionLabel: "Resume") { [weak self] in
            self?.pausedIDs.insert(id)
        }
    }

    func reboot(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        pausedIDs.remove(id)
        sendMonitor("system_reset", to: id, actionLabel: "Restart", onFailure: nil)
    }

    // Sends a control command to the running machine. Failures (a dead or
    // unresponsive control socket) surface as an alert instead of silently
    // doing nothing.
    private func sendMonitor(_ command: String, to id: UUID, actionLabel: String, onFailure: (() -> Void)?) {
        let path = QEMUManager.monitorSocketURL(for: id).path
        DispatchQueue.global(qos: .userInitiated).async {
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

    // MARK: Argument construction

    static func buildArguments(for config: VMConfig) -> [String] {
        switch config.machineFamily {
        case .quadra800:
            return buildQuadraArguments(for: config)
        case .powerMacG4:
            return buildPowerMacArguments(for: config)
        }
    }

    // Extra cocoa display options for the classic input helpers (right-click
    // as Control+click, scroll wheel as arrow keys). Empty when the helpers
    // are turned off for this VM.
    private static func inputHelperOptions(for config: VMConfig) -> String {
        if config.classicInputHelpers {
            return ",right-click-ctrl=on,scroll-keys=on"
        }
        return ""
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
            options += ["file=\(toolsCD.path)", "format=raw"]
        }
        options.append("readonly=on")
        return options.joined(separator: ",")
    }

    // qemu-system-ppc -M mac99: a New World Power Mac that boots Mac OS 8.5
    // through 9.2.2 (and early OS X) via the bundled OpenBIOS firmware, so no
    // Apple ROM is involved. Storage is IDE, networking is the sungem NIC.
    // Sound comes from the screamer (AWACS) device ported onto our QEMU build;
    // the guest driver attaches because the bundled OpenBIOS advertises the
    // davbus/awacs nodes.
    private static func buildPowerMacArguments(for config: VMConfig) -> [String] {
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
        let bootingG3CompatibilityCD = bootingFromUserCD && !config.useG4CPU
        let deferToolsMedia = bootingFromUserCD
        let sharing = config.hasSharedFolder && !bootingFromUserCD
        let tablet = config.tabletInput
        // On Power Macs the Tools HFS image is a read-only Virtio block disk.
        // It uses the same startup NDRV loader as folder sharing and mounts as
        // a normal desktop volume without relying on IDE hot-plug detection.
        let toolsViaVirtio = !deferToolsMedia && config.toolsCDInserted &&
            AppPaths.toolsCD != nil
        let needsNdrvLoader = sharing || tablet || bootingG3CompatibilityCD ||
            toolsViaVirtio

        // Input configuration. QEMU treats whichever pointing device the guest
        // touched last as "the mouse", and the cocoa display only releases the
        // cursor while that device is absolute.
        //
        // Mac OS 8.5 and 8.6 predate the KeyLargo PMU used by QEMU's default
        // mac99 profile, so all guests use the CUDA/ADB profile. Mac OS 8.6
        // and 9 can opt into the faster 7400/G4 model; Mac OS 8.5 retains the
        // original G3 identity advertised by the bundled OpenBIOS. The ADB
        // mouse is also a working captured fallback until the optional Virtio
        // tablet driver loads.
        // Large PowerPC games generate far more translated code than QEMU's
        // small default TCG cache can retain. Quake III reaches roughly
        // 195 MB during a single arena; a 512 MB cache prevents translation
        // churn across longer sessions and multiple maps.
        args += ["-accel", "tcg,tb-size=512"]
        if !bootingFromUserCD {
            // Mac OS 9 spends much of hard-disk startup polling timers for
            // hardware that mac99 does not expose. Instruction timing lets
            // those waits finish without sleeping on the host. The Finder
            // watcher above then invokes QEMU's atomic real-time handoff, so
            // applications, games, sound, and the system clock run normally.
            args += ["-icount", "shift=4,sleep=off"]
        }
        args += ["-M", "mac99,via=cuda,audiodev=snd0"]
        args += ["-cpu", config.useG4CPU ? "7400" : "g3"]
        args += ["-m", String(config.ramMB)]
        args += ["-L", AppPaths.pcBiosDir.path]
        // right-click-ctrl: deliver right clicks as Control+click so Mac OS
        // 8/9 contextual menus open. scroll-keys: turn scroll wheel motion
        // into arrow-key taps (classic Mac OS has no wheel driver). Both are
        // ClassicMac additions to the cocoa display (cocoaui/input-remap.patch),
        // and both are per-VM: off when the guest has a real driver such as
        // USB Overdrive installed.
        args += ["-display", "cocoa,swap-opt-cmd=off\(inputHelperOptions(for: config))"]
        // Live window resizing: expose the host-resize request registers on
        // the std VGA device (see ppcvid/vga-host-resize.patch). The bundled
        // qemu_vga.ndrv polls them and switches the guest resolution through
        // the Display Manager when the window is dragged to a new size.
        // "-vga std" must be explicit: QEMU treats a -global for the VGA
        // driver as a user-configured display and would otherwise skip
        // creating the default one. 64 MB of VRAM covers 3840x2160 at 32-bit
        // (the default 16 MB tops out below 4K).
        args += ["-vga", "std"]
        args += ["-global", "VGA.host-resize=on"]
        args += ["-global", "VGA.vgamem_mb=64"]
        // Packed 1/2/4-bpp modes (ppcvid/vga-packed-depths.patch) let the
        // bundled qemu_vga.ndrv offer Black & White, 4 and 16 colors in the
        // Monitors control panel alongside 256/thousands/millions.
        args += ["-global", "VGA.packed-lowbpp=on"]
        // Direct Cocoa scanout does not need QEMU to write-protect and dirty-
        // track each framebuffer page. Disabling those repeated TCG traps is
        // particularly important for QuickDraw's framebuffer-heavy BitBlt and
        // shape operations. Shadowed low-color modes keep normal tracking.
        args += ["-global", "VGA.untracked-vram=on"]
        // Move the classic Mac cursor through QEXT and let Cocoa composite it,
        // avoiding framebuffer redraws for pointer motion. Older guest drivers
        // ignore the advertised feature and keep their software cursor.
        args += ["-global", "VGA.hardware-cursor=on"]
        // GXMetal exposes a versioned QuickDraw 3D RAVE command queue through
        // the VGA device. The guest engine still checks the advertised feature
        // bits before claiming a context, so unfinished or unsupported drawing
        // paths continue through the system software renderer.
        args += ["-global", "VGA.gxmetal=on"]
        if !bootingFromUserCD {
            // Let QEMU recognize Finder directly in guest VRAM and perform
            // the clock handoff without full-frame screenshots or a VNC
            // client. BootClockHandoff above remains a 15-second fallback.
            args += ["-global", "VGA.classicmac-boot-handoff=on"]
        }
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
        // Route the OpenBIOS firmware console to the (disconnected) serial
        // port so the firmware text screens never appear. Together with the
        // bundled OpenBIOS's console background being repainted black (see
        // scripts/build-qemu.sh), the display stays black until the Mac OS
        // boot screen takes over.
        args += ["-prom-env", "output-device=ttya"]
        // OpenBIOS sizes the framebuffer from -g at boot. QEMU's mac99
        // firmware interface names the classic direct-color modes 15 and 32
        // bpp; the UI presents those as Thousands and Millions.
        let powerMacBootDepth = config.depth == ColorDepth.millions.rawValue ? 32 : 15
        args += ["-g", "\(config.width)x\(config.height)x\(powerMacBootDepth)"]
        args += ["-name", config.name]

        // Audio through the screamer at 44.1 kHz. When sound is off, route to
        // the null backend so nothing touches the host audio device.
        if config.sound {
            args += ["-audiodev", "coreaudio,id=snd0,out.buffer-length=50000"]
        } else {
            args += ["-audiodev", "none,id=snd0"]
        }

        // HMP monitor on a unix socket so the app can pause/resume/reboot.
        args += ["-monitor", "unix:\(QEMUManager.monitorSocketURL(for: config.id).path),server=on,wait=off"]

        // An in-place system reset hangs the mac99 machine (a longstanding
        // QEMU limitation: the guest never comes back, leaving a black
        // screen), so turn any reset request into a clean QEMU exit instead.
        // The QMP socket lets the app see why QEMU exited (SHUTDOWN event
        // reason); on a reset reason it relaunches the VM, which makes
        // Restart behave like a real reboot. A guest Shut Down still just
        // stops the machine.
        args += ["-action", "reboot=shutdown"]
        args += ["-qmp", "unix:\(QEMUManager.qmpSocketURL(for: config.id).path),server=on,wait=off"]

        // Main IDE hard disk.
        args += ["-drive", "file=\(config.diskImageURL.path),format=raw,media=disk"]

        // Keep a user-facing disc tray present on the optical IDE channel,
        // even when it starts empty, so the running window's Disc menu can
        // insert an image later. Give the primary optical position to a
        // selected startup disc; normal hard-disk starts reserve it for Tools.
        let userDiscIndex = bootingFromUserCD ? 2 : 3
        var userDisc = "if=ide,index=\(userDiscIndex),media=cdrom,id=cd0,readonly=on"
        let escapedCDPath = config.cdImagePath?
            .replacingOccurrences(of: ",", with: ",,")
        if let escapedCDPath, !escapedCDPath.isEmpty {
            userDisc += ",file=\(escapedCDPath),format=raw"
        }
        args += ["-drive", userDisc]
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
                "virtio-blk-pci,drive=classicmac-cd-boot",
                "-prom-env",
                "boot-device=virtio0:\\\\:tbxi"
            ]
        } else if bootingFromUserCD {
            // Mac OS 9 on the default G4 profile starts and installs reliably
            // from KeyLargo IDE. Keeping the image on this one path avoids a
            // duplicate desktop volume and preserves Installer's native I/O.
            args += ["-boot", "d"]
        }

        // Keep the compatibility IDE tray present but empty. Mac OS 9 fails to
        // notice live changes to this QEMU IDE tray, so the actual Tools image
        // is exposed below through the reliable Virtio block path instead.
        let toolsDiscIndex = bootingFromUserCD ? 3 : 2
        args += ["-drive", toolsDriveSpec(
            for: config,
            iface: "ide",
            index: toolsDiscIndex,
            loadMedia: false
        )]

        if toolsViaVirtio, let toolsCD = AppPaths.toolsCD {
            let escapedToolsPath = toolsCD.path
                .replacingOccurrences(of: ",", with: ",,")
            args += [
                "-blockdev",
                "driver=file,node-name=classicmac-tools-file,filename=\(escapedToolsPath),read-only=on",
                "-blockdev",
                "driver=raw,node-name=classicmac-tools,file=classicmac-tools-file,read-only=on",
                "-device",
                "virtio-blk-pci,drive=classicmac-tools"
            ]
        }

        // User-mode networking through the mac99 onboard sungem ethernet.
        if config.networking {
            args += ["-nic", "user,model=sungem"]
        } else {
            // QEMU otherwise creates the mac99 machine's default Sungem NIC.
            args += ["-nic", "none"]
        }

        // Shared folder via virtio-9p-pci and the classicvirtio ndrvloader.
        // The loader is placed in guest RAM by QEMU's generic loader device and
        // executed by OpenBIOS in place of the default boot command. Tablet
        // input uses the same loader to install the virtio-tablet-pci driver.
        if needsNdrvLoader {
            args += ["-device", "loader,addr=0x4000000,file=\(AppPaths.ndrvLoader.path)"]
            args += ["-prom-env", "boot-command=init-program go"]
            if tablet {
                args += ["-device", "virtio-tablet-pci"]
            }
            if sharing {
                args += ["-device", "virtio-9p-pci,fsdev=share0,mount_tag=\(config.sharedVolumeName)"]
                let escapedPath = config.sharedFolderPath!.replacingOccurrences(of: ",", with: ",,")
                args += ["-fsdev", "local,id=share0,security_model=none,path=\(escapedPath)"]
            }
        }

        return args
    }

    private static func buildQuadraArguments(for config: VMConfig) -> [String] {
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
        // Map the host Command key to the guest Command key (not Option), so
        // shortcuts like Cmd-W reach classic Mac OS. The left Command key still
        // only passes through once the window has grabbed the mouse (click in it).
        // right-click-ctrl/scroll-keys: same classic-input remapping as the
        // Power Mac (contextual menus via Control+click, wheel as arrow keys).
        args += ["-display", "cocoa,swap-opt-cmd=off\(inputHelperOptions(for: config))"]
        // -g only applies to machine-created framebuffers; when the qfb is added as
        // a device its size is set via device options instead.
        if !qfbAsDevice {
            args += ["-g", "\(config.width)x\(config.height)x\(config.depth)"]
        }
        args += ["-name", config.name]

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
        args += ["-monitor", "unix:\(QEMUManager.monitorSocketURL(for: config.id).path),server=on,wait=off"]

        // PRAM (stores screen resolution + boot order across reboots).
        args += ["-drive", "file=\(config.pramImageURL.path),format=raw,if=mtd"]

        // Main SCSI hard disk at ID 0.
        args += ["-device", "scsi-hd,scsi-id=0,drive=hd0"]
        args += ["-drive", "file=\(config.diskImageURL.path),media=disk,format=raw,if=none,id=hd0"]

        // User-facing CD-ROM at SCSI ID 3. Keep its tray available even when
        // empty so the running window's Disc menu can insert an image.
        args += ["-device", "scsi-cd,scsi-id=3,drive=cd0"]
        var userDisc = "media=cdrom,if=none,id=cd0,readonly=on"
        if let cdPath = config.cdImagePath, !cdPath.isEmpty {
            userDisc += ",file=\(cdPath),format=raw"
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
            floppy += ",file=\(floppyPath),format=raw"
        } else {
            floppy += ",media=cdrom,readonly=off"
        }
        args += ["-drive", floppy]

        // User-mode networking through the Quadra's built-in SONIC ethernet.
        // The q800 machine creates the onboard dp8393x and binds it to nd_table[0],
        // so networking is configured with -nic (not a separate -device).
        if config.networking {
            args += ["-nic", "user,model=dp83932"]
        } else {
            // QEMU otherwise creates the q800 machine's default NIC.
            args += ["-nic", "none"]
        }

        // Shared folder via the classicvirtio NuBus virtio transport. The
        // nubus-virtio-mmio card must be created before the nubus-qfb framebuffer
        // so it lands in the earlier NuBus slot. The same card also provides
        // tablet input when enabled.
        if needsVirtioCard {
            args += ["-device", "nubus-virtio-mmio,romfile=\(AppPaths.declROM.path)"]
            args += ["-device", "virtio-blk-device,drive=fd0,removable=on"]
            if qfbAsDevice {
                args += ["-device", "nubus-qfb,width=\(config.width),height=\(config.height),depth=\(config.depth)"]
            }
            if tablet {
                args += ["-device", "virtio-tablet-device"]
            }
            if sharing {
                args += ["-device", "virtio-9p-device,fsdev=share0,mount_tag=\(config.sharedVolumeName)"]
                let escapedPath = config.sharedFolderPath!.replacingOccurrences(of: ",", with: ",,")
                args += ["-fsdev", "local,id=share0,security_model=none,path=\(escapedPath)"]
            }
        }

        return args
    }

    // MARK: qemu-img helpers

    static func createRawImage(at url: URL, sizeArgument: String) -> CommandResult {
        if FileManager.default.fileExists(atPath: url.path) {
            return .success("Image already exists")
        }
        return runQemuImg(["create", "-f", "raw", url.path, sizeArgument])
    }

    @discardableResult
    static func runQemuImg(_ arguments: [String]) -> CommandResult {
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
