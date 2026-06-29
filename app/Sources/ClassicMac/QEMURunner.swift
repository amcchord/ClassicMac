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

// Launches and tracks emulator processes, and builds qemu-img / qemu-system-m68k
// command lines from a VMConfig.
@MainActor
final class QEMUManager: ObservableObject {
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
        AppPaths.vmDir(for: id).appendingPathComponent("monitor.sock")
    }

    func start(_ config: VMConfig) {
        if runningIDs.contains(config.id) {
            return
        }
        guard AppPaths.qemuIsAvailable else {
            lastError = "QEMU is not bundled yet. Run scripts/build-qemu.sh and scripts/bundle-qemu.sh."
            return
        }
        guard AppPaths.romIsAvailable else {
            lastError = "The Quadra 800 ROM is missing from the application bundle."
            return
        }

        // Remove any stale monitor socket so QEMU can bind a fresh one.
        try? FileManager.default.removeItem(at: QEMUManager.monitorSocketURL(for: config.id))

        let process = Process()
        process.executableURL = AppPaths.qemuSystemBinary
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

    // MARK: Argument construction

    static func buildArguments(for config: VMConfig) -> [String] {
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
        args += ["-display", "cocoa,swap-opt-cmd=on"]
        // -g only applies to machine-created framebuffers; when the qfb is added as
        // a device its size is set via device options instead.
        if !qfbAsDevice {
            args += ["-g", "\(config.width)x\(config.height)x\(config.depth)"]
        }
        args += ["-name", config.name]

        // Audio: a real backend hums constantly on the emulated ASC, so default
        // to a silent (null) backend unless the user opts into sound.
        if config.sound {
            args += ["-audiodev", "coreaudio,id=snd0"]
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
