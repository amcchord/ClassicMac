import Foundation
import Darwin
import CryptoKit
import CMachineArchive

// A catalog's smaller storage bound is usable only on a destination that
// reports sparse-file support. Missing metadata retains the 3.0 space check.
struct MachineTemplateStoragePlan {
    let requiredBytes: Int64
    let sparseWriteLimit: Int64?

    init(machine: DownloadableMachine, supportsSparseFiles: Bool) {
        sparseWriteLimit = supportsSparseFiles ? machine.requiredStorageBytes : nil
        requiredBytes = sparseWriteLimit ?? machine.installedBytes
    }
}

enum MachineTemplateInstaller {
    static let spaceReserve: Int64 = 256 * 1_048_576
    // This is part of the schema-1 optional storage contract. The packager
    // charges every nonzero disk chunk, even a partial last chunk, at this size.
    static let sparseChunkBytes: Int64 = 1_048_576
    private static let zeroChunk = Data(repeating: 0, count: Int(sparseChunkBytes))

    private static func isZeroChunk(_ data: Data) -> Bool {
        // Most of a large raw template is empty. Compare whole buffers using
        // the system's vectorized routine instead of iterating Data byte by byte.
        guard !data.isEmpty else { return true }
        return data.withUnsafeBytes { bytes in
            zeroChunk.withUnsafeBytes { zeros in
                memcmp(bytes.baseAddress!, zeros.baseAddress!, bytes.count) == 0
            }
        }
    }

    static func supportsSparseFiles(at directory: URL) -> Bool {
        guard (try? directory.resourceValues(forKeys: [.volumeSupportsSparseFilesKey]))?
            .volumeSupportsSparseFiles == true else { return false }
        var volume = statfs()
        guard statfs(directory.path, &volume) == 0 else { return false }
        // APFS supports explicit hole punching. Other filesystems retain the
        // full-size requirement until their allocation behavior is qualified.
        let fileSystem = withUnsafeBytes(of: volume.f_fstypename) {
            String(decoding: $0.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
        return fileSystem == "apfs" && volume.f_bsize > 0 && Int64(volume.f_bsize) <= sparseChunkBytes
    }

    static func storagePlan(for machine: DownloadableMachine, at directory: URL) -> MachineTemplateStoragePlan {
        MachineTemplateStoragePlan(machine: machine, supportsSparseFiles: supportsSparseFiles(at: directory))
    }

    static func storageCharge(for bytes: Int64) -> Int64 {
        ((bytes + sparseChunkBytes - 1) / sparseChunkBytes) * sparseChunkBytes
    }

    private static func allocatedBytes(_ descriptor: Int32) throws -> Int64 {
        var info = stat()
        guard fstat(descriptor, &info) == 0, info.st_blocks >= 0 else { throw invalidArchive() }
        return Int64(info.st_blocks) * 512
    }

    private static func checkAllocatedStorage(in directory: URL, maximumBytes: Int64) throws {
        var total: Int64 = 0
        for name in ["disk.img", "config.json", "preview.png", "bootrom.bin", "nvram.bin", "nvram.img", "Quadra800.rom", "pram.img", VMTemplateMetadata.fileName] {
            let path = directory.appendingPathComponent(name)
            var info = stat()
            if lstat(path.path, &info) != 0 {
                if errno == ENOENT { continue }
                throw invalidArchive()
            }
            guard info.st_blocks >= 0 else { throw invalidArchive() }
            total += Int64(info.st_blocks) * 512
            guard total <= maximumBytes else { throw storageBudgetExceeded() }
        }
    }

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
        guard !value.isEmpty, value.count <= 100, !value.hasPrefix("."),
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
        let plan = storagePlan(for: machine, at: directory)
        try checkSpace(at: directory, requiredBytes: plan.requiredBytes + spaceReserve)
        let staging = directory.appendingPathComponent(".classicmac-import-\(UUID().uuidString)", isDirectory: true)
        try fm.createDirectory(at: staging, withIntermediateDirectories: false,
                               attributes: [.posixPermissions: 0o700])
        defer { try? fm.removeItem(at: staging) }
        try extract(archive, into: staging, expectedBytes: machine.installedBytes,
                    expectedDiskBytes: machine.diskCapacityBytes, maximumStorageBytes: plan.sparseWriteLimit,
                    copland: machine.family == .powerMac7500, quadra: machine.family == .quadra800,
                    osx: machine.family == .powerMacOSX,
                    cancelled: cancelled, progress: progress)
        if cancelled() { throw CancellationError() }

        let configURL = staging.appendingPathComponent("config.json")
        let configData = try Data(contentsOf: configURL)
        let source: VMConfig
        let decoder = JSONDecoder()
        decoder.userInfo[VMConfig.importingTemplate] = true
        do { source = try decoder.decode(VMConfig.self, from: configData) }
        catch { throw MachineDownloadError.unsafeArchive("The machine's settings are damaged.") }
        let templateRecommended = (try? JSONDecoder().decode(TemplateRecommendedSettingsEnvelope.self, from: configData))?.recommendedSettings
        let recommended = mergedRecommendedSettings(template: templateRecommended, catalog: machine.recommendedSettings)
        guard source.machineFamily == machine.family,
              source.diskImageName == "disk.img",
              source.pramImageName == "pram.img" else {
            throw MachineDownloadError.unsafeArchive("This template does not match its catalog machine.")
        }
        if source.machineFamily == .powerMac7500 { try CoplandMachine.validateFirmware(in: staging) }
        if source.machineFamily == .quadra800 {
            let rom = staging.appendingPathComponent("Quadra800.rom")
            if fm.fileExists(atPath: rom.path) {
                let bytes = try Data(contentsOf: rom)
                let hash = SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
                guard bytes.count == 1_048_576,
                      hash == "05ad753fb594e656cf078023ec189e09e2a7655a780de993b75b8c51ed6b09ca" else {
                    throw MachineDownloadError.unsafeArchive("The Quadra ROM does not match this machine.")
                }
            }
        }
        // Build a new config from supported hardware fields. Template paths,
        // IDs, mounted discs, browser mode and future unknown fields cannot be
        // carried into a new user's machine.
        guard let diskBytes = try staging.appendingPathComponent("disk.img").resourceValues(forKeys: [.fileSizeKey]).fileSize else {
            throw invalidArchive()
        }
        let diskSizeGB = max(1, Int((Int64(diskBytes) + 1_073_741_823) / 1_073_741_824))
        var fresh = VMConfig(
            name: name, machineFamily: source.machineFamily, ramMB: source.ramMB,
            diskSizeGB: diskSizeGB, width: source.width, height: source.height,
            depth: source.depth, useEnhancedFramebuffer: false,
            customResolution: source.customResolution, useBrowserDisplay: false,
            bootFromCD: false, toolsCDInserted: source.toolsCDInserted,
            networking: source.networking, sound: source.sound,
            useG4CPU: source.useG4CPU, useVirtioDisk: source.useVirtioDisk,
            graphicsCard: source.graphicsCard, tabletInput: source.tabletInput,
            classicInputHelpers: source.classicInputHelpers
        )
        if let recommended {
            applyRecommendedSettings(recommended, to: &fresh)
        }
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(fresh).write(to: configURL, options: .atomic)
        try encoder.encode(VMTemplateMetadata(machine: machine)).write(
            to: staging.appendingPathComponent(VMTemplateMetadata.fileName), options: .atomic
        )
        if let limit = plan.sparseWriteLimit {
            try checkAllocatedStorage(in: staging, maximumBytes: limit)
        }
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
                VMCheckpointStore.createFreshInstallIfSupported(in: destination)
                return destination
            }
            if errno == EEXIST { continue }
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }
        throw MachineDownloadError.unsafeArchive("There are too many machines with that name. Choose another name.")
    }

    private struct TemplateRecommendedSettingsEnvelope: Decodable {
        let recommendedSettings: RecommendedSettings?
    }

    static func mergedRecommendedSettings(template: RecommendedSettings?, catalog: RecommendedSettings?) -> RecommendedSettings? {
        guard template != nil || catalog != nil else { return nil }
        return RecommendedSettings(
            ram_mb: template?.ram_mb ?? catalog?.ram_mb,
            osx_ram_mb: template?.osx_ram_mb ?? catalog?.osx_ram_mb,
            graphics: template?.graphics ?? catalog?.graphics,
            guest_adapter: template?.guest_adapter ?? catalog?.guest_adapter,
            cpu: template?.cpu ?? catalog?.cpu
        )
    }

    static func applyRecommendedSettings(_ settings: RecommendedSettings, to config: inout VMConfig,
                                         physicalMemory: UInt64 = ProcessInfo.processInfo.physicalMemory) {
        switch config.machineFamily {
        case .powerMacOSX:
            if let requested = settings.osx_ram_mb ?? settings.ram_mb {
                let hostCap: Int
                if physicalMemory >= 4 * 1_073_741_824 {
                    hostCap = 2048
                } else if physicalMemory >= 2 * 1_073_741_824 {
                    hostCap = 1024
                } else {
                    hostCap = 512
                }
                config.ramMB = min(max(requested, 256), min(2048, hostCap))
            }
        case .powerMacG4:
            if let requested = settings.ram_mb {
                let minRAM = MachineFamily.powerMacG4.ramPresets.min() ?? 128
                let maxRAM = MachineFamily.powerMacG4.ramPresets.max() ?? 896
                config.ramMB = min(max(requested, minRAM), maxRAM)
            }
            if settings.cpu?.lowercased() == "g3" {
                config.useG4CPU = false
            }
        case .quadra800:
            if let requested = settings.ram_mb {
                let minRAM = MachineFamily.quadra800.ramPresets.min() ?? 64
                let maxRAM = MachineFamily.quadra800.ramPresets.max() ?? 1000
                config.ramMB = min(max(requested, minRAM), maxRAM)
            }
        case .powerMac7500, .pippin:
            break
        }
    }

    // Deliberately narrow tar reader: gzip + flat regular files only. No shell,
    // archive-provided paths, links, extended headers, ownership, permissions,
    // or devices are interpreted. The one supported extension is GNU sparse
    // 1.0 for disk.img. GNU's positive base-256 size
    // encoding supports raw disks larger than USTAR's 8 GB limit.
    static func extract(_ archive: URL, into directory: URL, expectedBytes: Int64,
                        expectedDiskBytes: Int64? = nil, maximumStorageBytes: Int64? = nil, copland: Bool = false, quadra: Bool = false, osx: Bool = false,
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
        var chargedStorage: Int64 = 0
        var previousAllocation: Int64 = 0
        var seen = Set<String>()
        var sparseDiskSize: Int64?
        while true {
            if cancelled() { throw CancellationError() }
            let header = try readExactly(512, from: input)
            if header.allSatisfy({ $0 == 0 }) {
                guard sparseDiskSize == nil else { throw invalidArchive() }
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
                  header.subdata(in: 157..<257).allSatisfy({ $0 == 0 }),
                  header.subdata(in: 345..<500).allSatisfy({ $0 == 0 }) else { throw invalidArchive() }
            let memberSize = try number(header.subdata(in: 124..<136))
            if header[156] == 120 {
                guard sparseDiskSize == nil, !seen.contains("disk.img"), memberSize > 0,
                      memberSize <= CM_SPARSE_HEADER_LIMIT else { throw invalidArchive() }
                let pax = try readExactly(Int(memberSize), from: input)
                var diskSize: Int64 = 0
                guard pax.withUnsafeBytes({ cm_sparse_disk_size($0.bindMemory(to: UInt8.self).baseAddress, $0.count, &diskSize) }) else {
                    throw invalidArchive()
                }
                try readPadding(after: memberSize, from: input)
                sparseDiskSize = diskSize
                continue
            }
            guard header[156] == 0 || header[156] == 48 else { throw invalidArchive() }
            let nameBytes = header.prefix(100).prefix(while: { $0 != 0 })
            guard let headerName = String(data: nameBytes, encoding: .utf8) else { throw invalidArchive() }
            let name = sparseDiskSize == nil ? headerName : "disk.img"
            let allowed = ["config.json", "disk.img", "preview.png"] + (copland ? ["bootrom.bin", "nvram.bin"] : [])
                + (quadra ? ["Quadra800.rom", "pram.img"] : []) + (osx ? ["nvram.img"] : [])
            guard allowed.contains(name), seen.insert(name).inserted else { throw invalidArchive() }
            let size = sparseDiskSize ?? memberSize
            guard size > 0, size <= expectedBytes - extracted else { throw invalidArchive() }
            if name == "Quadra800.rom" && size != 1_048_576 { throw invalidArchive() }
            if name == "pram.img" && size != 256 && size != 512 { throw invalidArchive() }
            if name == "bootrom.bin" && size != 4_194_304 { throw invalidArchive() }
            if name == "nvram.bin" && size != 8209 { throw invalidArchive() }
            if name == "nvram.img" && size != 16_384 { throw invalidArchive() }
            if name == "config.json" && size > 65_536 { throw invalidArchive() }
            if name == "preview.png" && size > 16 * 1_048_576 { throw invalidArchive() }
            if name == "disk.img" && (size < 512 || size % 512 != 0) { throw invalidArchive() }
            if name == "disk.img", let expectedDiskBytes, size != expectedDiskBytes {
                throw MachineDownloadError.unsafeArchive("Its disk capacity does not match the catalog.")
            }
            if name != "disk.img", let maximumStorageBytes {
                let charge = storageCharge(for: size)
                guard charge <= maximumStorageBytes - chargedStorage else { throw storageBudgetExceeded() }
                chargedStorage += charge
            }
            let destination = directory.appendingPathComponent(name)
            let fd = open(destination.path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0o600)
            guard fd >= 0 else { throw invalidArchive() }
            let output = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
            do {
                if sparseDiskSize != nil {
                    try writeSparseDisk(input, to: output, size: size, memberSize: memberSize,
                        maximumStorageBytes: maximumStorageBytes.map { $0 - previousAllocation },
                        cancelled: cancelled, progress: { progress(extracted + $0) })
                    extracted += size
                    if maximumStorageBytes != nil { previousAllocation += try allocatedBytes(fd) }
                    // Subsequent flat members still account for disk blocks already written.
                    chargedStorage = max(chargedStorage, previousAllocation)
                    sparseDiskSize = nil
                    try output.close()
                    try readPadding(after: memberSize, from: input)
                    progress(extracted)
                    continue
                }
                if name == "disk.img", maximumStorageBytes != nil {
                    // Extending a raw APFS file does not allocate its contents.
                    // Punching skipped ranges below prevents APFS from filling
                    // small seek gaps with allocated zeros when it closes.
                    try output.truncate(atOffset: UInt64(size))
                    try output.seek(toOffset: 0)
                }
                var remaining = size
                while remaining > 0 {
                    if cancelled() { throw CancellationError() }
                    let data = try readExactly(Int(min(sparseChunkBytes, remaining)), from: input)
                    if name == "disk.img" && isZeroChunk(data) {
                        // Seek across whole zero chunks, preserving logical raw
                        // capacity without allocating empty guest space.
                        if maximumStorageBytes != nil {
                            var hole = fpunchhole_t(fp_flags: 0, reserved: 0,
                                fp_offset: off_t(size - remaining), fp_length: off_t(data.count))
                            guard fcntl(fd, F_PUNCHHOLE, &hole) == 0 else {
                                throw MachineDownloadError.unsafeArchive("This folder could not preserve the machine's empty disk space. Choose a folder on your Mac's APFS disk.")
                            }
                        }
                        try output.seek(toOffset: UInt64(size - remaining + Int64(data.count)))
                    } else {
                        if name == "disk.img", let maximumStorageBytes {
                            let charge = storageCharge(for: Int64(data.count))
                            guard charge <= maximumStorageBytes - chargedStorage else { throw storageBudgetExceeded() }
                            chargedStorage += charge
                        }
                        try output.write(contentsOf: data)
                        if let maximumStorageBytes {
                            guard try allocatedBytes(fd) <= maximumStorageBytes - previousAllocation else {
                                throw storageBudgetExceeded()
                            }
                        }
                    }
                    remaining -= Int64(data.count)
                    extracted += Int64(data.count)
                    progress(extracted)
                }
                try output.truncate(atOffset: UInt64(size))
                if let maximumStorageBytes {
                    previousAllocation += try allocatedBytes(fd)
                    guard previousAllocation <= maximumStorageBytes else { throw storageBudgetExceeded() }
                }
                try output.close()
            } catch {
                try? output.close()
                throw error
            }
            try readPadding(after: size, from: input)
        }
        process.waitUntilExit()
        guard process.terminationStatus == 0, extracted == expectedBytes,
              seen.contains("config.json"), seen.contains("disk.img"),
              !copland || (seen.contains("bootrom.bin") && seen.contains("nvram.bin")) else { throw invalidArchive() }
    }

    private static func writeSparseDisk(_ input: FileHandle, to output: FileHandle,
                                        size: Int64, memberSize: Int64, maximumStorageBytes: Int64?,
                                        cancelled: () -> Bool, progress: (Int64) -> Void) throws {
        var count = 0
        let context = Unmanaged.passUnretained(input).toOpaque()
        guard let extents = cm_sparse_read_map({ context, block in
            let handle = Unmanaged<FileHandle>.fromOpaque(context!).takeUnretainedValue()
            guard let data = try? MachineTemplateInstaller.readExactly(Int(CM_TAR_BLOCK), from: handle) else { return false }
            data.copyBytes(to: block!, count: Int(CM_TAR_BLOCK))
            return true
        }, context, size, memberSize, &count) else { throw invalidArchive() }
        defer { free(extents) }
        if let maximumStorageBytes {
            let stored = UnsafeBufferPointer(start: extents, count: count).reduce(Int64(0)) { $0 + $1.length }
            guard stored <= maximumStorageBytes else { throw storageBudgetExceeded() }
        }
        try output.truncate(atOffset: UInt64(size))
        let diskExtents = UnsafeBufferPointer(start: extents, count: count)
        let fd = output.fileDescriptor
        var blockSize: Int64 = 0
        if maximumStorageBytes != nil {
            var volume = statfs()
            guard fstatfs(fd, &volume) == 0, volume.f_bsize > 0 else { throw invalidArchive() }
            blockSize = Int64(volume.f_bsize)
        }
        var punchedThroughIndex = 0
        for (index, extent) in diskExtents.enumerated() {
            if cancelled() { throw CancellationError() }
            try output.seek(toOffset: UInt64(extent.offset))
            var remaining = extent.length
            while remaining > 0 {
                if cancelled() { throw CancellationError() }
                let data = try readExactly(Int(min(sparseChunkBytes, remaining)), from: input)
                try output.write(contentsOf: data)
                remaining -= Int64(data.count)
                let cursor = extent.offset + extent.length - remaining
                if let maximumStorageBytes, try allocatedBytes(fd) > maximumStorageBytes {
                    // APFS can speculatively preallocate or bridge gaps across
                    // neighboring writes; punch unused holes before enforcing budget.
                    let start = max(0, punchedThroughIndex - 1)
                    try punchSparseGaps(fd, extents: diskExtents, fromIndex: start,
                                        throughExtent: index, writtenCursor: cursor,
                                        totalSize: size, blockSize: blockSize, cancelled: cancelled)
                    punchedThroughIndex = index
                    if try allocatedBytes(fd) > maximumStorageBytes && start > 0 {
                        try punchSparseGaps(fd, extents: diskExtents, fromIndex: 0,
                                            throughExtent: index, writtenCursor: cursor,
                                            totalSize: size, blockSize: blockSize, cancelled: cancelled)
                    }
                    if try allocatedBytes(fd) > maximumStorageBytes {
                        throw storageBudgetExceeded()
                    }
                }
                progress(cursor)
            }
        }
        if let maximumStorageBytes {
            // APFS can allocate gaps between neighboring writes. Release
            // full filesystem blocks after all extents have been written.
            try punchSparseGaps(fd, extents: diskExtents, fromIndex: 0,
                                throughExtent: diskExtents.count, writtenCursor: size,
                                totalSize: size, blockSize: blockSize, cancelled: cancelled)
            if try allocatedBytes(fd) > maximumStorageBytes {
                throw storageBudgetExceeded()
            }
        }
    }

    private static func punchSparseGaps(_ fd: Int32, extents: UnsafeBufferPointer<CMSparseExtent>,
                                        fromIndex: Int, throughExtent: Int, writtenCursor: Int64,
                                        totalSize: Int64, blockSize: Int64, cancelled: () -> Bool) throws {
        var end: Int64 = fromIndex > 0 ? extents[fromIndex - 1].offset + extents[fromIndex - 1].length : 0
        for i in fromIndex..<min(throughExtent, extents.count) {
            if cancelled() { throw CancellationError() }
            let extent = extents[i]
            try punchHole(fd, from: end, to: extent.offset, blockSize: blockSize)
            end = extent.offset + extent.length
        }
        if throughExtent < extents.count {
            let current = extents[throughExtent]
            try punchHole(fd, from: end, to: current.offset, blockSize: blockSize)
            end = writtenCursor
        }
        try punchHole(fd, from: end, to: totalSize, blockSize: blockSize)
    }

    private static func punchHole(_ fd: Int32, from start: Int64, to end: Int64, blockSize: Int64) throws {
        let offset = ((start + blockSize - 1) / blockSize) * blockSize
        let length = (end / blockSize) * blockSize - offset
        if length <= 0 { return }
        var hole = fpunchhole_t(fp_flags: 0, reserved: 0, fp_offset: off_t(offset), fp_length: off_t(length))
        guard fcntl(fd, F_PUNCHHOLE, &hole) == 0 else {
            throw MachineDownloadError.unsafeArchive("This folder could not preserve the machine's empty disk space.")
        }
    }

    private static func readPadding(after size: Int64, from input: FileHandle) throws {
        let padding = Int((512 - size % 512) % 512)
        if padding > 0 {
            guard try readExactly(padding, from: input).allSatisfy({ $0 == 0 }) else { throw invalidArchive() }
        }
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

    private static func storageBudgetExceeded() -> MachineDownloadError {
        .unsafeArchive("The machine needs more initial storage than its catalog allows. Contact the template publisher.")
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
