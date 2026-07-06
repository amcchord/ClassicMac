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

    private var processes: [UUID: Process] = [:]
    private var qmpMonitors: [UUID: QMPEventMonitor] = [:]
    private var previewTimers: [UUID: Timer] = [:]

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

        // Tell the QEMU window where the bundled Tools CD lives so its
        // Machine menu can offer "Insert Tools CD" while the Mac runs.
        var environment = ProcessInfo.processInfo.environment
        if let toolsCD = AppPaths.toolsCD {
            environment["CLASSICMAC_TOOLS_CD"] = toolsCD.path
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
                self.processes.removeValue(forKey: config.id)
                self.stopPreviewUpdates(for: config.id)
                self.persistPreview(config)
                if proc.terminationStatus != 0 && proc.terminationReason == .exit {
                    monitor?.cancel()
                    self.lastError = AppError(
                        "\u{201C}\(config.name)\u{201D} Shut Down Unexpectedly",
                        "The emulated Mac stopped on its own. Try starting it again.",
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
                "The emulator could not be launched. \(error.localizedDescription)"
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

        startPreviewUpdates(for: config)

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

    private func startPreviewUpdates(for config: VMConfig) {
        stopPreviewUpdates(for: config.id)
        let timer = Timer.scheduledTimer(withTimeInterval: 3.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                self?.capturePreview(config)
            }
        }
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

    func stop(_ id: UUID) {
        guard let process = processes[id] else { return }
        process.terminate()
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

    // The dedicated Tools CD drive (id=tools0). Starts loaded with the
    // bundled ClassicMac Tools image when the VM's settings ask for it,
    // otherwise as an empty tray the Machine menu can fill at runtime.
    private static func toolsDriveSpec(for config: VMConfig, iface: String) -> String {
        var spec = "if=\(iface),media=cdrom,id=tools0"
        if config.toolsCDInserted, let toolsCD = AppPaths.toolsCD {
            spec += ",file=\(toolsCD.path),format=raw"
        }
        return spec
    }

    // qemu-system-ppc -M mac99: a New World Power Mac that boots Mac OS 8.5
    // through 9.2.2 (and early OS X) via the bundled OpenBIOS firmware, so no
    // Apple ROM is involved. Storage is IDE, networking is the sungem NIC, and
    // via=pmu provides USB (ADB-free) input with correct mouse tracking.
    // Sound comes from the screamer (AWACS) device ported onto our QEMU build;
    // the guest driver attaches because the bundled OpenBIOS advertises the
    // davbus/awacs nodes.
    private static func buildPowerMacArguments(for config: VMConfig) -> [String] {
        var args: [String] = []

        // Folder sharing needs the classicvirtio ndrvloader to run before the
        // OS: it installs the virtio NDRVs and then continues the normal boot.
        // Tablet input also needs the ndrvloader (it loads the virtio-tablet
        // driver). The loader takes over the firmware boot command, so it is
        // skipped when booting from CD (e.g. OS installs) - sharing and tablet
        // input are simply inactive for that boot.
        let sharing = config.hasSharedFolder && !(config.bootFromCD && config.cdImagePath?.isEmpty == false)
        let tablet = config.tabletInput && !(config.bootFromCD && config.cdImagePath?.isEmpty == false)
        let needsNdrvLoader = sharing || tablet

        args += ["-M", "mac99,via=pmu,audiodev=snd0"]
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
        // Route the OpenBIOS firmware console to the (disconnected) serial
        // port so the firmware text screens never appear. Together with the
        // bundled OpenBIOS's console background being repainted black (see
        // scripts/build-qemu.sh), the display stays black until the Mac OS
        // boot screen takes over.
        args += ["-prom-env", "output-device=ttya"]
        // OpenBIOS sizes the framebuffer from -g at boot. Boot depth is
        // millions of colors; Monitors can switch anywhere from Black &
        // White up to millions once Mac OS is running (packed-lowbpp).
        args += ["-g", "\(config.width)x\(config.height)x32"]
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

        // Optional IDE CD-ROM.
        if let cdPath = config.cdImagePath, !cdPath.isEmpty {
            args += ["-drive", "file=\(cdPath),format=raw,media=cdrom"]
            if config.bootFromCD {
                args += ["-boot", "d"]
            }
        }

        // Dedicated second CD drive for the ClassicMac Tools CD. Always
        // present so the QEMU window's Machine menu can insert/eject the
        // Tools CD while the Mac is running; loaded at boot when the VM's
        // settings say so. It comes after the user's disc on the IDE chain,
        // so "-boot d" still starts up from the user's (bootable) CD even
        // with both inserted.
        args += ["-drive", toolsDriveSpec(for: config, iface: "ide")]

        // User-mode networking through the mac99 onboard sungem ethernet.
        if config.networking {
            args += ["-nic", "user,model=sungem"]
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

        // The nubus-virtio-mmio card is needed for both folder sharing and
        // tablet input (both are classicvirtio features driven by the declrom).
        let needsVirtioCard = sharing || tablet

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

        // Optional CD-ROM at SCSI ID 3.
        if let cdPath = config.cdImagePath, !cdPath.isEmpty {
            args += ["-device", "scsi-cd,scsi-id=3,drive=cd0"]
            args += ["-drive", "file=\(cdPath),media=cdrom,if=none,id=cd0"]
            if config.bootFromCD {
                args += ["-boot", "d"]
            }
        }

        // Dedicated second CD drive (SCSI ID 4) for the ClassicMac Tools CD;
        // see the Power Mac builder for the rationale.
        args += ["-device", "scsi-cd,scsi-id=4,drive=tools0"]
        args += ["-drive", toolsDriveSpec(for: config, iface: "none")]

        // User-mode networking through the Quadra's built-in SONIC ethernet.
        // The q800 machine creates the onboard dp8393x and binds it to nd_table[0],
        // so networking is configured with -nic (not a separate -device).
        if config.networking {
            args += ["-nic", "user,model=dp83932"]
        }

        // Shared folder via the classicvirtio NuBus virtio transport. The
        // nubus-virtio-mmio card must be created before the nubus-qfb framebuffer
        // so it lands in the earlier NuBus slot. The same card also provides
        // tablet input when enabled.
        if needsVirtioCard {
            args += ["-device", "nubus-virtio-mmio,romfile=\(AppPaths.declROM.path)"]
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
