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
    static let defaultURL = URL(string: "https://mcchord.net/classicmac/catalog-v2.json")!
    static let retroBootURL = URL(string: "https://mcchord.net/retroboot-downloads/catalog.json")!
    static let maximumCatalogBytes: Int64 = 1_048_576
    static var currentAppVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "4.10.0"
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

struct ThinArchive: Codable, Hashable {
    let format: String
    let archiveURL: URL
    let archiveBytes: Int64
    let sha256: String
}

struct RecommendedSettings: Codable, Hashable {
    let ram_mb: Int?
    let osx_ram_mb: Int?
    let graphics: String?
    let guest_adapter: String?
    let cpu: String?

    init(ram_mb: Int? = nil, osx_ram_mb: Int? = nil, graphics: String? = nil,
         guest_adapter: String? = nil, cpu: String? = nil) {
        self.ram_mb = ram_mb
        self.osx_ram_mb = osx_ram_mb
        self.graphics = graphics
        self.guest_adapter = guest_adapter
        self.cpu = cpu
    }
}

struct DownloadableMachine: Codable, Identifiable, Hashable {
    let id: String
    let name: String
    let summary: String
    let osVersion: String
    let minimumAppVersion: String
    let archiveURL: URL
    let archiveBytes: Int64
    // Exact sum of the uncompressed files, including every zero byte in the
    // raw disk. This remains the full expansion bound for older importers.
    let installedBytes: Int64
    let sha256: String
    let diskCapacityBytes: Int64?
    let requiredStorageBytes: Int64?
    let machineFamily: MachineFamily?
    let thinArchive: ThinArchive?
    let tags: [String]?
    let highlights: [String]?
    let recommendedSettings: RecommendedSettings?
    var family: MachineFamily { machineFamily ?? .powerMacG4 }
    private var archiveFirmware: (bytes: Int64, files: Int64) {
        switch family {
        case .powerMacG4: return (0, 0)
        case .powerMacOSX: return (16_384, 1)
        case .powerMac7500: return (4_202_513, 2)
        case .quadra800: return (1_049_088, 2)
        }
    }

    init(id: String, name: String, summary: String, osVersion: String,
         minimumAppVersion: String, archiveURL: URL,
         archiveBytes: Int64, installedBytes: Int64, sha256: String,
         diskCapacityBytes: Int64? = nil, requiredStorageBytes: Int64? = nil, machineFamily: MachineFamily? = nil,
         thinArchive: ThinArchive? = nil, tags: [String]? = nil,
         highlights: [String]? = nil, recommendedSettings: RecommendedSettings? = nil) {
        self.id = id; self.name = name; self.summary = summary
        self.osVersion = osVersion
        self.minimumAppVersion = minimumAppVersion
        let resolved = Self.resolvedArchive(archiveURL: archiveURL, archiveBytes: archiveBytes,
                                            sha256: sha256, thinArchive: thinArchive)
        self.archiveURL = resolved.archiveURL
        self.archiveBytes = resolved.archiveBytes
        self.installedBytes = installedBytes
        self.sha256 = resolved.sha256
        self.diskCapacityBytes = diskCapacityBytes
        self.requiredStorageBytes = requiredStorageBytes
        self.machineFamily = machineFamily
        self.thinArchive = thinArchive
        self.tags = tags
        self.highlights = highlights
        self.recommendedSettings = recommendedSettings
    }

    private enum CodingKeys: String, CodingKey {
        case id, name, summary, osVersion, minimumAppVersion, archiveURL, archiveBytes
        case installedBytes, sha256, diskCapacityBytes, requiredStorageBytes, machineFamily
        case thinArchive, tags, highlights, recommendedSettings
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(String.self, forKey: .id)
        name = try c.decode(String.self, forKey: .name)
        summary = try c.decode(String.self, forKey: .summary)
        osVersion = try c.decode(String.self, forKey: .osVersion)
        minimumAppVersion = try c.decode(String.self, forKey: .minimumAppVersion)
        let rawArchiveURL = try c.decode(URL.self, forKey: .archiveURL)
        let rawArchiveBytes = try c.decode(Int64.self, forKey: .archiveBytes)
        installedBytes = try c.decode(Int64.self, forKey: .installedBytes)
        let rawSHA256 = try c.decode(String.self, forKey: .sha256)
        diskCapacityBytes = try c.decodeIfPresent(Int64.self, forKey: .diskCapacityBytes)
        requiredStorageBytes = try c.decodeIfPresent(Int64.self, forKey: .requiredStorageBytes)
        machineFamily = try c.decodeIfPresent(MachineFamily.self, forKey: .machineFamily)
        let thin = try? c.decodeIfPresent(ThinArchive.self, forKey: .thinArchive)
        thinArchive = thin
        tags = try c.decodeIfPresent([String].self, forKey: .tags)
        highlights = try c.decodeIfPresent([String].self, forKey: .highlights)
        recommendedSettings = try? c.decodeIfPresent(RecommendedSettings.self, forKey: .recommendedSettings)
        let resolved = Self.resolvedArchive(archiveURL: rawArchiveURL, archiveBytes: rawArchiveBytes,
                                            sha256: rawSHA256, thinArchive: thin)
        archiveURL = resolved.archiveURL
        archiveBytes = resolved.archiveBytes
        sha256 = resolved.sha256
    }

    private static func resolvedArchive(archiveURL: URL, archiveBytes: Int64, sha256: String,
                                        thinArchive: ThinArchive?) -> (archiveURL: URL, archiveBytes: Int64, sha256: String) {
        if let thin = thinArchive,
           thin.format == "retroboot-thin-1",
           secureURL(thin.archiveURL),
           (1...(140 * 1_073_741_824)).contains(thin.archiveBytes),
           isValidSHA256(thin.sha256) {
            return (thin.archiveURL, thin.archiveBytes, thin.sha256)
        }
        return (archiveURL, archiveBytes, sha256)
    }

    private static func isValidSHA256(_ value: String) -> Bool {
        value.count == 64 && value.allSatisfy({ "0123456789abcdef".contains($0) })
    }

    func validate() throws {
        let allowedID = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyz0123456789-._")
        guard !id.isEmpty, id.utf8.count <= 100,
              id.unicodeScalars.allSatisfy({ allowedID.contains($0) }),
              !id.hasPrefix("."),
              Self.validText(name, maximum: 100),
              Self.validText(summary, maximum: 1000),
              Self.validText(osVersion, maximum: 80),
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
                  installedBytes - capacity <= 16 * 1_048_576 + 65_536 + archiveFirmware.bytes else {
                throw MachineDownloadError.invalidCatalog("Its disk capacity does not match the expanded files.")
            }
        }
        if let storage = requiredStorageBytes {
            // Each flat archive member can round up by less than one
            // accounting chunk. Metadata never relaxes the full expansion cap.
            guard diskCapacityBytes != nil, storage >= 1_048_576,
                  storage % 1_048_576 == 0,
                  storage <= installedBytes + (3 + archiveFirmware.files) * 1_048_576 else {
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

// Records the original download; later guest changes do not alter provenance.
struct VMTemplateMetadata: Codable, Equatable {
    static let fileName = "template-info.json"
    let schemaVersion: Int
    let templateID: String
    let osVersion: String
    let archiveSHA256: String
    let importedAt: Date

    init(machine: DownloadableMachine, importedAt: Date = Date()) {
        schemaVersion = 1
        templateID = machine.id
        osVersion = machine.osVersion
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
