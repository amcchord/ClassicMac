import Foundation

// A single emulated Quadra 800. Persisted as config.json inside the VM folder.
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

    // Media + boot
    var cdImagePath: String?
    var bootFromCD: Bool

    // Misc
    var networking: Bool

    init(id: UUID = UUID(),
         name: String,
         ramMB: Int = 128,
         diskSizeGB: Int = 2,
         width: Int = 1024,
         height: Int = 768,
         depth: Int = 16,
         useEnhancedFramebuffer: Bool = true,
         cdImagePath: String? = nil,
         bootFromCD: Bool = true,
         networking: Bool = true) {
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
        self.cdImagePath = cdImagePath
        self.bootFromCD = bootFromCD
        self.networking = networking
    }

    var folder: URL { AppPaths.vmDir(for: id) }
    var diskImageURL: URL { folder.appendingPathComponent(diskImageName) }
    var pramImageURL: URL { folder.appendingPathComponent(pramImageName) }
    var configURL: URL { folder.appendingPathComponent("config.json") }

    var resolutionLabel: String { "\(width)x\(height)x\(depth)" }
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
