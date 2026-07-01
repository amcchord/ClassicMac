import Foundation

// A single emulated Quadra 800. Each VM is a self-contained ".classic" package
// (a directory Finder shows as a file) whose location is `bundleURL`; the
// settings below are persisted as config.json inside that package, alongside
// disk.img and pram.img.
struct VMConfig: Codable, Identifiable, Hashable {
    var id: UUID
    var name: String
    var ramMB: Int
    var diskImageName: String
    var pramImageName: String
    var diskSizeGB: Int

    // Display
    var width: Int
    var height: Int
    var depth: Int
    var useEnhancedFramebuffer: Bool
    var customResolution: Bool

    // Media + boot
    var cdImagePath: String?
    var bootFromCD: Bool

    // Misc
    var networking: Bool
    var sound: Bool

    // Host folder shared with the guest (appears on the Mac desktop). Optional.
    var sharedFolderPath: String?

    // The .classic package on disk that holds this VM. Runtime only: it is the
    // file's location, not part of the persisted config, so it is excluded from
    // Codable (see CodingKeys) and set when the VM is loaded, created, or opened.
    var bundleURL: URL?

    // Only the persisted fields are encoded; bundleURL is intentionally omitted.
    private enum CodingKeys: String, CodingKey {
        case id, name, ramMB, diskImageName, pramImageName, diskSizeGB
        case width, height, depth, useEnhancedFramebuffer, customResolution
        case cdImagePath, bootFromCD, networking, sound, sharedFolderPath
    }

    init(id: UUID = UUID(),
         name: String,
         ramMB: Int = 128,
         diskSizeGB: Int = 2,
         width: Int = 1024,
         height: Int = 768,
         depth: Int = 16,
         useEnhancedFramebuffer: Bool = true,
         customResolution: Bool = false,
         cdImagePath: String? = nil,
         bootFromCD: Bool = true,
         networking: Bool = true,
         sound: Bool = true,
         sharedFolderPath: String? = nil,
         bundleURL: URL? = nil) {
        self.id = id
        self.name = name
        self.ramMB = ramMB
        self.diskImageName = "disk.img"
        self.pramImageName = "pram.img"
        self.diskSizeGB = diskSizeGB
        self.width = width
        self.height = height
        self.depth = depth
        self.useEnhancedFramebuffer = useEnhancedFramebuffer
        self.customResolution = customResolution
        self.cdImagePath = cdImagePath
        self.bootFromCD = bootFromCD
        self.networking = networking
        self.sound = sound
        self.sharedFolderPath = sharedFolderPath
        self.bundleURL = bundleURL
    }

    // Bounds accepted by the enhanced framebuffer.
    static let maxWidth = 3840
    static let maxHeight = 2160
    static let minWidth = 512
    static let minHeight = 384

    // Tolerant decoding so VMs created by older builds keep loading.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(UUID.self, forKey: .id)
        name = try c.decode(String.self, forKey: .name)
        ramMB = try c.decodeIfPresent(Int.self, forKey: .ramMB) ?? 128
        diskImageName = try c.decodeIfPresent(String.self, forKey: .diskImageName) ?? "disk.img"
        pramImageName = try c.decodeIfPresent(String.self, forKey: .pramImageName) ?? "pram.img"
        diskSizeGB = try c.decodeIfPresent(Int.self, forKey: .diskSizeGB) ?? 2
        width = try c.decodeIfPresent(Int.self, forKey: .width) ?? 1024
        height = try c.decodeIfPresent(Int.self, forKey: .height) ?? 768
        depth = try c.decodeIfPresent(Int.self, forKey: .depth) ?? 16
        useEnhancedFramebuffer = try c.decodeIfPresent(Bool.self, forKey: .useEnhancedFramebuffer) ?? true
        customResolution = try c.decodeIfPresent(Bool.self, forKey: .customResolution) ?? false
        cdImagePath = try c.decodeIfPresent(String.self, forKey: .cdImagePath)
        bootFromCD = try c.decodeIfPresent(Bool.self, forKey: .bootFromCD) ?? false
        networking = try c.decodeIfPresent(Bool.self, forKey: .networking) ?? true
        sound = try c.decodeIfPresent(Bool.self, forKey: .sound) ?? true
        sharedFolderPath = try c.decodeIfPresent(String.self, forKey: .sharedFolderPath)
    }

    // Filename extension for a VM package.
    static let packageExtension = "classic"

    // The VM package directory. Falls back to the legacy Application Support
    // location for a config that has not been given a bundle yet (should not
    // happen for loaded/created VMs).
    var folder: URL { bundleURL ?? AppPaths.legacyVMDir(for: id) }
    var diskImageURL: URL { folder.appendingPathComponent(diskImageName) }
    var pramImageURL: URL { folder.appendingPathComponent(pramImageName) }
    var configURL: URL { folder.appendingPathComponent("config.json") }

    var resolutionLabel: String { "\(width)x\(height)x\(depth)" }

    var hasSharedFolder: Bool {
        guard let path = sharedFolderPath else { return false }
        return !path.isEmpty
    }

    // Volume name shown on the Mac desktop for the shared folder. Classic Mac
    // volume names cannot contain ":" and are limited to 27 characters.
    var sharedVolumeName: String {
        guard let path = sharedFolderPath, !path.isEmpty else { return "Shared" }
        let base = URL(fileURLWithPath: path).lastPathComponent
        var cleaned = base.replacingOccurrences(of: ":", with: "-")
        cleaned = cleaned.replacingOccurrences(of: ",", with: "-")
        if cleaned.isEmpty {
            return "Shared"
        }
        if cleaned.count > 27 {
            return String(cleaned.prefix(27))
        }
        return cleaned
    }
}

// Color depth presets understood by the framebuffer.
enum ColorDepth: Int, CaseIterable, Identifiable {
    case greys256 = 8
    case thousands = 16
    case millions = 24

    var id: Int { rawValue }

    var label: String {
        switch self {
        case .greys256: return "256 Colors (8-bit)"
        case .thousands: return "Thousands (16-bit)"
        case .millions: return "Millions (24-bit)"
        }
    }
}

// Common resolutions. The enhanced framebuffer accepts arbitrary sizes; these
// are sensible presets for a class.
struct ResolutionPreset: Identifiable, Hashable {
    var width: Int
    var height: Int
    var id: String { "\(width)x\(height)" }
    var label: String { "\(width) x \(height)" }

    static let all: [ResolutionPreset] = [
        ResolutionPreset(width: 640, height: 480),
        ResolutionPreset(width: 800, height: 600),
        ResolutionPreset(width: 1024, height: 768),
        ResolutionPreset(width: 1152, height: 870),
        ResolutionPreset(width: 1280, height: 1024),
        ResolutionPreset(width: 1440, height: 900)
    ]
}

// RAM presets for the Quadra 800 (real hardware topped out around 136 MB; QEMU
// allows more but classic Mac OS gains little above 128-256 MB).
let ramPresets: [Int] = [64, 128, 256, 512, 1000]
