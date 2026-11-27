import Darwin
import Foundation

enum CheckpointCopyMethod: String, Codable, Equatable {
    case apfsClone = "clonefile"
    case copy = "copy"

    var label: String {
        switch self {
        case .apfsClone: return "APFS Clone"
        case .copy: return "Copy"
        }
    }
}

enum CheckpointKind: String, Codable, Equatable {
    case manual
    case freshInstall = "fresh-install"
    case preRestore = "pre-restore"
    case preRecovery = "pre-recovery"

    var badgeLabel: String? {
        switch self {
        case .manual: return nil
        case .freshInstall: return "Fresh install"
        case .preRestore: return "Pre-restore backup"
        case .preRecovery: return "Pre-recovery backup"
        }
    }
}

struct VMCheckpoint: Codable, Identifiable, Equatable {
    let id: String
    let label: String
    let kind: CheckpointKind
    let createdAt: Date
    let diskBytes: Int64
    let allocatedBytes: Int64
    let method: CheckpointCopyMethod
}

struct VMSessionState: Codable, Equatable {
    var clean: Bool
    var lastExitReason: String?
    var lastStartedAt: Date?
    var lastStoppedAt: Date?
    var recoveryPrompted: Bool?
}

enum VMCheckpointError: LocalizedError, Equatable {
    case running
    case missingDisk
    case notFound
    case invalidCheckpoint(String)
    case copyFailed(String)

    var errorDescription: String? {
        switch self {
        case .running:
            return "Shut down this Mac before creating or restoring a checkpoint."
        case .missingDisk:
            return "The machine's hard disk could not be found."
        case .notFound:
            return "That checkpoint could not be found."
        case .invalidCheckpoint(let detail):
            return detail
        case .copyFailed(let detail):
            return "The checkpoint copy failed. \(detail)"
        }
    }
}

/// Manages zero-cost APFS copy-on-write checkpoints inside `<bundle>/Checkpoints/<id>/`
/// and tracks clean-shutdown state in `<bundle>/.session-state.json`.
enum VMCheckpointStore {
    static let directoryName = "Checkpoints"
    static let metadataFileName = "checkpoint.json"
    static let sessionStateFileName = ".session-state.json"

    private static let encoder: JSONEncoder = {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        e.dateEncodingStrategy = .iso8601
        return e
    }()

    private static let decoder: JSONDecoder = {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }()

    static func checkpointsDirectory(in bundleURL: URL) -> URL {
        bundleURL.appendingPathComponent(directoryName, isDirectory: true)
    }

    static func sessionStateURL(in bundleURL: URL) -> URL {
        bundleURL.appendingPathComponent(sessionStateFileName)
    }

    static func isValidCheckpointID(_ id: String) -> Bool {
        !id.isEmpty && !id.hasPrefix(".") && !id.contains("/") && !id.contains("\\") && id != ".."
    }

    // MARK: Session state

    static func readSessionState(in bundleURL: URL) -> VMSessionState? {
        let url = sessionStateURL(in: bundleURL)
        guard let data = try? Data(contentsOf: url) else { return nil }
        return try? decoder.decode(VMSessionState.self, from: data)
    }

    static func writeSessionState(_ state: VMSessionState, in bundleURL: URL) {
        let fm = FileManager.default
        guard fm.fileExists(atPath: bundleURL.path),
              let data = try? encoder.encode(state) else { return }
        try? data.write(to: sessionStateURL(in: bundleURL), options: .atomic)
    }

    static func markStarted(in bundleURL: URL, now: Date = Date()) {
        var state = readSessionState(in: bundleURL) ?? VMSessionState(clean: true)
        state.clean = false
        state.lastExitReason = nil
        state.lastStartedAt = now
        writeSessionState(state, in: bundleURL)
    }

    static func markStopped(in bundleURL: URL, clean: Bool, reason: String?, now: Date = Date()) {
        var state = readSessionState(in: bundleURL) ?? VMSessionState(clean: clean)
        state.clean = clean
        state.lastExitReason = reason
        state.lastStoppedAt = now
        if clean {
            state.recoveryPrompted = false
        }
        writeSessionState(state, in: bundleURL)
    }

    /// Creates a `"Pre-recovery backup"` once after an unclean exit when checkpoints exist.
    @discardableResult
    static func createPreRecoveryBackupIfNeeded(for config: VMConfig) -> VMCheckpoint? {
        guard var state = readSessionState(in: config.folder),
              !state.clean,
              state.recoveryPrompted != true,
              !list(in: config.folder).isEmpty,
              FileManager.default.fileExists(atPath: (config.machineFamily == .pippin ? config.flashImageURL : config.diskImageURL).path) else {
            return nil
        }
        state.recoveryPrompted = true
        writeSessionState(state, in: config.folder)
        return try? create(for: config, label: "Pre-recovery backup", kind: .preRecovery)
    }

    // MARK: Listing

    static func list(in bundleURL: URL) -> [VMCheckpoint] {
        let root = checkpointsDirectory(in: bundleURL)
        let fm = FileManager.default
        guard let entries = try? fm.contentsOfDirectory(
            at: root,
            includingPropertiesForKeys: [.isDirectoryKey],
            options: [.skipsHiddenFiles]
        ) else {
            return []
        }
        var results: [VMCheckpoint] = []
        for dir in entries {
            let id = dir.lastPathComponent
            guard isValidCheckpointID(id) else { continue }
            let metaURL = dir.appendingPathComponent(metadataFileName)
            let diskURL = dir.appendingPathComponent("disk.img")
            let flashURL = dir.appendingPathComponent("flash.img")
            guard fm.fileExists(atPath: diskURL.path) || fm.fileExists(atPath: flashURL.path),
                  let data = try? Data(contentsOf: metaURL),
                  let checkpoint = try? decoder.decode(VMCheckpoint.self, from: data),
                  checkpoint.id == id else {
                continue
            }
            results.append(checkpoint)
        }
        return results.sorted {
            if $0.createdAt != $1.createdAt {
                return $0.createdAt > $1.createdAt
            }
            return $0.id > $1.id
        }
    }

    // MARK: Create / Fresh install / Restore / Delete

    @discardableResult
    static func create(
        for config: VMConfig,
        label: String,
        kind: CheckpointKind = .manual,
        now: Date = Date()
    ) throws -> VMCheckpoint {
        let isPippin = config.machineFamily == .pippin
        return try create(
            in: config.folder,
            diskURL: isPippin ? config.flashImageURL : config.diskImageURL,
            primaryFileName: isPippin ? config.flashImageURL.lastPathComponent : "disk.img",
            pramURL: (config.machineFamily.usesPRAMImage || isPippin) ? config.pramImageURL : nil,
            isOSX: config.machineFamily == .powerMacOSX,
            label: label,
            kind: kind,
            now: now
        )
    }

    static func createFreshInstallIfSupported(in bundleURL: URL) {
        let fm = FileManager.default
        let diskURL = bundleURL.appendingPathComponent("disk.img")
        guard fm.fileExists(atPath: diskURL.path) else { return }
        guard !list(in: bundleURL).contains(where: { $0.kind == .freshInstall }) else { return }
        let pramURL = bundleURL.appendingPathComponent("pram.img")
        let nvramURL = bundleURL.appendingPathComponent("nvram.img")
        _ = try? create(
            in: bundleURL,
            diskURL: diskURL,
            primaryFileName: "disk.img",
            pramURL: fm.fileExists(atPath: pramURL.path) ? pramURL : nil,
            isOSX: fm.fileExists(atPath: nvramURL.path),
            label: "Fresh install",
            kind: .freshInstall
        )
        if readSessionState(in: bundleURL) == nil {
            writeSessionState(VMSessionState(clean: true, lastExitReason: "fresh-install"), in: bundleURL)
        }
    }

    private static func create(
        in bundleURL: URL,
        diskURL: URL,
        primaryFileName: String = "disk.img",
        pramURL: URL?,
        isOSX: Bool,
        label: String,
        kind: CheckpointKind,
        now: Date = Date()
    ) throws -> VMCheckpoint {
        let fm = FileManager.default
        guard fm.fileExists(atPath: diskURL.path) else {
            throw VMCheckpointError.missingDisk
        }

        let dir = checkpointsDirectory(in: bundleURL)
        try fm.createDirectory(at: dir, withIntermediateDirectories: true)

        let id = nextCheckpointID(in: dir, now: now)
        let staging = dir.appendingPathComponent(".\(id).tmp", isDirectory: true)
        let final = dir.appendingPathComponent(id, isDirectory: true)
        try? fm.removeItem(at: staging)
        try fm.createDirectory(at: staging, withIntermediateDirectories: true)
        defer {
            if fm.fileExists(atPath: staging.path) {
                try? fm.removeItem(at: staging)
            }
        }

        let stagedDisk = staging.appendingPathComponent(primaryFileName)
        let method = try cloneOrCopy(from: diskURL, to: stagedDisk)

        let nvramURL = bundleURL.appendingPathComponent("nvram.img")
        if fm.fileExists(atPath: nvramURL.path) {
            if isOSX {
                _ = try OSXNVRAM.validate(at: nvramURL)
            }
            _ = try cloneOrCopy(from: nvramURL, to: staging.appendingPathComponent("nvram.img"))
        }

        if let pramURL, fm.fileExists(atPath: pramURL.path) {
            _ = try cloneOrCopy(from: pramURL, to: staging.appendingPathComponent("pram.img"))
        }

        let (diskBytes, allocatedBytes) = fileSizes(at: stagedDisk)
        let cleanLabel = normalizedLabel(label)
        let checkpoint = VMCheckpoint(
            id: id,
            label: cleanLabel,
            kind: kind,
            createdAt: now,
            diskBytes: diskBytes,
            allocatedBytes: allocatedBytes,
            method: method
        )
        let metaData = try encoder.encode(checkpoint)
        try metaData.write(to: staging.appendingPathComponent(metadataFileName), options: .atomic)

        guard Darwin.rename(staging.path, final.path) == 0 else {
            let err = String(cString: strerror(errno))
            throw VMCheckpointError.copyFailed(err)
        }
        return checkpoint
    }

    @discardableResult
    static func restore(
        _ checkpointID: String,
        for config: VMConfig,
        createBackup: Bool = true
    ) throws -> CheckpointCopyMethod {
        guard isValidCheckpointID(checkpointID) else {
            throw VMCheckpointError.notFound
        }
        let fm = FileManager.default
        let cpDir = checkpointsDirectory(in: config.folder).appendingPathComponent(checkpointID, isDirectory: true)
        let isPippin = config.machineFamily == .pippin
        let primaryFileName = isPippin ? config.flashImageURL.lastPathComponent : "disk.img"
        let livePrimaryURL = isPippin ? config.flashImageURL : config.diskImageURL
        let cpDisk = cpDir.appendingPathComponent(primaryFileName)
        let cpMeta = cpDir.appendingPathComponent(metadataFileName)
        guard fm.fileExists(atPath: cpDir.path), fm.fileExists(atPath: cpMeta.path) else {
            throw VMCheckpointError.notFound
        }
        guard fm.fileExists(atPath: cpDisk.path) else {
            throw VMCheckpointError.invalidCheckpoint(isPippin
                ? "This checkpoint's Save Flash image is missing."
                : "This checkpoint's disk image is missing.")
        }

        let cpNVRAM = cpDir.appendingPathComponent("nvram.img")
        if config.machineFamily == .powerMacOSX, fm.fileExists(atPath: cpNVRAM.path) {
            _ = try OSXNVRAM.validate(at: cpNVRAM)
        }

        if createBackup, fm.fileExists(atPath: livePrimaryURL.path) {
            _ = try create(for: config, label: "Pre-restore backup", kind: .preRestore)
        }

        let tempDisk = config.folder.appendingPathComponent(".\(primaryFileName).restore.\(UUID().uuidString)")
        defer { try? fm.removeItem(at: tempDisk) }
        let method = try cloneOrCopy(from: cpDisk, to: tempDisk)

        let cpPRAM = cpDir.appendingPathComponent("pram.img")
        var tempNVRAM: URL?
        var tempPRAM: URL?
        defer {
            if let tempNVRAM { try? fm.removeItem(at: tempNVRAM) }
            if let tempPRAM { try? fm.removeItem(at: tempPRAM) }
        }

        if fm.fileExists(atPath: cpNVRAM.path) {
            let staged = config.folder.appendingPathComponent(".nvram.img.restore.\(UUID().uuidString)")
            _ = try cloneOrCopy(from: cpNVRAM, to: staged)
            tempNVRAM = staged
        }
        if (config.machineFamily.usesPRAMImage || isPippin), fm.fileExists(atPath: cpPRAM.path) {
            let staged = config.folder.appendingPathComponent(".pram.img.restore.\(UUID().uuidString)")
            _ = try cloneOrCopy(from: cpPRAM, to: staged)
            tempPRAM = staged
        }

        guard Darwin.rename(tempDisk.path, livePrimaryURL.path) == 0 else {
            let err = String(cString: strerror(errno))
            throw VMCheckpointError.copyFailed(err)
        }
        if let tempNVRAM {
            let dstNVRAM = config.folder.appendingPathComponent("nvram.img")
            guard Darwin.rename(tempNVRAM.path, dstNVRAM.path) == 0 else {
                let err = String(cString: strerror(errno))
                throw VMCheckpointError.copyFailed(err)
            }
        }
        if let tempPRAM {
            guard Darwin.rename(tempPRAM.path, config.pramImageURL.path) == 0 else {
                let err = String(cString: strerror(errno))
                throw VMCheckpointError.copyFailed(err)
            }
        }

        var state = readSessionState(in: config.folder) ?? VMSessionState(clean: true)
        state.clean = true
        state.lastExitReason = "restored-\(checkpointID)"
        state.recoveryPrompted = false
        writeSessionState(state, in: config.folder)

        return method
    }

    static func delete(_ checkpointID: String, in bundleURL: URL) throws {
        guard isValidCheckpointID(checkpointID) else {
            throw VMCheckpointError.notFound
        }
        let target = checkpointsDirectory(in: bundleURL).appendingPathComponent(checkpointID, isDirectory: true)
        guard FileManager.default.fileExists(atPath: target.path) else {
            throw VMCheckpointError.notFound
        }
        try FileManager.default.removeItem(at: target)
    }

    // MARK: APFS clonefile & helpers

    static func cloneOrCopy(from src: URL, to dst: URL) throws -> CheckpointCopyMethod {
        if FileManager.default.fileExists(atPath: dst.path) {
            try? FileManager.default.removeItem(at: dst)
        }
        if Darwin.clonefile(src.path, dst.path, 0) == 0 {
            return .apfsClone
        }
        let err = errno
        if err == ENOTSUP || err == EXDEV {
            try? FileManager.default.removeItem(at: dst)
            do {
                try FileManager.default.copyItem(at: src, to: dst)
                return .copy
            } catch {
                throw VMCheckpointError.copyFailed(error.localizedDescription)
            }
        }
        throw VMCheckpointError.copyFailed(String(cString: strerror(err)))
    }

    private static func fileSizes(at url: URL) -> (diskBytes: Int64, allocatedBytes: Int64) {
        var st = stat()
        if lstat(url.path, &st) == 0 {
            let logical = Int64(st.st_size)
            let blocks = Int64(st.st_blocks) * 512
            return (logical, blocks)
        }
        let values = try? url.resourceValues(forKeys: [.fileSizeKey, .fileAllocatedSizeKey])
        let logical = Int64(values?.fileSize ?? 0)
        let allocated = Int64(values?.fileAllocatedSize ?? values?.fileSize ?? 0)
        return (logical, allocated)
    }

    private static func nextCheckpointID(in directory: URL, now: Date) -> String {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(secondsFromGMT: 0)!
        let c = calendar.dateComponents([.year, .month, .day, .hour, .minute, .second], from: now)
        let base = String(
            format: "%04d%02d%02dT%02d%02d%02dZ",
            c.year ?? 1970, c.month ?? 1, c.day ?? 1,
            c.hour ?? 0, c.minute ?? 0, c.second ?? 0
        )
        let fm = FileManager.default
        var candidate = base
        var suffix = 2
        while fm.fileExists(atPath: directory.appendingPathComponent(candidate).path) ||
                fm.fileExists(atPath: directory.appendingPathComponent(".\(candidate).tmp").path) {
            candidate = "\(base)-\(suffix)"
            suffix += 1
        }
        return candidate
    }

    private static func normalizedLabel(_ raw: String) -> String {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { return "Checkpoint" }
        return String(trimmed.prefix(80))
    }
}
