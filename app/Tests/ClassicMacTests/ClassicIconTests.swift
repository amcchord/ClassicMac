import XCTest
import AppKit
import PlatinumKit
@testable import ClassicMac

final class ClassicIconTests: XCTestCase {
    /// Compare shipping PNGs with a separate decoder of the original family.
    /// RGB conversion to premultiplied storage may round a partial-alpha
    /// channel by one; opaque pixels and every alpha sample must be exact.
    @MainActor
    func testNativeMacOSArtworkAndAlphaSurviveExport() throws {
        PlatinumIcons.removeAllOverrides()
        let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let folder = root.appendingPathComponent("platinum/macos-icons")
        let names = ["cd", "disk", "folder", "folder_open", "library", "web", "display", "clipboard",
                     "caution", "stop", "note", "lock", "trash", "harddisk", "preferences", "mac"]
        func pixels(_ image: CGImage) throws -> [UInt8] {
            var p = [UInt8](repeating: 0, count: image.width * image.height * 4)
            try p.withUnsafeMutableBytes { b in
                let cg = try XCTUnwrap(CGContext(data: b.baseAddress, width: image.width, height: image.height,
                    bitsPerComponent: 8, bytesPerRow: image.width * 4,
                    space: CGColorSpace(name: CGColorSpace.sRGB)!,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue))
                cg.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
            }
            return p
        }
        for name in names {
            let family = ClassicIcon.decode(icns: try Data(contentsOf: folder.appendingPathComponent(name + ".icns")))
            for size in [16, 32] {
                let original = try XCTUnwrap(family[size], "\(name)@\(size) original")
                let shipping = try XCTUnwrap(PlatinumIcons.image(name, size: size))
                XCTAssertEqual(shipping.width, size); XCTAssertEqual(shipping.height, size)
                let e = try pixels(original), a = try pixels(shipping)
                for i in stride(from: 0, to: a.count, by: 4) {
                    XCTAssertEqual(a[i+3], e[i+3], "\(name)@\(size) alpha")
                    for c in 0..<3 {
                        XCTAssertLessThanOrEqual(abs(Int(a[i+c])-Int(e[i+c])), e[i+3] == 255 ? 0 : 1,
                                                 "\(name)@\(size) color")
                    }
                }
                if ["web", "lock"].contains(name) || name == "cd" && size == 16 {
                    let alpha = Set(stride(from: 3, to: a.count, by: 4).map { a[$0] })
                    XCTAssertGreaterThan(alpha.count, 2, "Native 8-bit mask must retain intermediate alpha")
                }
            }
        }
    }

    func testBundledMachineIconsContainBothNativeSizes() throws {
        let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let icons = IconPacks.bundledImages(in: root.appendingPathComponent("Resources/Iconfactory"))
        XCTAssertEqual(icons.count, 3)
        for name in ["powermac_g4", "quadra_800", "powermac_7500"] {
            for size in [16, 32] {
                let image = try XCTUnwrap(icons[name]?[size], "Missing bundled \(name)@\(size)")
                XCTAssertEqual(image.width, size)
                XCTAssertEqual(image.height, size)
                let pixels = try XCTUnwrap(image.dataProvider?.data as Data?)
                XCTAssertTrue(stride(from: 3, to: pixels.count, by: 4).contains { pixels[$0] != 0 })
            }
        }
    }

    func testStandardColorTables() {
        XCTAssertEqual(ClassicIcon.palette8.count, 256)
        XCTAssertEqual(ClassicIcon.palette8[0], 0xFFFFFF)
        XCTAssertEqual(ClassicIcon.palette8[1], 0xFFFFCC)
        XCTAssertEqual(ClassicIcon.palette8[214], 0x000033)
        XCTAssertEqual(ClassicIcon.palette8[215], 0xEE0000)
        XCTAssertEqual(ClassicIcon.palette8[245], 0xEEEEEE)
        XCTAssertEqual(ClassicIcon.palette8[255], 0x000000)
        XCTAssertEqual(ClassicIcon.palette4.count, 16)
    }

    /// An icl8 + ICN# family: masked pixels are transparent, the rest use the
    /// standard color table.
    func testDecodesMaskedEightBitIcon() throws {
        var icl8 = Data(repeating: 255, count: 1024)     // black
        icl8[0] = 0                                      // white top-left pixel
        var icnMask = Data(repeating: 0, count: 256)     // 128 icon + 128 mask bytes
        icnMask[128] = 0x80                              // only the first pixel visible
        func element(_ type: String, _ body: Data) -> Data {
            var d = Data(type.utf8)
            withUnsafeBytes(of: UInt32(body.count + 8).bigEndian) { d.append(contentsOf: $0) }
            return d + body
        }
        let members = element("ICN#", icnMask) + element("icl8", icl8)
        var icns = Data("icns".utf8)
        withUnsafeBytes(of: UInt32(members.count + 8).bigEndian) { icns.append(contentsOf: $0) }
        icns += members

        let image = try XCTUnwrap(ClassicIcon.decode(icns: icns)[32])
        XCTAssertEqual(image.width, 32)
        let pixels = try XCTUnwrap(image.dataProvider?.data as Data?)
        XCTAssertEqual(Array(pixels[0..<4]), [255, 255, 255, 255], "visible white pixel")
        XCTAssertEqual(Array(pixels[4..<8]), [0, 0, 0, 0], "masked pixel is transparent")
    }
}
