import AppKit
import PlatinumKit

/// Classic Macintosh artwork by The Iconfactory. The bundled machine icons
/// are included under separately granted permission. Imported collections
/// take precedence, and removing imports restores the bundled defaults.
@MainActor
final class IconPacks: ObservableObject {
    static let shared = IconPacks()

    static let website = URL(string: "https://freeware.iconfactory.com")!

    /// Collections on this computer (folder names).
    @Published private(set) var collections: [String] = []
    /// ClassicMac icon name → the Iconfactory icon now drawn for it.
    @Published private(set) var activeIcons: [String: String] = [:]
    @Published private(set) var isImporting = false

    /// Iconfactory icons for each ClassicMac icon, best match first. Names
    /// are the collections' folder names (":" is "/" in the Finder).
    static let choices: [(icon: String, label: String, sources: [String])] = [
        ("powermac_g4", "Power Mac G4", ["G4 Side List View", "G4 Copland List View", "G4 Cube", "G3 miniTower"]),
        ("quadra_800", "Quadra 800", ["800:8000 series", "Quadra900", "Quadra700"]),
        ("powermac_7500", "Power Mac 7500", ["7200+series:G3 Desktop", "650:7100", "Performa:PowerMac 6000 series"]),
    ]

    static var directory: URL {
        let dir = AppPaths.supportDir.appendingPathComponent("Icons", isDirectory: true)
        AppPaths.ensureDirectory(dir)
        return dir
    }

    var isUsingIconfactory: Bool { !activeIcons.isEmpty }

    // MARK: Loading

    /// Finds the collections on this computer and applies their icons.
    func load() {
        let bundled = Self.bundledImages()
        let fm = FileManager.default
        let packs = ((try? fm.contentsOfDirectory(at: Self.directory, includingPropertiesForKeys: [.isDirectoryKey])) ?? [])
            .filter { (try? $0.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
        collections = packs.map(\.lastPathComponent)

        // Every folder with a custom icon file ("Icon\r") is a candidate, by
        // its folder name. The icon file itself is invisible to enumerators.
        var available: [String: URL] = [:]
        for pack in packs {
            guard let walker = fm.enumerator(at: pack, includingPropertiesForKeys: [.isDirectoryKey]) else { continue }
            for case let folder as URL in walker
            where (try? folder.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true {
                let icon = folder.appendingPathComponent("Icon\r")
                if available[folder.lastPathComponent] == nil, fm.fileExists(atPath: icon.path) {
                    available[folder.lastPathComponent] = icon
                }
            }
        }

        var active: [String: String] = [:]
        for choice in Self.choices {
            var applied = false
            for source in choice.sources {
                guard let file = available[source], let images = Self.images(fromIconFile: file),
                      let big = images[32] else { continue }
                PlatinumIcons.setOverride(big, name: choice.icon, size: 32)
                PlatinumIcons.setOverride(images[16], name: choice.icon, size: 16)
                active[choice.icon] = source.replacingOccurrences(of: ":", with: "/")
                applied = true
                break
            }
            if !applied {
                PlatinumIcons.setOverride(bundled[choice.icon]?[32], name: choice.icon, size: 32)
                PlatinumIcons.setOverride(bundled[choice.icon]?[16], name: choice.icon, size: 16)
                if bundled[choice.icon]?[32] != nil {
                    active[choice.icon] = "The Iconfactory (included)"
                }
            }
        }
        activeIcons = active
    }

    /// Plain icns files preserve the artists' pixels without resource-fork
    /// dependencies, so the same assets can travel with other platforms.
    nonisolated static func bundledImages(in directory: URL? = nil) -> [String: [Int: CGImage]] {
        let directory = directory ?? AppPaths.resourcesDir.appendingPathComponent("Iconfactory")
        var result: [String: [Int: CGImage]] = [:]
        for name in ["powermac_g4", "quadra_800", "powermac_7500"] {
            guard let data = try? Data(contentsOf: directory.appendingPathComponent(name + ".icns")) else { continue }
            result[name] = ClassicIcon.decode(icns: data)
        }
        return result
    }

    // MARK: Adding and removing

    /// Copies Iconfactory collections (their downloaded disk images, or
    /// folders) into ClassicMac's icon folder, then applies them.
    func add(_ urls: [URL]) async throws {
        isImporting = true
        defer { isImporting = false; load() }
        for url in urls {
            if url.pathExtension.lowercased() == "dmg" {
                try await Self.importDiskImage(url)
            } else {
                try await Self.copyCollection(from: url, named: url.lastPathComponent)
            }
        }
    }

    func removeAll() {
        let fm = FileManager.default
        for item in (try? fm.contentsOfDirectory(at: Self.directory, includingPropertiesForKeys: nil)) ?? [] {
            try? fm.removeItem(at: item)
        }
        PlatinumIcons.removeAllOverrides()
        load()
    }

    private static func importDiskImage(_ dmg: URL) async throws {
        let mount = FileManager.default.temporaryDirectory
            .appendingPathComponent("ClassicMac-Icons-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: mount, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: mount) }
        try await run("/usr/bin/hdiutil", ["attach", "-readonly", "-nobrowse", "-noautoopen", "-quiet",
                                          "-mountpoint", mount.path, dmg.path])
        do {
            let name = (try? mount.resourceValues(forKeys: [.volumeNameKey]).volumeName)
                ?? dmg.deletingPathExtension().lastPathComponent
            try await copyCollection(from: mount, named: name)
        } catch {
            try? await run("/usr/bin/hdiutil", ["detach", "-quiet", "-force", mount.path])
            throw error
        }
        try? await run("/usr/bin/hdiutil", ["detach", "-quiet", "-force", mount.path])
    }

    /// Copies a collection with ditto, which keeps the classic resource forks
    /// that hold the icons, along with the license and Read Me.
    private static func copyCollection(from source: URL, named name: String) async throws {
        let fm = FileManager.default
        guard collectionHasIcons(source) else {
            throw IconPackError("\u{201C}\(name)\u{201D} doesn\u{2019}t contain classic Mac icons from The Iconfactory.")
        }
        let safe = name.replacingOccurrences(of: "/", with: "-").trimmingCharacters(in: .whitespaces)
        let destination = directory.appendingPathComponent(safe.isEmpty ? "Icons" : safe, isDirectory: true)
        if fm.fileExists(atPath: destination.path) { try fm.removeItem(at: destination) }
        try await run("/usr/bin/ditto", ["--rsrc", "--extattr", source.path, destination.path])
        for junk in [".DS_Store", ".fseventsd", ".Trashes", ".Spotlight-V100"] {
            try? fm.removeItem(at: destination.appendingPathComponent(junk))
        }
    }

    private static func collectionHasIcons(_ folder: URL) -> Bool {
        let fm = FileManager.default
        guard let walker = fm.enumerator(at: folder, includingPropertiesForKeys: [.isDirectoryKey]) else { return false }
        var checked = 0
        for case let url as URL in walker {
            checked += 1
            if checked > 2_000 { break }
            if url.lastPathComponent == "Icon\r", images(fromIconFile: url)?[32] != nil { return true }
            if (try? url.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true,
               let images = images(fromIconFile: url.appendingPathComponent("Icon\r")), images[32] != nil {
                return true
            }
        }
        return false
    }

    private static func run(_ tool: String, _ arguments: [String]) async throws {
        try await Task.detached(priority: .userInitiated) {
            let process = Process()
            process.executableURL = URL(fileURLWithPath: tool)
            process.arguments = arguments
            process.standardOutput = FileHandle.nullDevice
            process.standardError = FileHandle.nullDevice
            try process.run()
            process.waitUntilExit()
            guard process.terminationStatus == 0 else {
                throw IconPackError("The icons could not be copied (\(URL(fileURLWithPath: tool).lastPathComponent) \(process.terminationStatus)).")
            }
        }.value
    }

    // MARK: Reading classic icons

    /// The 32- and 16-pixel images of a folder's custom icon file: the 'icns'
    /// resource in the file's resource fork.
    nonisolated static func images(fromIconFile file: URL) -> [Int: CGImage]? {
        let fork = file.appendingPathComponent("..namedfork/rsrc")
        guard let data = try? Data(contentsOf: fork), let icns = ResourceFork(data)?.first(type: "icns") else {
            return nil
        }
        let images = ClassicIcon.decode(icns: icns)
        return images.isEmpty ? nil : images
    }
}

/// Decodes the classic members of an 'icns' family exactly: true-color
/// il32/is32 with their 8-bit masks when present, otherwise the 8-bit icl8/ics8
/// (or 4-bit icl4/ics4, or 1-bit) image through the Mac OS standard color table
/// with the ICN#/ics# mask, so every pixel is the artist's.
enum ClassicIcon {
    static func decode(icns: Data) -> [Int: CGImage] {
        let elements = parse(icns)
        var result: [Int: CGImage] = [:]
        let sizes: [(size: Int, rgb: String, rgbMask: String, eight: String, four: String, bits: String)] = [
            (32, "il32", "l8mk", "icl8", "icl4", "ICN#"),
            (16, "is32", "s8mk", "ics8", "ics4", "ics#"),
        ]
        for spec in sizes {
            if let rgb = elements[spec.rgb], let mask = elements[spec.rgbMask],
               let image = trueColor(rgb: rgb, mask: mask, size: spec.size) {
                result[spec.size] = image
            } else if let bits = elements[spec.bits] {
                let n = spec.size * spec.size
                let mask = bits.count >= n / 4 ? Data(bits.suffix(n / 8)) : nil
                let alpha = elements[spec.rgbMask]
                if let eight = elements[spec.eight], eight.count >= n {
                    result[spec.size] = indexed(eight, depth: 8, mask: mask, alphaMask: alpha, size: spec.size)
                } else if let four = elements[spec.four], four.count >= n / 2 {
                    result[spec.size] = indexed(four, depth: 4, mask: mask, alphaMask: alpha, size: spec.size)
                } else {
                    result[spec.size] = indexed(Data(bits.prefix(n / 8)), depth: 1, mask: mask, size: spec.size)
                }
            }
        }
        return result
    }

    private static func parse(_ icns: Data) -> [String: Data] {
        var out: [String: Data] = [:]
        let bytes = [UInt8](icns)
        var o = 8
        while o + 8 <= bytes.count {
            let type = String(bytes: bytes[o..<o + 4], encoding: .macOSRoman) ?? ""
            let length = Int(bytes[o + 4]) << 24 | Int(bytes[o + 5]) << 16 | Int(bytes[o + 6]) << 8 | Int(bytes[o + 7])
            guard length >= 8, o + length <= bytes.count else { break }
            out[type] = Data(bytes[(o + 8)..<(o + length)])
            o += length
        }
        return out
    }

    /// il32/is32: three run-length-encoded channel planes; the mask is 8-bit.
    private static func trueColor(rgb: Data, mask: Data, size: Int) -> CGImage? {
        let n = size * size
        var source = [UInt8](rgb)
        if source.count >= 4 && source[0] == 0 && source[1] == 0 && source[2] == 0 && source[3] == 0 {
            source.removeFirst(4)   // it32-style header, never in il32 but harmless
        }
        var planes = [UInt8]()
        if source.count == n * 3 {
            planes = source
        } else {
            var i = 0
            while i < source.count && planes.count < n * 3 {
                let c = Int(source[i]); i += 1
                if c < 0x80 {
                    let run = c + 1
                    guard i + run <= source.count else { break }
                    planes += source[i..<i + run]; i += run
                } else {
                    guard i < source.count else { break }
                    planes += [UInt8](repeating: source[i], count: c - 0x80 + 3); i += 1
                }
            }
        }
        guard planes.count >= n * 3, mask.count >= n else { return nil }
        let alpha = [UInt8](mask)
        var pixels = [UInt8](repeating: 0, count: n * 4)
        for p in 0..<n {
            let a = Int(alpha[p])
            pixels[p * 4] = UInt8(Int(planes[p]) * a / 255)
            pixels[p * 4 + 1] = UInt8(Int(planes[n + p]) * a / 255)
            pixels[p * 4 + 2] = UInt8(Int(planes[2 * n + p]) * a / 255)
            pixels[p * 4 + 3] = UInt8(a)
        }
        return image(pixels, size: size)
    }

    private static func indexed(_ data: Data, depth: Int, mask: Data?, alphaMask: Data? = nil, size: Int) -> CGImage? {
        let n = size * size
        let src = [UInt8](data), bitmask = mask.map { [UInt8]($0) }
        let alpha = alphaMask.flatMap { $0.count >= n ? [UInt8]($0) : nil }
        var pixels = [UInt8](repeating: 0, count: n * 4)
        for p in 0..<n {
            let index: Int
            switch depth {
            case 8: index = Int(src[p])
            case 4: index = Int(src[p / 2] >> (p % 2 == 0 ? 4 : 0) & 0x0F)
            default: index = Int(src[p / 8] >> (7 - p % 8) & 1)
            }
            let color = depth == 8 ? palette8[index] : depth == 4 ? palette4[index] : (index == 1 ? 0x000000 : 0xFFFFFF)
            let a = Int(alpha?[p] ?? ((bitmask.map { $0[p / 8] >> (7 - p % 8) & 1 == 1 } ?? true) ? 255 : 0))
            guard a > 0 else { continue }
            pixels[p * 4] = UInt8(Int(color >> 16 & 0xFF) * a / 255)
            pixels[p * 4 + 1] = UInt8(Int(color >> 8 & 0xFF) * a / 255)
            pixels[p * 4 + 2] = UInt8(Int(color & 0xFF) * a / 255)
            pixels[p * 4 + 3] = UInt8(a)
        }
        return image(pixels, size: size)
    }

    private static func image(_ pixels: [UInt8], size: Int) -> CGImage? {
        guard let provider = CGDataProvider(data: Data(pixels) as CFData) else { return nil }
        return CGImage(width: size, height: size, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: size * 4,
                       space: CGColorSpace(name: CGColorSpace.sRGB)!,
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }

    /// The Mac OS standard 8-bit color table: the 6x6x6 cube from white,
    /// then ten-step red, green, blue and gray ramps, then black.
    static let palette8: [Int] = {
        var table: [Int] = []
        for i in 0..<215 {
            let r = 5 - i / 36, g = 5 - (i / 6) % 6, b = 5 - i % 6
            table.append(r * 0x33 << 16 | g * 0x33 << 8 | b * 0x33)
        }
        let ramp = [0xEE, 0xDD, 0xBB, 0xAA, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11]
        table += ramp.map { $0 << 16 }
        table += ramp.map { $0 << 8 }
        table += ramp.map { $0 }
        table += ramp.map { $0 << 16 | $0 << 8 | $0 }
        table.append(0x000000)
        return table
    }()

    /// The Mac OS standard 4-bit color table.
    static let palette4: [Int] = [
        0xFFFFFF, 0xFCF305, 0xFF6402, 0xDD0806, 0xF20884, 0x4600A5, 0x0000D4, 0x02ABEA,
        0x1FB714, 0x006411, 0x562C05, 0x90713A, 0xC0C0C0, 0x808080, 0x404040, 0x000000,
    ]
}

struct IconPackError: LocalizedError {
    let message: String
    init(_ message: String) { self.message = message }
    var errorDescription: String? { message }
}

/// A classic Mac OS resource fork, read-only.
struct ResourceFork {
    private let data: Data
    private let dataOffset: Int
    private let mapOffset: Int

    init?(_ data: Data) {
        guard data.count >= 16 else { return nil }
        self.data = data
        dataOffset = Self.u32(data, 0)
        mapOffset = Self.u32(data, 4)
        guard mapOffset + 30 <= data.count, dataOffset < data.count else { return nil }
    }

    /// The first resource of a four-character type.
    func first(type: String) -> Data? {
        let typeList = mapOffset + Self.u16(data, mapOffset + 24)
        guard typeList + 2 <= data.count else { return nil }
        let count = Self.u16(data, typeList) + 1
        for i in 0..<count {
            let entry = typeList + 2 + 8 * i
            guard entry + 8 <= data.count else { return nil }
            guard String(bytes: data[(data.startIndex + entry)..<(data.startIndex + entry + 4)],
                         encoding: .macOSRoman) == type else { continue }
            let reference = typeList + Self.u16(data, entry + 6)
            guard reference + 12 <= data.count else { return nil }
            let start = dataOffset + (Self.u32(data, reference + 4) & 0xFFFFFF)
            guard start + 4 <= data.count else { return nil }
            let length = Self.u32(data, start)
            guard start + 4 + length <= data.count else { return nil }
            return data.subdata(in: (data.startIndex + start + 4)..<(data.startIndex + start + 4 + length))
        }
        return nil
    }

    private static func u32(_ d: Data, _ o: Int) -> Int {
        let b = d.startIndex + o
        return Int(d[b]) << 24 | Int(d[b + 1]) << 16 | Int(d[b + 2]) << 8 | Int(d[b + 3])
    }

    private static func u16(_ d: Data, _ o: Int) -> Int {
        let b = d.startIndex + o
        return Int(d[b]) << 8 | Int(d[b + 1])
    }
}
