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
        if let bundleResources = bundleResources, FileManager.default.fileExists(atPath: bundleResources.appendingPathComponent("qemu/qemu-system-m68k").path) {
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

    static var qemuSystemBinary: URL {
        let bundled = resourcesDir.appendingPathComponent("qemu/qemu-system-m68k")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }
        return developmentBuildDir.appendingPathComponent("qemu-system-m68k")
    }

    static var qemuImgBinary: URL {
        let bundled = resourcesDir.appendingPathComponent("qemu/qemu-img")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
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

    static var romIsAvailable: Bool {
        FileManager.default.fileExists(atPath: quadraROM.path)
    }
}
