import Foundation

enum MachineDownloadError: LocalizedError {
    case invalidCatalog(String)
    case unsupportedVersion(String)
    case network(String)
    case integrity
    case unsafeArchive(String)
    case insufficientSpace(Int64)
    case invalidName

    var errorDescription: String? {
        switch self {
        case .invalidCatalog(let reason): return "The machine catalog could not be opened. \(reason)"
        case .unsupportedVersion(let version): return "This machine needs ClassicMac \(version) or later. Update ClassicMac, then try again."
        case .network(let reason): return "The download could not finish. \(reason)"
        case .integrity: return "The downloaded file did not pass its integrity check. Please retry the download."
        case .unsafeArchive(let reason): return "The downloaded machine could not be installed. \(reason)"
        case .insufficientSpace(let bytes): return "There isn't enough free space. This step needs \(ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file)) available. Choose another folder or free some space, then retry."
        case .invalidName: return "Give your Mac a name of up to 100 characters."
        }
    }
}

struct MachineCatalog: Codable {
    static let defaultURL = URL(string: "https://mcchord.net/classicmac/catalog.json")!
    static let maximumCatalogBytes: Int64 = 1_048_576
    static var currentAppVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "3.0.1"
    }

    let schemaVersion: Int
    let machines: [DownloadableMachine]

    static func decode(_ data: Data) throws -> Self {
        guard data.count <= maximumCatalogBytes else {
            throw MachineDownloadError.invalidCatalog("The catalog is too large.")
        }
        let result: Self
        do { result = try JSONDecoder().decode(Self.self, from: data) }
        catch { throw MachineDownloadError.invalidCatalog("Its format is not recognized.") }
        guard result.schemaVersion == 1 else {
            throw MachineDownloadError.invalidCatalog("Update ClassicMac to read this catalog version.")
        }
        guard result.machines.count <= 50,
              Set(result.machines.map(\.id)).count == result.machines.count else {
            throw MachineDownloadError.invalidCatalog("Its machine list is not valid.")
        }
        for machine in result.machines { try machine.validate() }
        return result
    }
}

struct DownloadableMachine: Codable, Identifiable, Hashable {
    let id: String
    let name: String
    let summary: String
    let osVersion: String
    let gxMetalVersion: String
    let minimumAppVersion: String
    let archiveURL: URL
    let archiveBytes: Int64
    // Exact sum of the uncompressed files, including every zero byte in the
    // raw disk. This remains the full expansion bound for older importers.
    let installedBytes: Int64
    let sha256: String
    let diskCapacityBytes: Int64?
    let requiredStorageBytes: Int64?

    init(id: String, name: String, summary: String, osVersion: String,
         gxMetalVersion: String, minimumAppVersion: String, archiveURL: URL,
         archiveBytes: Int64, installedBytes: Int64, sha256: String,
         diskCapacityBytes: Int64? = nil, requiredStorageBytes: Int64? = nil) {
        self.id = id; self.name = name; self.summary = summary
        self.osVersion = osVersion; self.gxMetalVersion = gxMetalVersion
        self.minimumAppVersion = minimumAppVersion; self.archiveURL = archiveURL
        self.archiveBytes = archiveBytes; self.installedBytes = installedBytes
        self.sha256 = sha256; self.diskCapacityBytes = diskCapacityBytes
        self.requiredStorageBytes = requiredStorageBytes
    }

    func validate() throws {
        let allowedID = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyz0123456789-._")
        guard !id.isEmpty, id.utf8.count <= 100,
              id.unicodeScalars.allSatisfy({ allowedID.contains($0) }),
              !id.hasPrefix("."),
              Self.validText(name, maximum: 100),
              Self.validText(summary, maximum: 1000),
              Self.validText(osVersion, maximum: 80),
              Self.validText(gxMetalVersion, maximum: 80),
              AppReleaseVersion(minimumAppVersion) != nil,
              Self.secureURL(archiveURL),
              archiveBytes > 0, archiveBytes <= 140 * 1_073_741_824,
              installedBytes >= 512, installedBytes <= 140 * 1_073_741_824,
              sha256.count == 64,
              sha256.allSatisfy({ "0123456789abcdef".contains($0) }) else {
            throw MachineDownloadError.invalidCatalog("A machine contains invalid download information.")
        }
        if let capacity = diskCapacityBytes {
            guard capacity >= 512, capacity % 512 == 0, capacity < installedBytes,
                  installedBytes - capacity <= 16 * 1_048_576 + 65_536 else {
                throw MachineDownloadError.invalidCatalog("Its disk capacity does not match the expanded files.")
            }
        }
        if let storage = requiredStorageBytes {
            // Three flat archive members can each round up by less than one
            // accounting chunk. Metadata never relaxes the full expansion cap.
            guard diskCapacityBytes != nil, storage >= 1_048_576,
                  storage % 1_048_576 == 0,
                  storage <= installedBytes + 3 * 1_048_576 else {
                throw MachineDownloadError.invalidCatalog("Its initial storage requirement is not valid.")
            }
        }
    }

    func checkCompatibility(appVersion: String = MachineCatalog.currentAppVersion) throws {
        guard let current = AppReleaseVersion(appVersion),
              let minimum = AppReleaseVersion(minimumAppVersion), current >= minimum else {
            throw MachineDownloadError.unsupportedVersion(minimumAppVersion)
        }
    }

    static func secureURL(_ url: URL) -> Bool {
        url.scheme?.lowercased() == "https" && !(url.host ?? "").isEmpty &&
            url.user == nil && url.password == nil && url.fragment == nil
    }

    private static func validText(_ text: String, maximum: Int) -> Bool {
        !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty &&
            text.count <= maximum && !text.unicodeScalars.contains(where: {
                CharacterSet.controlCharacters.contains($0) && $0 != "\n"
            })
    }
}

struct AppReleaseVersion: Comparable {
    let components: [Int]

    init?(_ string: String) {
        let parts = string.split(separator: ".", omittingEmptySubsequences: false)
        guard (2...3).contains(parts.count), parts.allSatisfy({
            !$0.isEmpty && $0.count <= 6 && $0.allSatisfy(\.isASCII) && $0.allSatisfy(\.isNumber)
        }) else { return nil }
        components = parts.map { Int($0)! } + (parts.count == 2 ? [0] : [])
    }

    static func < (lhs: Self, rhs: Self) -> Bool {
        lhs.components.lexicographicallyPrecedes(rhs.components)
    }
}

// Provenance, not a live assertion that the guest's driver is still installed.
// A user can modify or remove GXMetal inside the machine after importing it.
struct VMTemplateMetadata: Codable, Equatable {
    static let fileName = "template-info.json"
    let schemaVersion: Int
    let templateID: String
    let osVersion: String
    let gxMetalVersion: String
    let archiveSHA256: String
    let importedAt: Date

    init(machine: DownloadableMachine, importedAt: Date = Date()) {
        schemaVersion = 1
        templateID = machine.id
        osVersion = machine.osVersion
        gxMetalVersion = machine.gxMetalVersion
        archiveSHA256 = machine.sha256
        self.importedAt = importedAt
    }

    static func load(from bundle: URL) -> Self? {
        let url = bundle.appendingPathComponent(fileName)
        guard let size = try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize,
              size <= 16_384,
              let data = try? Data(contentsOf: url),
              let result = try? JSONDecoder().decode(Self.self, from: data),
              result.schemaVersion == 1 else { return nil }
        return result
    }
}
