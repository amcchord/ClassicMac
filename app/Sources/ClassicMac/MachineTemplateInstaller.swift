import Foundation
import Darwin

enum MachineTemplateInstaller {
    static let spaceReserve: Int64 = 256 * 1_048_576

    static func checkSpace(at directory: URL, requiredBytes: Int64) throws {
        let values = try directory.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
        let available = try values.volumeAvailableCapacityForImportantUsage ??
            ((FileManager.default.attributesOfFileSystem(forPath: directory.path)[.systemFreeSize]) as? NSNumber)?.int64Value
        guard let available, available >= requiredBytes else {
            throw MachineDownloadError.insufficientSpace(requiredBytes)
        }
    }

    static func normalizedName(_ name: String) throws -> String {
        let value = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !value.isEmpty, value.count <= 100,
              !value.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains) else {
            throw MachineDownloadError.invalidName
        }
        return value
    }

    // The caller verifies the archive before entering this routine. Everything
    // is unpacked in a new private staging directory on the destination volume;
    // the final package becomes visible only after all checks and rewrites pass.
    static func installVerifiedArchive(_ archive: URL, machine: DownloadableMachine,
                                       name: String, in directory: URL,
                                       cancelled: () -> Bool = { false },
                                       progress: (Int64) -> Void = { _ in }) throws -> URL {
        try machine.validate()
        let name = try normalizedName(name)
        let fm = FileManager.default
        let directory = directory.resolvingSymlinksInPath()
        guard try directory.resourceValues(forKeys: [.isDirectoryKey]).isDirectory == true else {
            throw MachineDownloadError.unsafeArchive("Choose an existing folder for the machine.")
        }
        try checkSpace(at: directory, requiredBytes: machine.installedBytes + spaceReserve)
        let staging = directory.appendingPathComponent(".classicmac-import-\(UUID().uuidString)", isDirectory: true)
        try fm.createDirectory(at: staging, withIntermediateDirectories: false,
                               attributes: [.posixPermissions: 0o700])
        defer { try? fm.removeItem(at: staging) }
        try extract(archive, into: staging, expectedBytes: machine.installedBytes,
                    cancelled: cancelled, progress: progress)
        if cancelled() { throw CancellationError() }

        let configURL = staging.appendingPathComponent("config.json")
        let source: VMConfig
        do { source = try JSONDecoder().decode(VMConfig.self, from: Data(contentsOf: configURL)) }
        catch { throw MachineDownloadError.unsafeArchive("The machine's settings are damaged.") }
        guard source.machineFamily == .powerMacG4,
              source.diskImageName == "disk.img",
              source.pramImageName == "pram.img" else {
            throw MachineDownloadError.unsafeArchive("This template is not a supported Power Mac machine.")
        }
        // Build a new config from supported hardware fields. Template paths,
        // IDs, mounted discs, browser mode and future unknown fields cannot be
        // carried into a new user's machine.
        guard let diskBytes = try staging.appendingPathComponent("disk.img").resourceValues(forKeys: [.fileSizeKey]).fileSize else {
            throw invalidArchive()
        }
        let diskSizeGB = max(1, Int((Int64(diskBytes) + 1_073_741_823) / 1_073_741_824))
        let fresh = VMConfig(
            name: name, machineFamily: .powerMacG4, ramMB: source.ramMB,
            diskSizeGB: diskSizeGB, width: source.width, height: source.height,
            depth: source.depth, useEnhancedFramebuffer: false,
            customResolution: source.customResolution, useBrowserDisplay: false,
            bootFromCD: false, toolsCDInserted: true,
            networking: source.networking, sound: source.sound,
            useG4CPU: source.useG4CPU, tabletInput: source.tabletInput,
            classicInputHelpers: source.classicInputHelpers
        )
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(fresh).write(to: configURL, options: .atomic)
        try encoder.encode(VMTemplateMetadata(machine: machine)).write(
            to: staging.appendingPathComponent(VMTemplateMetadata.fileName), options: .atomic
        )
        if cancelled() { throw CancellationError() }

        let base = name.replacingOccurrences(of: "/", with: "-")
            .replacingOccurrences(of: ":", with: "-")
            .trimmingCharacters(in: CharacterSet(charactersIn: ". "))
        let safeBase = base.isEmpty ? "Mac OS 9" : base
        for number in 1...10_000 {
            let suffix = number == 1 ? "" : " \(number)"
            let destination = directory.appendingPathComponent("\(safeBase)\(suffix).classic", isDirectory: true)
            if fm.fileExists(atPath: destination.path) { continue }
            // RENAME_EXCL makes the existence check atomic with publication,
            // including competing imports that choose the same name.
            if renamex_np(staging.path, destination.path, UInt32(RENAME_EXCL)) == 0 {
                return destination
            }
            if errno == EEXIST { continue }
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }
        throw MachineDownloadError.unsafeArchive("There are too many machines with that name. Choose another name.")
    }

    // Deliberately narrow tar reader: gzip + flat regular files only. No shell,
    // archive-provided paths, links, extended headers, ownership, permissions,
    // devices or sparse extents are interpreted. GNU's positive base-256 size
    // encoding supports raw disks larger than USTAR's 8 GB limit.
    static func extract(_ archive: URL, into directory: URL, expectedBytes: Int64,
                        cancelled: () -> Bool = { false },
                        progress: (Int64) -> Void = { _ in }) throws {
        let pipe = Pipe()
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/gzip")
        process.arguments = ["-dc", "--", archive.path]
        process.environment = ["PATH": "/usr/bin:/bin"]
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        try process.run()
        let input = pipe.fileHandleForReading
        defer {
            try? input.close()
            if process.isRunning { process.terminate() }
            process.waitUntilExit()
        }
        var extracted: Int64 = 0
        var seen = Set<String>()
        while true {
            if cancelled() { throw CancellationError() }
            let header = try readExactly(512, from: input)
            if header.allSatisfy({ $0 == 0 }) {
                let second = try readExactly(512, from: input)
                guard second.allSatisfy({ $0 == 0 }) else { throw invalidArchive() }
                // Standard tar block padding is bounded; reject concatenated
                // archives or unbounded zero output after the end marker.
                var padding = 0
                while let chunk = try input.read(upToCount: 512), !chunk.isEmpty {
                    if cancelled() { throw CancellationError() }
                    padding += chunk.count
                    guard padding <= 10_240, chunk.allSatisfy({ $0 == 0 }) else { throw invalidArchive() }
                }
                break
            }
            let checksum = try number(header.subdata(in: 148..<156))
            let actualChecksum = header.enumerated().reduce(Int64(0)) { total, item in
                total + ((148..<156).contains(item.offset) ? 32 : Int64(item.element))
            }
            guard checksum == actualChecksum,
                  header[156] == 0 || header[156] == 48,
                  header.subdata(in: 157..<257).allSatisfy({ $0 == 0 }),
                  header.subdata(in: 345..<500).allSatisfy({ $0 == 0 }) else { throw invalidArchive() }
            let nameBytes = header.prefix(100).prefix(while: { $0 != 0 })
            guard let name = String(data: nameBytes, encoding: .utf8),
                  ["config.json", "disk.img", "preview.png"].contains(name),
                  seen.insert(name).inserted else { throw invalidArchive() }
            let size = try number(header.subdata(in: 124..<136))
            guard size > 0, size <= expectedBytes - extracted else { throw invalidArchive() }
            if name == "config.json" && size > 65_536 { throw invalidArchive() }
            if name == "preview.png" && size > 16 * 1_048_576 { throw invalidArchive() }
            if name == "disk.img" && (size < 512 || size % 512 != 0) { throw invalidArchive() }
            let destination = directory.appendingPathComponent(name)
            let fd = open(destination.path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0o600)
            guard fd >= 0 else { throw invalidArchive() }
            let output = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
            do {
                var remaining = size
                while remaining > 0 {
                    if cancelled() { throw CancellationError() }
                    let data = try readExactly(Int(min(1_048_576, remaining)), from: input)
                    if name == "disk.img" && data.allSatisfy({ $0 == 0 }) {
                        // Preserve a sparse raw disk: a prepared 8 GB template
                        // can contain only a few hundred MB of actual data.
                        try output.seek(toOffset: UInt64(size - remaining + Int64(data.count)))
                    } else {
                        try output.write(contentsOf: data)
                    }
                    remaining -= Int64(data.count)
                    extracted += Int64(data.count)
                    progress(extracted)
                }
                try output.truncate(atOffset: UInt64(size))
                try output.close()
            } catch {
                try? output.close()
                throw error
            }
            let padding = Int((512 - size % 512) % 512)
            if padding > 0 {
                guard try readExactly(padding, from: input).allSatisfy({ $0 == 0 }) else { throw invalidArchive() }
            }
        }
        process.waitUntilExit()
        guard process.terminationStatus == 0, extracted == expectedBytes,
              seen.contains("config.json"), seen.contains("disk.img") else { throw invalidArchive() }
    }

    private static func readExactly(_ count: Int, from input: FileHandle) throws -> Data {
        var result = Data()
        while result.count < count {
            guard let chunk = try input.read(upToCount: count - result.count), !chunk.isEmpty else {
                throw invalidArchive()
            }
            result.append(chunk)
        }
        return result
    }

    private static func number(_ field: Data) throws -> Int64 {
        if field.first == 0x80 {
            var value: Int64 = 0
            for byte in field.dropFirst() {
                guard value <= (Int64.max - Int64(byte)) / 256 else { throw invalidArchive() }
                value = value * 256 + Int64(byte)
            }
            return value
        }
        guard let text = String(data: field, encoding: .ascii) else { throw invalidArchive() }
        let digits = text.trimmingCharacters(in: CharacterSet(charactersIn: "\0 "))
        guard !digits.isEmpty, digits.allSatisfy({ "01234567".contains($0) }),
              let value = Int64(digits, radix: 8) else { throw invalidArchive() }
        return value
    }

    private static func invalidArchive() -> MachineDownloadError {
        .unsafeArchive("Its archive is damaged or contains unsupported files. Retry the download; if it happens again, contact the template publisher.")
    }
}

// Serializes access to a partial download across sheets and app processes.
// The file descriptor owns the advisory lock until the operation finishes.
final class MachineDownloadLease {
    private var descriptor: Int32 = -1
    init(directory: URL, digest: String) throws {
        let url = directory.appendingPathComponent(digest + ".lock")
        let opened = open(url.path, O_RDWR | O_CREAT | O_NOFOLLOW, 0o600)
        guard opened >= 0 else { throw MachineDownloadError.network("The download folder isn't writable.") }
        guard flock(opened, LOCK_EX | LOCK_NB) == 0 else {
            close(opened)
            throw MachineDownloadError.network("This machine is already downloading in another window.")
        }
        descriptor = opened
    }
    deinit {
        if descriptor >= 0 { flock(descriptor, LOCK_UN); close(descriptor) }
    }
}
