import Foundation
import Darwin

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
        case .powerMacG4, .pippin: return "Power Mac G4"
        case .powerMacOSX: return "Power Mac G4 for OS X"
        case .powerMac7500: return "Power Mac 7500"
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
        case .powerMacG4, .pippin: return qemuSystemPPCBinary
        case .powerMacOSX:
            return helperBinary(app: helperAppName(for: family), binary: "qemu-system-ppc") ??
                developmentRepoRoot.appendingPathComponent("vendor/qemu-osx/build/qemu-system-ppc")
        case .powerMac7500:
            return helperBinary(app: "Power Mac 7500", binary: "dingusppc") ??
                developmentRepoRoot.appendingPathComponent("vendor/dingusppc/build/bin/dingusppc.app/Contents/MacOS/dingusppc")
        }
    }

    static var pippinROM: URL {
        supportDir.appendingPathComponent("ROMs", isDirectory: true).appendingPathComponent("pippin.rom")
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

    static var osxFirmwareDir: URL {
        let bundled = resourcesDir.appendingPathComponent("qemu-osx")
        if FileManager.default.fileExists(atPath: bundled.path) {
            return bundled
        }
        return developmentRepoRoot.appendingPathComponent("osx/firmware")
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

    // Browser display assets are copied into the release bundle. Development
    // runs load the tracked source directory directly so `swift run` and the
    // tests do not need a partially assembled app bundle.
    static var browserAssetsDir: URL {
        let bundled = resourcesDir.appendingPathComponent("Browser")
        if FileManager.default.fileExists(
            atPath: bundled.appendingPathComponent("index.html").path
        ) {
            return bundled
        }
        return developmentRepoRoot.appendingPathComponent("browser")
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

    // MARK: Private runtime sockets

    // Maximum socket filename inside runtimeSocketDir is "<UUID>.qmp.sock" / ".vnc.sock" (45 bytes).
    // sockaddr_un.sun_path is 104 bytes including NUL terminator, leaving 57 bytes max for the dir path.
    private static let maxSocketDirectoryBytes = 104 - 1 - 45 - 1

    static var runtimeSocketDir: URL {
        let uid = getuid()
        let primary = URL(fileURLWithPath: "/tmp/ClassicMac-\(uid)", isDirectory: true)
        let tmpRoot = URL(fileURLWithPath: NSTemporaryDirectory(), isDirectory: true).standardizedFileURL
        let fallback = tmpRoot.appendingPathComponent("cm-\(uid)", isDirectory: true)
        return verifiedRuntimeSocketDir(primary: primary, fallback: fallback, uid: uid)
    }

    static func verifiedRuntimeSocketDir(primary: URL, fallback: URL, uid: uid_t = getuid()) -> URL {
        if let verified = verifiedPrivateDirectory(at: primary, uid: uid),
           verified.path.utf8.count <= maxSocketDirectoryBytes {
            return verified
        }
        let parent = fallback.deletingLastPathComponent()
        if !FileManager.default.fileExists(atPath: parent.path) {
            try? FileManager.default.createDirectory(at: parent, withIntermediateDirectories: true)
        }
        if fallback.path.utf8.count <= maxSocketDirectoryBytes,
           let verifiedFallback = verifiedPrivateDirectory(at: fallback, uid: uid) {
            return verifiedFallback
        }
        if parent.path.utf8.count <= maxSocketDirectoryBytes,
           let verifiedParent = verifiedPrivateDirectory(at: parent, uid: uid) {
            return verifiedParent
        }
        let shortPIDDir = URL(fileURLWithPath: "/tmp/cm-\(uid)-\(getpid())", isDirectory: true)
        if let verifiedShort = verifiedPrivateDirectory(at: shortPIDDir, uid: uid) {
            return verifiedShort
        }
        return fallback
    }

    private static func verifiedPrivateDirectory(at url: URL, uid: uid_t) -> URL? {
        let path = url.standardizedFileURL.path
        var st = stat()
        if lstat(path, &st) != 0 {
            if errno != ENOENT { return nil }
            if mkdir(path, 0o700) != 0 && errno != EEXIST {
                return nil
            }
            guard lstat(path, &st) == 0 else { return nil }
        }
        guard (st.st_mode & S_IFMT) == S_IFDIR, st.st_uid == uid else {
            return nil
        }
        if (st.st_mode & 0o777) != 0o700 {
            guard chmod(path, 0o700) == 0,
                  lstat(path, &st) == 0,
                  (st.st_mode & S_IFMT) == S_IFDIR,
                  st.st_uid == uid,
                  (st.st_mode & 0o077) == 0 else {
                return nil
            }
        }
        return URL(fileURLWithPath: path, isDirectory: true)
    }

    // MARK: Health checks

    private static let displayProbeLock = NSLock()
    private static var cachedOSXDisplayProbe: (path: String, supported: Bool)?

    static var osxSupportsClassicMacDisplay: Bool {
        let binary = qemuBinary(for: .powerMacOSX)
        displayProbeLock.lock()
        if let cached = cachedOSXDisplayProbe, cached.path == binary.path {
            displayProbeLock.unlock()
            return cached.supported
        }
        displayProbeLock.unlock()

        let supported = probeClassicMacDisplay(binary: binary)
        displayProbeLock.lock()
        if FileManager.default.isExecutableFile(atPath: binary.path) {
            cachedOSXDisplayProbe = (binary.path, supported)
        }
        displayProbeLock.unlock()
        return supported
    }

    static func probeClassicMacDisplay(binary: URL) -> Bool {
        guard FileManager.default.isExecutableFile(atPath: binary.path) else { return false }
        let process = Process()
        process.executableURL = binary
        process.arguments = ["-display", "help"]
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        do {
            try process.run()
        } catch {
            return false
        }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        guard process.terminationStatus == 0,
              let output = String(data: data, encoding: .utf8) else { return false }
        return output
            .components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .contains("classicmac")
    }

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
        case .powerMac7500, .pippin: return [] // Validated against the machine / user ROM.
        case .quadra800:
            return [quadraROM, pcBiosDir.appendingPathComponent("mac_qfb.rom")]
        case .powerMacOSX:
            return ["openbios-ppc-osx", "macosx-flash-seed.img", "qemu_vga.ndrv"].map {
                osxFirmwareDir.appendingPathComponent($0)
            }
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
