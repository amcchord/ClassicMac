import Foundation
import SwiftUI

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
    @Published var lastError: String?

    private var processes: [UUID: Process] = [:]

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

    func start(_ config: VMConfig) {
        if runningIDs.contains(config.id) {
            return
        }
        guard AppPaths.qemuIsAvailable(for: config.machineFamily) else {
            lastError = "QEMU is not bundled yet. Run scripts/build-qemu.sh and scripts/bundle-qemu.sh."
            return
        }
        if let preflightError = QEMUManager.preflight(config) {
            lastError = preflightError
            return
        }

        // Remove any stale monitor socket so QEMU can bind a fresh one.
        try? FileManager.default.removeItem(at: QEMUManager.monitorSocketURL(for: config.id))

        let process = Process()
        process.executableURL = AppPaths.qemuBinary(for: config.machineFamily)
        process.arguments = QEMUManager.buildArguments(for: config)
        process.currentDirectoryURL = config.folder

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
                self.runningIDs.remove(config.id)
                self.pausedIDs.remove(config.id)
                self.processes.removeValue(forKey: config.id)
                if proc.terminationStatus != 0 && proc.terminationReason == .exit {
                    self.lastError = "Emulator exited unexpectedly.\n\n\(message)"
                }
            }
        }

        do {
            try process.run()
        } catch {
            lastError = "Could not launch QEMU: \(error.localizedDescription)"
            return
        }

        processes[config.id] = process
        runningIDs.insert(config.id)
    }

    func stop(_ id: UUID) {
        guard let process = processes[id] else { return }
        process.terminate()
    }

    func pause(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        sendMonitor("stop", to: id)
        pausedIDs.insert(id)
    }

    func resume(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        sendMonitor("cont", to: id)
        pausedIDs.remove(id)
    }

    func reboot(_ id: UUID) {
        guard runningIDs.contains(id) else { return }
        sendMonitor("system_reset", to: id)
        pausedIDs.remove(id)
    }

    private func sendMonitor(_ command: String, to id: UUID) {
        let path = QEMUManager.monitorSocketURL(for: id).path
        DispatchQueue.global(qos: .userInitiated).async {
            _ = HMPClient.send(command, socketPath: path)
        }
    }

    // MARK: Launch preflight

    // Checks everything QEMU will need before spawning it, so failures surface
    // as clear messages instead of a cryptic emulator exit. Returns nil when
    // the machine is ready to boot. Also repairs what it safely can (a deleted
    // Quadra PRAM is recreated from the seed).
    private static func preflight(_ config: VMConfig) -> String? {
        let fm = FileManager.default

        let missing = AppPaths.missingFirmware(for: config.machineFamily)
        if !missing.isEmpty {
            return "The \(config.machineFamily.label) firmware is missing from the application bundle: \(missing.joined(separator: ", ")). Re-run scripts/build-qemu.sh and scripts/bundle-qemu.sh."
        }

        guard fm.fileExists(atPath: config.diskImageURL.path) else {
            return "The machine's hard disk image (\(config.diskImageName)) is missing from \u{201C}\(config.folder.lastPathComponent)\u{201D}. If you moved or edited the machine file, restore its disk image."
        }

        if config.machineFamily.usesPRAMImage && !fm.fileExists(atPath: config.pramImageURL.path) {
            // A PRAM is tiny and recreatable; restore it rather than failing.
            if fm.fileExists(atPath: AppPaths.pramSeed.path) {
                try? fm.copyItem(at: AppPaths.pramSeed, to: config.pramImageURL)
            }
            if !fm.fileExists(atPath: config.pramImageURL.path) {
                return "The machine's PRAM image is missing and could not be recreated."
            }
        }

        if let cdPath = config.cdImagePath, !cdPath.isEmpty, !fm.fileExists(atPath: cdPath) {
            return "The CD image \u{201C}\(URL(fileURLWithPath: cdPath).lastPathComponent)\u{201D} could not be found. It may have been moved or deleted - eject it in the machine's settings or choose it again."
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
        // The loader takes over the firmware boot command, so it is skipped
        // when booting from CD (e.g. OS installs) - sharing is simply inactive
        // for that boot.
        let sharing = config.hasSharedFolder && !(config.bootFromCD && config.cdImagePath?.isEmpty == false)

        args += ["-M", "mac99,via=pmu,audiodev=snd0"]
        args += ["-m", String(config.ramMB)]
        args += ["-L", AppPaths.pcBiosDir.path]
        args += ["-display", "cocoa,swap-opt-cmd=off"]
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
        // Route the OpenBIOS firmware console to the (disconnected) serial
        // port so the yellow firmware text screens never appear; the display
        // stays blank until the Mac OS boot screen takes over.
        args += ["-prom-env", "output-device=ttya"]
        // OpenBIOS sizes the framebuffer from -g at boot. Depth is fixed at
        // millions of colors; Mac OS 9 can still switch lower in Monitors.
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

        // Main IDE hard disk.
        args += ["-drive", "file=\(config.diskImageURL.path),format=raw,media=disk"]

        // Optional IDE CD-ROM.
        if let cdPath = config.cdImagePath, !cdPath.isEmpty {
            args += ["-drive", "file=\(cdPath),format=raw,media=cdrom"]
            if config.bootFromCD {
                args += ["-boot", "d"]
            }
        }

        // User-mode networking through the mac99 onboard sungem ethernet.
        if config.networking {
            args += ["-nic", "user,model=sungem"]
        }

        // Shared folder via virtio-9p-pci and the classicvirtio ndrvloader.
        // The loader is placed in guest RAM by QEMU's generic loader device and
        // executed by OpenBIOS in place of the default boot command.
        if sharing {
            args += ["-device", "loader,addr=0x4000000,file=\(AppPaths.ndrvLoader.path)"]
            args += ["-prom-env", "boot-command=init-program go"]
            args += ["-device", "virtio-9p-pci,fsdev=share0,mount_tag=\(config.sharedVolumeName)"]
            let escapedPath = config.sharedFolderPath!.replacingOccurrences(of: ",", with: ",,")
            args += ["-fsdev", "local,id=share0,security_model=none,path=\(escapedPath)"]
        }

        return args
    }

    private static func buildQuadraArguments(for config: VMConfig) -> [String] {
        var args: [String] = []

        let sharing = config.hasSharedFolder

        // Framebuffer selection.
        // - No sharing + enhanced: machine creates the qfb (fb=qemu) and -g applies.
        // - Sharing + enhanced: the virtio card must precede the framebuffer card,
        //   so the machine creates no framebuffer (fb=none) and we add nubus-qfb as
        //   a -device after nubus-virtio-mmio (its size comes from device options).
        // - Not enhanced: leave the built-in macfb (fb=mac default) and -g applies.
        var machine = "q800"
        let qfbAsDevice = sharing && config.useEnhancedFramebuffer
        if config.useEnhancedFramebuffer {
            if sharing {
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
        args += ["-display", "cocoa,swap-opt-cmd=off"]
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

        // User-mode networking through the Quadra's built-in SONIC ethernet.
        // The q800 machine creates the onboard dp8393x and binds it to nd_table[0],
        // so networking is configured with -nic (not a separate -device).
        if config.networking {
            args += ["-nic", "user,model=dp83932"]
        }

        // Shared folder via the classicvirtio NuBus virtio transport. The
        // nubus-virtio-mmio card must be created before the nubus-qfb framebuffer
        // so it lands in the earlier NuBus slot.
        if sharing {
            args += ["-device", "nubus-virtio-mmio,romfile=\(AppPaths.declROM.path)"]
            if qfbAsDevice {
                args += ["-device", "nubus-qfb,width=\(config.width),height=\(config.height),depth=\(config.depth)"]
            }
            args += ["-device", "virtio-9p-device,fsdev=share0,mount_tag=\(config.sharedVolumeName)"]
            let escapedPath = config.sharedFolderPath!.replacingOccurrences(of: ",", with: ",,")
            args += ["-fsdev", "local,id=share0,security_model=none,path=\(escapedPath)"]
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
            return .failure("qemu-img is not bundled yet. Build QEMU first.")
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
