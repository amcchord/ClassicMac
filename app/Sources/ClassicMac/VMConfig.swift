import Foundation

// The emulated machine model. Determines which QEMU binary and machine type a
// VM boots, which OS versions it can run, and which features are available.
enum MachineFamily: String, Codable, CaseIterable, Identifiable {
    // Motorola 68040 Quadra 800 (qemu-system-m68k -M q800). System 7.1 - 8.1.
    case quadra800
    // PowerPC Power Mac G4 (qemu-system-ppc -M mac99). Mac OS 8.5 - 9.2.2.
    // Boots through OpenBIOS, so no Apple ROM file is needed.
    case powerMacG4

    var id: String { rawValue }

    var label: String {
        switch self {
        case .quadra800: return "Quadra 800"
        case .powerMacG4: return "Power Mac G4"
        }
    }

    var cpuLabel: String {
        switch self {
        case .quadra800: return "Motorola 68040"
        case .powerMacG4: return "PowerPC G4"
        }
    }

    var hardwareLabel: String {
        switch self {
        case .quadra800: return "Macintosh Quadra 800 - Motorola 68040"
        case .powerMacG4: return "Power Mac G4 - PowerPC"
        }
    }

    var osSupportLabel: String {
        switch self {
        case .quadra800: return "System 7.1 - Mac OS 8.1"
        case .powerMacG4: return "Mac OS 8.5 - 9.2.2"
        }
    }

    var defaultName: String {
        switch self {
        case .quadra800: return "Mac OS 8.1"
        case .powerMacG4: return "Mac OS 9.2"
        }
    }

    // The Power Mac tops out at 896 MB: Mac OS 9 sound (and general stability
    // under emulation) breaks with 1 GB or more installed.
    var ramPresets: [Int] {
        switch self {
        case .quadra800: return [64, 128, 256, 512, 1000]
        case .powerMacG4: return [128, 256, 512, 768, 896]
        }
    }

    var defaultRAMMB: Int {
        switch self {
        case .quadra800: return 128
        case .powerMacG4: return 512
        }
    }

    var diskSizePresets: [Int] {
        switch self {
        case .quadra800: return [1, 2, 4, 8]
        case .powerMacG4: return [2, 4, 8, 16]
        }
    }

    var defaultDiskSizeGB: Int {
        switch self {
        case .quadra800: return 2
        case .powerMacG4: return 8
        }
    }

    // The qfb enhanced framebuffer is a Quadra/NuBus feature; the mac99
    // display is the std VGA framebuffer driven by the bundled qemu_vga.ndrv,
    // which supports custom boot resolutions (via -g) and live window
    // resizing (via the vga-host-resize channel). Host folder sharing works
    // on both: the Quadra through the classicvirtio NuBus transport and the
    // Power Mac through virtio-9p-pci plus the classicvirtio ndrvloader.
    // Sound works on both: the Quadra has the Apple Sound Chip and the Power
    // Mac the screamer (AWACS) port.
    var supportsEnhancedFramebuffer: Bool { self == .quadra800 }
    var supportsSharedFolder: Bool { true }
    var supportsSound: Bool { true }
    var supportsCustomResolution: Bool { true }
    var supportsFloppyDisk: Bool { self == .quadra800 }
    var usesPRAMImage: Bool { self == .quadra800 }
}

// A single emulated classic Macintosh. Each VM is a self-contained ".classic"
// package (a directory Finder shows as a file) whose location is `bundleURL`;
// the settings below are persisted as config.json inside that package,
// alongside disk.img (and pram.img on the Quadra 800).
struct VMConfig: Codable, Identifiable, Hashable {
    var id: UUID
    var name: String
    var machineFamily: MachineFamily
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

    // Writable raw floppy image mounted in the Quadra's removable drive.
    // New World Power Macs did not have floppy hardware, so this setting is
    // only available for the Quadra 800.
    var floppyImagePath: String?

    // Start up with the bundled ClassicMac Tools CD in the dedicated second
    // CD drive. Independent of cdImagePath, so a bootable install disc and
    // the Tools CD can be mounted at the same time.
    var toolsCDInserted: Bool

    // Misc
    var networking: Bool
    var sound: Bool

    // Tablet input: use a virtio absolute-pointing device so the mouse moves
    // seamlessly in and out of the VM window without needing to be captured.
    // On by default. When off, the standard relative mouse is used and the
    // window grabs the cursor on click (Control-Option-G to release).
    var tabletInput: Bool

    // Host-side input remapping for classic guests: right-click delivered as
    // Control+click (contextual menus) and scroll wheel as arrow keys. On by
    // default; turned off per-VM when a real driver like USB Overdrive is
    // installed in the guest, which would otherwise double up input.
    var classicInputHelpers: Bool

    // Host folder shared with the guest (appears on the Mac desktop). Optional.
    var sharedFolderPath: String?

    // The .classic package on disk that holds this VM. Runtime only: it is the
    // file's location, not part of the persisted config, so it is excluded from
    // Codable (see CodingKeys) and set when the VM is loaded, created, or opened.
    var bundleURL: URL?

    // Only the persisted fields are encoded; bundleURL is intentionally omitted.
    private enum CodingKeys: String, CodingKey {
        case id, name, machineFamily, ramMB, diskImageName, pramImageName, diskSizeGB
        case width, height, depth, useEnhancedFramebuffer, customResolution
        case cdImagePath, bootFromCD, floppyImagePath
        case networking, sound, sharedFolderPath
        case classicInputHelpers, tabletInput, toolsCDInserted
    }

    init(id: UUID = UUID(),
         name: String,
         machineFamily: MachineFamily = .quadra800,
         ramMB: Int = 128,
         diskSizeGB: Int = 2,
         width: Int = 1024,
         height: Int = 768,
         depth: Int = 16,
         useEnhancedFramebuffer: Bool = true,
         customResolution: Bool = false,
         cdImagePath: String? = nil,
         bootFromCD: Bool = true,
         floppyImagePath: String? = nil,
         toolsCDInserted: Bool = false,
         networking: Bool = true,
         sound: Bool = true,
         tabletInput: Bool = true,
         classicInputHelpers: Bool = true,
         sharedFolderPath: String? = nil,
         bundleURL: URL? = nil) {
        self.id = id
        self.name = name
        self.machineFamily = machineFamily
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
        self.floppyImagePath = machineFamily.supportsFloppyDisk ? floppyImagePath : nil
        self.toolsCDInserted = toolsCDInserted
        self.networking = networking
        self.sound = sound
        self.tabletInput = tabletInput
        self.classicInputHelpers = classicInputHelpers
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
        // VMs created before PPC support have no machineFamily key; they are
        // all Quadra 800s.
        machineFamily = try c.decodeIfPresent(MachineFamily.self, forKey: .machineFamily) ?? .quadra800
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
        floppyImagePath = try c.decodeIfPresent(String.self, forKey: .floppyImagePath)
        toolsCDInserted = try c.decodeIfPresent(Bool.self, forKey: .toolsCDInserted) ?? false
        networking = try c.decodeIfPresent(Bool.self, forKey: .networking) ?? true
        sound = try c.decodeIfPresent(Bool.self, forKey: .sound) ?? true
        classicInputHelpers = try c.decodeIfPresent(Bool.self, forKey: .classicInputHelpers) ?? true
        tabletInput = try c.decodeIfPresent(Bool.self, forKey: .tabletInput) ?? true
        sharedFolderPath = try c.decodeIfPresent(String.self, forKey: .sharedFolderPath)
        sanitize()
    }

    // Brings a decoded config back within what its machine family supports, so
    // configs written by older builds (or edited by hand) always boot. In
    // particular Mac OS 9 on the Power Mac loses sound and becomes unstable
    // with 1 GB or more of RAM, so early PPC configs with bigger values are
    // pulled back to 896 MB.
    private mutating func sanitize() {
        if machineFamily == .powerMacG4 {
            if ramMB > 896 {
                ramMB = 896
            }
            useEnhancedFramebuffer = false
        }
        if !machineFamily.supportsSharedFolder {
            sharedFolderPath = nil
        }
        if !machineFamily.supportsFloppyDisk {
            floppyImagePath = nil
        }
        if ramMB < 8 {
            ramMB = machineFamily.defaultRAMMB
        }
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
    // The machine's last captured screen, saved when it shuts down.
    var previewURL: URL { folder.appendingPathComponent("preview.png") }

    var resolutionLabel: String {
        if machineFamily == .powerMacG4 {
            // The mac99 display always runs at millions of colors; the stored
            // depth only applies to the Quadra framebuffers.
            return "\(width)x\(height)"
        }
        return "\(width)x\(height)x\(depth)"
    }

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

    // Named the way the classic Monitors control panel does.
    var label: String {
        switch self {
        case .greys256: return "256 Colors"
        case .thousands: return "Thousands"
        case .millions: return "Millions"
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

// RAM presets now live on MachineFamily (see ramPresets there); the Quadra
// keeps its historical values and the Power Mac G4 allows more for Mac OS 9.
