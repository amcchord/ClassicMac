import CryptoKit
import Foundation

/// Composite/monitor video output mode exposed to Apple / Bandai Pippin (`-M pippin,video-out=...`).
enum PippinVideoOut: String, Codable, CaseIterable, Identifiable {
    case ntsc
    case pal
    case vga

    var id: String { rawValue }

    var label: String {
        switch self {
        case .ntsc: return "NTSC (640 × 480)"
        case .pal:  return "PAL (640 × 512)"
        case .vga:  return "VGA (640 × 480)"
        }
    }

    var shortLabel: String {
        switch self {
        case .ntsc: return "NTSC"
        case .pal:  return "PAL"
        case .vga:  return "VGA"
        }
    }

    var dimensions: (width: Int, height: Int) {
        switch self {
        case .ntsc, .vga: return (640, 480)
        case .pal:        return (640, 512)
        }
    }
}

enum PippinError: LocalizedError {
    case missingROM
    case invalidROM(String)
    case invalidDiscImage(String)
    case downloadFailed(String)

    var errorDescription: String? {
        switch self {
        case .missingROM:
            return "Pippin requires a 4 MB Kinka boot ROM. Choose the ROM file in Get Info, download it from the Internet Archive, or place pippin.rom in ClassicMac's ROMs folder."
        case .invalidROM(let reason):
            return reason
        case .invalidDiscImage(let reason):
            return reason
        case .downloadFailed(let reason):
            return reason
        }
    }
}

/// Hardware helpers, ROM checks, persistence setup, raw CD image conversion,
/// and QEMU command-line generation for Apple / Bandai Pippin (`-M pippin`).
enum PippinMachine {
    static let romByteCount = 4_194_304
    static let flashByteCount = 131_072
    static let nvramByteCount = 8_192
    static let pramByteCount = 256

    static let defaultRAMMB = 13
    static let minRAMMB = 5
    static let maxRAMMB = 16
    static let ramPresets = [5, 6, 8, 13, 16]

    static let kinka13SHA256 = "4e13dbbc5d9cc3e2169e52257a876bc60788fba262b7462347b3e7340b527365"
    static let kinka13SHA1 = "8e512af6e34dd823f3defec77d43ecbff1ecad54"

    static let archiveOrgROMURLs: [URL] = [
        URL(string: "https://archive.org/download/mame-0.221-roms-merged/pippin.zip/bandai%20pippin%2C19960920%20-%20kinka%201.3%2C-%203e6b3ee4-a52528e9ce8c.rom")!,
        URL(string: "https://archive.org/download/mame-0.205-roms-merged/pippin.zip/bandai%20pippin%2C19960920%20-%20kinka%201.3%2C-%203e6b3ee4-a52528e9ce8c.rom")!,
    ]

    /// Known Kinka boot ROM SHA-1 digests -> human-readable identification.
    static let knownROMSHA1: [String: String] = [
        "8e512af6e34dd823f3defec77d43ecbff1ecad54": "Pippin Kinka 1.3 (4 MB)",
        "6478ee9e8d5cfe3b00f699f7072482210e00c5cc": "Pippin Kinka 1.0 (4 MB)",
        "eb8739cab1807c6c7c51acc7f4a3afc1f9c6ddbb": "Pippin Kinka Dev (4 MB)",
    ]

    static let scsiCDDeviceSpec =
        "scsi-cd,bus=mesh-scsi.0,scsi-id=3,drive=cd0," +
        "vendor=SONY    ,product=CD-ROM CDU-8003A,ver=1.9a," +
        "quirk_mode_page_apple_vendor=on," +
        "quirk_mode_sense_rom_use_dbd=on," +
        "quirk_mode_page_vendor_specific_apple=on," +
        "quirk_mode_page_truncated=on"

    private static let rawSyncHeader: [UInt8] = [
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    ]

    static func romSHA1(data: Data) -> String {
        Insecure.SHA1.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    static func romSHA256(data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    /// Validates a 4 MiB Pippin boot ROM and returns a display label.
    @discardableResult
    static func validateROM(at url: URL) throws -> String {
        let fm = FileManager.default
        guard fm.fileExists(atPath: url.path) else {
            throw PippinError.missingROM
        }
        guard let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey]),
              values.isRegularFile == true else {
            throw PippinError.invalidROM("The selected Pippin ROM is not a readable file.")
        }
        guard values.fileSize == romByteCount else {
            throw PippinError.invalidROM(
                "Pippin boot ROMs must be exactly 4 MB (\(romByteCount) bytes)."
            )
        }
        let data = try Data(contentsOf: url, options: .mappedIfSafe)
        let digest = romSHA1(data: data)
        return knownROMSHA1[digest] ?? "Pippin Boot ROM (4 MB)"
    }

    /// Searches explicit config path, bundle-local ROM filenames, and shared Application Support ROMs.
    static func resolveROMURL(for config: VMConfig) -> URL? {
        let fm = FileManager.default
        if let explicit = config.pippinROMPath?.trimmingCharacters(in: .whitespacesAndNewlines),
           !explicit.isEmpty,
           fm.isReadableFile(atPath: explicit) {
            return URL(fileURLWithPath: explicit)
        }
        for name in ["pippin.rom", "kinka13.rom", "bootrom.bin"] {
            let local = config.folder.appendingPathComponent(name)
            if fm.isReadableFile(atPath: local.path) {
                return local
            }
        }
        let shared = AppPaths.pippinROM
        if fm.isReadableFile(atPath: shared.path) {
            return shared
        }
        return nil
    }

    /// Creates missing flash.img (128 KiB 0xFF), nvram.img (8 KiB 0x00), and pram.img (256 B 0x00).
    static func ensurePersistenceImages(for config: VMConfig) throws {
        let fm = FileManager.default
        try fm.createDirectory(at: config.folder, withIntermediateDirectories: true)

        if !fm.fileExists(atPath: config.flashImageURL.path) {
            let blankFlash = Data(repeating: 0xFF, count: flashByteCount)
            try blankFlash.write(to: config.flashImageURL, options: .atomic)
        }
        if !fm.fileExists(atPath: config.nvramImageURL.path) {
            let blankNVRAM = Data(repeating: 0x00, count: nvramByteCount)
            try blankNVRAM.write(to: config.nvramImageURL, options: .atomic)
        }
        if !fm.fileExists(atPath: config.pramImageURL.path) {
            let blankPRAM = Data(repeating: 0x00, count: pramByteCount)
            try blankPRAM.write(to: config.pramImageURL, options: .atomic)
        }
    }

    /// Returns true when the file starts with a 2352-byte raw CD-ROM sync pattern and its size is a multiple of 2352.
    static func isRaw2352CDImage(at url: URL) -> Bool {
        guard let values = try? url.resourceValues(forKeys: [.fileSizeKey]),
              let size = values.fileSize,
              size >= 2352,
              size % 2352 == 0,
              let handle = try? FileHandle(forReadingFrom: url) else {
            return false
        }
        defer { try? handle.close() }
        guard let header = try? handle.read(upToCount: rawSyncHeader.count),
              header.count == rawSyncHeader.count else {
            return false
        }
        return Array(header) == rawSyncHeader
    }

    /// Resolves a `.cue` descriptor to its referenced `.bin` image path in the same folder.
    static func resolveBinURL(fromCueURL cueURL: URL) throws -> URL {
        let text = try String(contentsOf: cueURL, encoding: .utf8)
        for line in text.components(separatedBy: .newlines) {
            let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
            guard trimmed.uppercased().hasPrefix("FILE ") else { continue }
            if let firstQuote = trimmed.firstIndex(of: "\""),
               let lastQuote = trimmed[trimmed.index(after: firstQuote)...].firstIndex(of: "\"") {
                let filename = String(trimmed[trimmed.index(after: firstQuote)..<lastQuote])
                let candidate = cueURL.deletingLastPathComponent().appendingPathComponent(filename)
                if FileManager.default.isReadableFile(atPath: candidate.path) {
                    return candidate
                }
            }
        }
        throw PippinError.invalidDiscImage("Could not locate the referenced .bin data track for \(cueURL.lastPathComponent).")
    }

    /// Converts a raw 2352-byte Mode 1/2 `.bin` (or `.cue`) disc image into a standard 2048-byte `.iso`.
    /// Already-2048-byte images are copied straight to `destinationURL`.
    static func convertRawBinToISO(from sourceURL: URL, to destinationURL: URL) throws {
        let inputURL = sourceURL.pathExtension.lowercased() == "cue"
            ? try resolveBinURL(fromCueURL: sourceURL)
            : sourceURL

        let fm = FileManager.default
        try fm.createDirectory(at: destinationURL.deletingLastPathComponent(), withIntermediateDirectories: true)

        if !isRaw2352CDImage(at: inputURL) {
            guard let values = try? inputURL.resourceValues(forKeys: [.fileSizeKey]),
                  let size = values.fileSize, size >= 2048, size % 2048 == 0 else {
                throw PippinError.invalidDiscImage("Unsupported disc image sector format for \(inputURL.lastPathComponent).")
            }
            if fm.fileExists(atPath: destinationURL.path) {
                try fm.removeItem(at: destinationURL)
            }
            try fm.copyItem(at: inputURL, to: destinationURL)
            return
        }

        let reader = try FileHandle(forReadingFrom: inputURL)
        defer { try? reader.close() }

        let tempURL = destinationURL.deletingLastPathComponent()
            .appendingPathComponent(".\(destinationURL.lastPathComponent).tmp.\(UUID().uuidString)")
        _ = fm.createFile(atPath: tempURL.path, contents: nil)
        defer { try? fm.removeItem(at: tempURL) }

        let writer = try FileHandle(forWritingTo: tempURL)
        var writerOpen = true
        defer { if writerOpen { try? writer.close() } }

        let batchSectors = 256
        let rawChunkBytes = batchSectors * 2352
        var sectorMode: UInt8?

        while true {
            let chunk = try reader.read(upToCount: rawChunkBytes) ?? Data()
            if chunk.isEmpty { break }
            let sectorCount = chunk.count / 2352
            guard sectorCount * 2352 == chunk.count else {
                throw PippinError.invalidDiscImage("Truncated 2352-byte sector in \(inputURL.lastPathComponent).")
            }
            if sectorMode == nil {
                sectorMode = chunk[chunk.startIndex + 15]
            }
            let payloadOffset = (sectorMode == 2) ? 24 : 16
            var outData = Data()
            outData.reserveCapacity(sectorCount * 2048)
            chunk.withUnsafeBytes { rawBuf in
                guard let base = rawBuf.baseAddress else { return }
                for idx in 0..<sectorCount {
                    outData.append(base.advanced(by: idx * 2352 + payloadOffset)
                        .assumingMemoryBound(to: UInt8.self), count: 2048)
                }
            }
            try writer.write(contentsOf: outData)
        }

        try writer.close()
        writerOpen = false
        if fm.fileExists(atPath: destinationURL.path) {
            try fm.removeItem(at: destinationURL)
        }
        try fm.moveItem(at: tempURL, to: destinationURL)
    }

    static func qemuEscape(_ value: String) -> String {
        value.replacingOccurrences(of: ",", with: ",,")
    }

    static func arguments(
        for config: VMConfig,
        romURL: URL,
        displayArguments: [String],
        monitorSocketURL: URL,
        qmpSocketURL: URL
    ) -> [String] {
        var args: [String] = []
        args += ["-accel", "tcg,tb-size=512"]
        args += ["-M", "pippin,video-out=\(config.effectivePippinVideoOut.rawValue),audiodev=snd0"]
        args += ["-m", String(config.ramMB)]
        args += ["-bios", romURL.path]
        args += displayArguments
        args += ["-name", qemuEscape(config.name)]
        if config.sound {
            args += ["-audiodev", "coreaudio,id=snd0,out.buffer-length=50000"]
        } else {
            args += ["-audiodev", "none,id=snd0"]
        }
        args += ["-monitor", "unix:\(qemuEscape(monitorSocketURL.path)),server=on,wait=off"]
        args += ["-action", "reboot=shutdown"]
        args += ["-qmp", "unix:\(qemuEscape(qmpSocketURL.path)),server=on,wait=off"]
        args += ["-drive", "file=\(qemuEscape(config.flashImageURL.path)),if=pflash,format=raw"]
        args += ["-drive", "file=\(qemuEscape(config.nvramImageURL.path)),if=mtd,index=0,format=raw"]
        args += ["-drive", "file=\(qemuEscape(config.pramImageURL.path)),if=mtd,index=1,format=raw"]

        if let cdPath = config.cdImagePath?.trimmingCharacters(in: .whitespacesAndNewlines),
           !cdPath.isEmpty {
            args += ["-drive", "file=\(qemuEscape(cdPath)),if=none,id=cd0,media=cdrom,readonly=on,format=raw"]
        } else {
            args += ["-drive", "if=none,id=cd0,media=cdrom,readonly=on"]
        }
        args += ["-device", scsiCDDeviceSpec]
        return args
    }
}

/// Downloads and cryptographically verifies the Apple / Bandai Pippin Kinka 1.3 boot ROM
/// from the Internet Archive (or an explicit `CLASSICMAC_PIPPIN_ROM_URL` HTTPS override).
enum PippinROMDownloader {
    enum DownloadPhase: Equatable, Sendable {
        case connecting
        case downloading(received: Int64, total: Int64)
        case verifying
    }

    private final class HTTPSRedirectDelegate: NSObject, URLSessionTaskDelegate {
        func urlSession(
            _ session: URLSession,
            task: URLSessionTask,
            willPerformHTTPRedirection response: HTTPURLResponse,
            newRequest request: URLRequest,
            completionHandler: @escaping (URLRequest?) -> Void
        ) {
            guard request.url?.scheme?.lowercased() == "https" else {
                completionHandler(nil)
                return
            }
            completionHandler(request)
        }
    }

    static func resolvedMirrors() -> [URL] {
        if let raw = ProcessInfo.processInfo.environment["CLASSICMAC_PIPPIN_ROM_URL"]?
            .trimmingCharacters(in: .whitespacesAndNewlines),
           !raw.isEmpty,
           let url = URL(string: raw),
           url.scheme?.lowercased() == "https",
           url.host != nil {
            return [url]
        }
        return PippinMachine.archiveOrgROMURLs
    }

    @discardableResult
    static func downloadKinka13(
        to destination: URL = AppPaths.pippinROM,
        mirrors: [URL]? = nil,
        expectedSHA256: String = PippinMachine.kinka13SHA256,
        session: URLSession? = nil,
        onProgress: @escaping @Sendable (DownloadPhase) -> Void = { _ in }
    ) async throws -> URL {
        let targets = mirrors ?? resolvedMirrors()
        guard !targets.isEmpty else {
            throw PippinError.downloadFailed("No Pippin boot ROM download URL is configured.")
        }

        let ownedSession: URLSession?
        let activeSession: URLSession
        if let session {
            ownedSession = nil
            activeSession = session
        } else {
            let config = URLSessionConfiguration.ephemeral
            config.timeoutIntervalForRequest = 30
            config.timeoutIntervalForResource = 180
            let created = URLSession(configuration: config)
            ownedSession = created
            activeSession = created
        }
        defer { ownedSession?.invalidateAndCancel() }

        let expectedDigest = expectedSHA256.lowercased()
        let fm = FileManager.default
        let destDir = destination.deletingLastPathComponent()
        try fm.createDirectory(at: destDir, withIntermediateDirectories: true)

        var lastError = "Could not reach archive.org."
        for url in targets {
            try Task.checkCancellation()
            guard url.scheme?.lowercased() == "https", url.host != nil else {
                lastError = "Pippin ROM downloads require an HTTPS URL."
                continue
            }

            onProgress(.connecting)
            let delegate = HTTPSRedirectDelegate()
            var request = URLRequest(url: url)
            request.setValue("ClassicMac/5.0.1", forHTTPHeaderField: "User-Agent")

            let tempURL = destDir.appendingPathComponent(".pippin.rom.part.\(UUID().uuidString)")
            defer { try? fm.removeItem(at: tempURL) }

            do {
                let (bytes, response) = try await activeSession.bytes(for: request, delegate: delegate)
                guard let http = response as? HTTPURLResponse else {
                    lastError = "Unexpected server response from \(url.host ?? "archive.org")."
                    continue
                }
                guard http.statusCode == 200 else {
                    lastError = "The server returned HTTP \(http.statusCode)."
                    continue
                }
                let expectedLength = Int64(PippinMachine.romByteCount)
                if http.expectedContentLength > 0 && http.expectedContentLength != expectedLength {
                    lastError = "Unexpected ROM size (\(http.expectedContentLength) bytes)."
                    continue
                }

                _ = fm.createFile(atPath: tempURL.path, contents: nil)
                let handle = try FileHandle(forWritingTo: tempURL)
                var handleClosed = false
                defer { if !handleClosed { try? handle.close() } }

                var hasher = SHA256()
                var buffer = [UInt8]()
                buffer.reserveCapacity(65_536)
                var received: Int64 = 0
                var lastReported: Int64 = 0
                onProgress(.downloading(received: 0, total: expectedLength))

                for try await byte in bytes {
                    try Task.checkCancellation()
                    received += 1
                    if received > expectedLength {
                        throw PippinError.downloadFailed("Downloaded file exceeded 4 MB (\(PippinMachine.romByteCount) bytes).")
                    }
                    buffer.append(byte)
                    if buffer.count >= 65_536 {
                        let chunk = Data(buffer)
                        buffer.removeAll(keepingCapacity: true)
                        hasher.update(data: chunk)
                        try handle.write(contentsOf: chunk)
                        if received - lastReported >= 65_536 {
                            lastReported = received
                            onProgress(.downloading(received: received, total: expectedLength))
                        }
                    }
                }
                if !buffer.isEmpty {
                    let chunk = Data(buffer)
                    hasher.update(data: chunk)
                    try handle.write(contentsOf: chunk)
                }

                try handle.close()
                handleClosed = true

                guard received == expectedLength else {
                    lastError = "Incomplete ROM download (\(received) of \(expectedLength) bytes)."
                    continue
                }

                onProgress(.verifying)
                let digest = hasher.finalize().map { String(format: "%02x", $0) }.joined()
                guard digest == expectedDigest else {
                    lastError = "Downloaded ROM failed SHA-256 verification."
                    continue
                }

                if fm.fileExists(atPath: destination.path) {
                    _ = try fm.replaceItemAt(destination, withItemAt: tempURL)
                } else {
                    try fm.moveItem(at: tempURL, to: destination)
                }
                return destination
            } catch is CancellationError {
                throw CancellationError()
            } catch let error as PippinError {
                lastError = error.localizedDescription
            } catch {
                lastError = error.localizedDescription
            }
        }

        throw PippinError.downloadFailed("Could not download Pippin Kinka 1.3 ROM: \(lastError)")
    }
}
