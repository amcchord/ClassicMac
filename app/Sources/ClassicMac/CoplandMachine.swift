import Foundation
import CryptoKit
import Darwin

// Copland's loader, ROM and NVRAM must remain a matched set. Firmware lives in
// the downloaded .classic package, never in the application's generic firmware.
enum CoplandMachine {
    static func controlPipe() -> Pipe {
        let pipe = Pipe()
        // The guest can exit between Process.isRunning and a control write.
        // Report EPIPE to the caller instead of terminating the launcher.
        _ = fcntl(pipe.fileHandleForWriting.fileDescriptor, F_SETNOSIGPIPE, 1)
        return pipe
    }

    static let romSHA256 = "098b588dbe12fdfa3d388636e431ccae69cd1c6e984801267b9b2602babbfd22"

    static func validateFirmware(in folder: URL) throws {
        let rom = folder.appendingPathComponent("bootrom.bin")
        let nvram = folder.appendingPathComponent("nvram.bin")
        guard (try? rom.resourceValues(forKeys: [.fileSizeKey]).fileSize) == 4_194_304,
              (try? nvram.resourceValues(forKeys: [.fileSizeKey]).fileSize) == 8209,
              let romData = try? Data(contentsOf: rom),
              SHA256.hash(data: romData).map({ String(format: "%02x", $0) }).joined() == romSHA256,
              let nvramData = try? Data(contentsOf: nvram),
              nvramData.prefix(17) == Data("DINGUSPPCNVRAM\0".utf8) + Data([0, 32]) else {
            throw MachineDownloadError.unsafeArchive("Copland's matched ROM or startup settings are missing or damaged. Download a fresh Copland machine.")
        }
    }

    static func arguments(for config: VMConfig) -> [String] {
        ["-b", "bootrom.bin",
         "-m", "pm7500", "--rambank1_size", "32",
         "--hdd_img", "disk.img", "-r",
         "--serial_backend", "copland", "--setenv", "output-device=ttya",
         "--setenv", "input-device=ttya", "--hold-keys", "caps-lock",
         "--hold-keys-ms", "500", "--log-verbosity", "-1"]
    }

    @MainActor static func statusURL(for id: UUID) -> URL {
        QEMUManager.monitorSocketURL(for: id).deletingPathExtension().appendingPathExtension("copland-status")
    }
}
