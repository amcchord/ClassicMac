import Foundation

// Resolves the locations of the bundled QEMU tools / firmware and the
// per-user Application Support directories that hold VMs and imported media.
enum AppPaths {

    // MARK: Bundled resources

    // Inside ClassicMac.app this is Contents/Resources. During development with
    // `swift run` it falls back to the CLASSICMAC_RESOURCES env var or the
    // vendored QEMU build directory so the app remains runnable from source.
    static var resourcesDir: URL {
        let bundleResources = Bundle.main.resourceURL
        if let bundleResources = bundleResources, FileManager.default.fileExists(atPath: bundleResources.appendingPathComponent("qemu/pc-bios").path) {
            return bundleResources
        }
        if let override = ProcessInfo.processInfo.environment["CLASSICMAC_RESOURCES"] {
            return URL(fileURLWithPath: override)
        }
        if let bundleResources = bundleResources {
            return bundleResources
        }
        return Bundle.main.bundleURL
    }

    // The emulator binaries live inside per-family helper app bundles in
    // Contents/Helpers so the running machine gets its own Dock icon and name
    // ("Quadra 800" / "Power Mac G4") instead of appearing as a bare
    // qemu-system executable.
    static func helperAppName(for family: MachineFamily) -> String {
        switch family {
        case .quadra800: return "Quadra 800"
        case .powerMacG4: return "Power Mac G4"
        }
    }

    private static func helperBinary(app: String, binary: String) -> URL? {
        let url = Bundle.main.bundleURL
            .appendingPathComponent("Contents/Helpers/\(app).app/Contents/MacOS/\(binary)")
        if FileManager.default.isExecutableFile(atPath: url.path) {
            return url
        }
        return nil
    }

    static var qemuSystemBinary: URL {
        if let helper = helperBinary(app: helperAppName(for: .quadra800), binary: "qemu-system-m68k") {
            return helper
        }
        return developmentBuildDir.appendingPathComponent("qemu-system-m68k")
    }

    static var qemuSystemPPCBinary: URL {
        if let helper = helperBinary(app: helperAppName(for: .powerMacG4), binary: "qemu-system-ppc") {
            return helper
        }
        return developmentBuildDir.appendingPathComponent("qemu-system-ppc")
    }

    // The emulator binary for a machine family.
    static func qemuBinary(for family: MachineFamily) -> URL {
        switch family {
        case .quadra800: return qemuSystemBinary
        case .powerMacG4: return qemuSystemPPCBinary
        }
    }

    static var qemuImgBinary: URL {
        if let helper = helperBinary(app: helperAppName(for: .quadra800), binary: "qemu-img") {
            return helper
        }
        return developmentBuildDir.appendingPathComponent("qemu-img")
    }

    static var pcBiosDir: URL {
        let bundled = resourcesDir.appendingPathComponent("qemu/pc-bios")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }
        return developmentRepoRoot.appendingPathComponent("vendor/qemu/pc-bios")
    }

    static var quadraROM: URL {
        let bundled = resourcesDir.appendingPathComponent("Quadra800.rom")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }
        return developmentRepoRoot.appendingPathComponent("Resources/Quadra800.rom")
    }

    // classicvirtio NuBus declaration ROM that enables host folder sharing.
    static var declROM: URL {
        let bundled = resourcesDir.appendingPathComponent("declrom")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }
        return developmentRepoRoot.appendingPathComponent("shared/declrom")
    }

    // classicvirtio PowerPC driver loader. Injected into guest RAM with QEMU's
    // generic loader device and run by OpenBIOS (boot-command=init-program go);
    // it installs the virtio NDRVs and then continues the normal Mac OS boot.
    static var ndrvLoader: URL {
        let bundled = resourcesDir.appendingPathComponent("ndrvloader")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }
        return developmentRepoRoot.appendingPathComponent("shared/ndrvloader")
    }

    // The "ClassicMac Tools" guest additions CD (HFS image with StuffIt
    // Expander, USB Overdrive, Disk Copy, ...). Built by
    // scripts/build-guest-cd.sh and bundled into Resources; nil when absent
    // so the UI can hide the Insert button.
    static var toolsCD: URL? {
        let bundled = resourcesDir.appendingPathComponent("ClassicMacTools.iso")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }
        let dev = developmentRepoRoot.appendingPathComponent("dist/ClassicMacTools.iso")
        if FileManager.default.fileExists(atPath: dev.path) {
            return dev
        }
        return nil
    }

    // A PRAM image that has been through one normal boot, so its signature is
    // valid. New VMs start from this so the virtio declaration ROM does not hang
    // the boot (a fresh, all-zero PRAM does).
    static var pramSeed: URL {
        let bundled = resourcesDir.appendingPathComponent("pram-seed.img")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }
        return developmentRepoRoot.appendingPathComponent("shared/pram-seed.img")
    }

    // MARK: Development fallbacks

    // When run via `swift run` from app/, the working directory is app/, so the
    // repo root is one level up and the QEMU build lives in vendor/qemu/build.
    private static var developmentRepoRoot: URL {
        if let override = ProcessInfo.processInfo.environment["CLASSICMAC_REPO"] {
            return URL(fileURLWithPath: override)
        }
        return URL(fileURLWithPath: FileManager.default.currentDirectoryPath).deletingLastPathComponent()
    }

    private static var developmentBuildDir: URL {
        developmentRepoRoot.appendingPathComponent("vendor/qemu/build")
    }

    // MARK: Application Support

    static var supportDir: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        let dir = base.appendingPathComponent("ClassicMac", isDirectory: true)
        ensureDirectory(dir)
        return dir
    }

    static var mediaDir: URL {
        let dir = supportDir.appendingPathComponent("Media", isDirectory: true)
        ensureDirectory(dir)
        return dir
    }

    // JSON index of known .classic VM packages (their file paths). The packages
    // themselves live wherever the user chose; this just records the library.
    static var libraryIndexURL: URL {
        supportDir.appendingPathComponent("library.json")
    }

    // Default place new VM packages are created and legacy VMs are migrated to.
    static var defaultLibraryDir: URL {
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first!
        let dir = docs.appendingPathComponent("ClassicMac", isDirectory: true)
        ensureDirectory(dir)
        return dir
    }

    // MARK: Legacy (pre-.classic) storage, kept only for one-time migration.

    static var legacyVMsDir: URL {
        supportDir.appendingPathComponent("VMs", isDirectory: true)
    }

    static func legacyVMDir(for id: UUID) -> URL {
        legacyVMsDir.appendingPathComponent(id.uuidString, isDirectory: true)
    }

    static func ensureDirectory(_ url: URL) {
        if !FileManager.default.fileExists(atPath: url.path) {
            try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        }
    }

    // MARK: Health checks

    static var qemuIsAvailable: Bool {
        FileManager.default.isExecutableFile(atPath: qemuSystemBinary.path)
    }

    static func qemuIsAvailable(for family: MachineFamily) -> Bool {
        FileManager.default.isExecutableFile(atPath: qemuBinary(for: family).path)
    }

    static var romIsAvailable: Bool {
        FileManager.default.fileExists(atPath: quadraROM.path)
    }

    // Firmware each machine family loads at launch. The Quadra boots the Apple
    // ROM (plus the qfb declaration ROM from pc-bios); the Power Mac boots
    // OpenBIOS and additionally needs the VGA option ROM and the Mac OS video
    // driver that OpenBIOS passes to the guest.
    static func requiredFirmware(for family: MachineFamily) -> [URL] {
        switch family {
        case .quadra800:
            return [quadraROM, pcBiosDir.appendingPathComponent("mac_qfb.rom")]
        case .powerMacG4:
            return [
                pcBiosDir.appendingPathComponent("openbios-ppc"),
                pcBiosDir.appendingPathComponent("vgabios-stdvga.bin"),
                pcBiosDir.appendingPathComponent("qemu_vga.ndrv")
            ]
        }
    }

    // Names of any firmware files missing for the family; empty means good to go.
    static func missingFirmware(for family: MachineFamily) -> [String] {
        requiredFirmware(for: family)
            .filter { !FileManager.default.fileExists(atPath: $0.path) }
            .map { $0.lastPathComponent }
    }
}
