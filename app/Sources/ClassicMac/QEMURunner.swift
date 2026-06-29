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
    @Published var lastError: String?

    private var processes: [UUID: Process] = [:]

    func isRunning(_ id: UUID) -> Bool {
        runningIDs.contains(id)
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

    // MARK: Argument construction

    static func buildArguments(for config: VMConfig) -> [String] {
        var args: [String] = []

        var machine = "q800"
        if config.useEnhancedFramebuffer {
            machine += ",fb=qemu"
        }
        args += ["-M", machine]

        args += ["-m", String(config.ramMB)]
        args += ["-bios", AppPaths.quadraROM.path]
        args += ["-L", AppPaths.pcBiosDir.path]
        args += ["-display", "cocoa,swap-opt-cmd=on"]
        args += ["-g", "\(config.width)x\(config.height)x\(config.depth)"]
        args += ["-name", config.name]

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
