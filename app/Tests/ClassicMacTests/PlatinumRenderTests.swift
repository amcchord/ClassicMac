import XCTest
import SwiftUI
import AppKit
@testable import PlatinumKit

/// Renders Platinum views to PNG for visual review when
/// PLATINUM_RENDER_DIR is set; otherwise checks they render at all.
@MainActor
final class PlatinumRenderTests: XCTestCase {
    private func bitmap(_ w: Int, _ h: Int, scale: Int = 1,
                        draw: (PixelPainter, PixelRect) -> Void) throws -> CGImage {
        let cg = try XCTUnwrap(CGContext(data: nil, width: w * scale, height: h * scale,
            bitsPerComponent: 8, bytesPerRow: w * scale * 4,
            space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue))
        cg.translateBy(x: 0, y: CGFloat(h * scale)); cg.scaleBy(x: CGFloat(scale), y: CGFloat(-scale))
        let p = PixelPainter(cg), r = PixelRect(0, 0, w, h)
        p.fill(r, Platinum.face); draw(p, r)
        return try XCTUnwrap(cg.makeImage())
    }

    private func rgba(_ image: CGImage) throws -> [UInt8] {
        var pixels = [UInt8](repeating: 0, count: image.width * image.height * 4)
        try pixels.withUnsafeMutableBytes { buffer in
            let cg = try XCTUnwrap(CGContext(data: buffer.baseAddress, width: image.width, height: image.height,
                bitsPerComponent: 8, bytesPerRow: image.width * 4,
                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue))
            cg.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        }
        return pixels
    }

    private func assertReference(_ actual: CGImage, name: String, scale: Int,
                                 file: StaticString = #filePath, line: UInt = #line) throws {
        let url = try XCTUnwrap(Bundle.module.url(forResource: name, withExtension: "png", subdirectory: "Platinum"))
        let source = try XCTUnwrap(CGImageSourceCreateWithURL(url as CFURL, nil))
        let expected = try XCTUnwrap(CGImageSourceCreateImageAtIndex(source, 0, nil))
        XCTAssertEqual(actual.width, expected.width * scale, file: file, line: line)
        XCTAssertEqual(actual.height, expected.height * scale, file: file, line: line)
        let a = try rgba(actual), e = try rgba(expected)
        var differences = 0
        for y in 0..<actual.height { for x in 0..<actual.width {
            let ai = (y * actual.width + x) * 4
            let ei = ((y / scale) * expected.width + x / scale) * 4
            if a[ai..<ai+4] != e[ei..<ei+4] { differences += 1 }
        } }
        XCTAssertEqual(differences, 0, "\(name) at \(scale)x: differing device pixels", file: file, line: line)
        if let dir = ProcessInfo.processInfo.environment["PLATINUM_RENDER_DIR"] {
            let url = URL(fileURLWithPath: dir).appendingPathComponent("\(name)-\(scale)x.png")
            let dest = try XCTUnwrap(CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil))
            CGImageDestinationAddImage(dest, actual, nil); XCTAssertTrue(CGImageDestinationFinalize(dest))
        }
    }

    func testAppearanceControlPixels() throws {
        for scale in [1, 2] {
            for (suffix, state) in [("normal", ControlState.normal), ("pressed", .pressed), ("disabled", .disabled)] {
                try assertReference(bitmap(80, 20, scale: scale) { Chrome.button($0, $1, state: state) },
                    name: "button-" + suffix, scale: scale)
                try assertReference(bitmap(86, 26, scale: scale) {
                    Chrome.defaultRing($0, around: $1.inset(3), disabled: state == .disabled)
                    Chrome.button($0, $1.inset(3), state: state)
                }, name: "default-" + suffix, scale: scale)
                for on in [false, true] {
                    try assertReference(bitmap(14, 12, scale: scale) { p, _ in
                        Chrome.checkbox(p, x: 0, y: 0, on: on, state: state)
                    }, name: "checkbox-\(on ? "on" : "off")-" + suffix, scale: scale)
                    try assertReference(bitmap(12, 12, scale: scale) { p, _ in
                        Chrome.radio(p, x: 0, y: 0, on: on, state: state)
                    }, name: "radio-\(on ? "on" : "off")-" + suffix, scale: scale)
                }
                try assertReference(bitmap(100, 20, scale: scale) { Chrome.popup($0, $1, state: state) },
                    name: "popup-" + suffix, scale: scale)
            }
        }
    }

    func testAdditionalAppearancePixels() throws {
        for scale in [1, 2] {
            for (suffix, state) in [("normal", ControlState.normal), ("pressed", .pressed), ("disabled", .disabled)] {
                try assertReference(bitmap(14, 12, scale: scale) { p, _ in
                    Chrome.checkboxMixed(p, x: 0, y: 0, state: state)
                }, name: "checkbox-mixed-" + suffix, scale: scale)
            }
            try assertReference(bitmap(250, 100, scale: scale) { Chrome.groupBox($0, $1, labelWidth: 0) },
                                name: "group", scale: scale)
            try assertReference(bitmap(256, 56, scale: scale) { Chrome.focusRing($0, around: $1.inset(2)) },
                                name: "focus", scale: scale)
            try assertReference(bitmap(104, 26, scale: scale) { Chrome.sunken($0, $1.inset(1), fill: nil) },
                                name: "sunken", scale: scale)
            for (i, value) in [0.3, 0.7].enumerated() {
                try assertReference(bitmap(250, 14, scale: scale) {
                    Chrome.progress($0, $1.inset(1), value: value, phase: 0, disabled: false)
                }, name: "progress-\(i)", scale: scale)
            }
            for (name, kind) in [("close", Chrome.TitleBox.close), ("zoom", .zoom), ("collapse", .collapse)] {
                try assertReference(bitmap(13, 13, scale: scale) { p, _ in
                    Chrome.titleBox(p, x: 0, y: 0, kind: kind, pressed: false)
                }, name: "os8-" + name, scale: scale)
            }
        }
    }

    func testSystemFontPixelsAndAdvances() throws {
        let url = try XCTUnwrap(Bundle.module.url(forResource: "system-advances", withExtension: "json", subdirectory: "Platinum"))
        let advances = try JSONDecoder().decode([Int].self, from: Data(contentsOf: url))
        for (i, advance) in advances.enumerated() {
            XCTAssertEqual(PlatinumFonts.width(String(UnicodeScalar(i + 32)!), font: .system), CGFloat(advance))
        }
        for scale in [1, 2] {
            let image = try bitmap(256, 120, scale: scale) { p, r in
                p.fill(r, Platinum.white)
                for code in 32..<127 {
                    let i = code - 32
                    p.text(String(UnicodeScalar(code)!), font: .system, x: i % 16 * 16,
                           baseline: i / 16 * 20 + 14, color: Platinum.black)
                }
            }
            try assertReference(image, name: "system-ascii", scale: scale)
        }
    }

    func testNativeDocumentWindowPixels() throws {
        for scale in [1, 2] {
            for (name, active, collapsed) in [("active", true, false), ("inactive", false, false), ("collapsed", true, true)] {
                try assertReference(bitmap(333, collapsed ? 23 : 169, scale: scale) { p, r in
                    Chrome.window(p, r, kind: .document, active: active, titleWidth: 70,
                                  boxes: .document, collapsed: collapsed, growable: true)
                    if active {
                        Chrome.titleBox(p, x: 4, y: 4, kind: .close, pressed: false)
                        Chrome.titleBox(p, x: 299, y: 4, kind: .zoom, pressed: false)
                        Chrome.titleBox(p, x: 315, y: 4, kind: .collapse, pressed: false)
                    }
                    if active && !collapsed { Chrome.growBox(p, x: 311, y: 147, active: true) }
                }, name: "os9-window-" + name, scale: scale)
            }
        }
    }

    func testNativeVMToolbarAndHeldTitleBoxes() throws {
        for scale in [1, 2] {
            for (name, state) in [("normal", ControlState.normal), ("pressed", .pressed), ("disabled", .disabled)] {
                try assertReference(bitmap(100, 22, scale: scale) { Chrome.smallBevel($0, $1, state: state) },
                                    name: "os9-bevel-" + name, scale: scale)
            }
            for (name, kind) in [("close", Chrome.TitleBox.close), ("zoom", .zoom), ("collapse", .collapse)] {
                try assertReference(bitmap(13, 13, scale: scale) { p, _ in
                    Chrome.titleBox(p, x: 0, y: 0, kind: kind, pressed: true)
                }, name: "os9-" + name + "-pressed", scale: scale)
            }
        }
    }

    func testGuestContentCannotCoverNativeGrowBox() throws {
        for scale in [1, 2] {
            let state = PlatinumWindowState(title: "ClassicMac")
            let view = PlatinumWindowChrome(state: state, kind: .document, boxes: .document, growable: true) {
                Color.red
            }.frame(width: 333, height: 169).environment(\.platinumStaticRender, true)
            let renderer = ImageRenderer(content: view); renderer.scale = CGFloat(scale)
            let whole = try XCTUnwrap(renderer.cgImage)
            let grow = try XCTUnwrap(whole.cropping(to: CGRect(x: 311 * scale, y: 147 * scale,
                                                              width: 15 * scale, height: 15 * scale)))
            try assertReference(grow, name: "os9-grow", scale: scale)
        }
    }

    func render<V: View>(_ view: V, name: String) throws -> CGImage {
        let renderer = ImageRenderer(content: view.environment(\.platinumStaticRender, true))
        renderer.scale = 2
        let image = try XCTUnwrap(renderer.cgImage, "\(name) did not render")
        if let dir = ProcessInfo.processInfo.environment["PLATINUM_RENDER_DIR"] {
            let url = URL(fileURLWithPath: dir).appendingPathComponent("\(name).png")
            let dest = try XCTUnwrap(CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil))
            CGImageDestinationAddImage(dest, image, nil)
            XCTAssertTrue(CGImageDestinationFinalize(dest))
        }
        return image
    }

    func testSwiftUITextRenders() throws {
        for scale in [1.0, 2.0] {
            let renderer = ImageRenderer(content: Text("ClassicMac Cancel Shut Down Name:")
                .font(.platinumSystem).foregroundStyle(.black).padding(4).background(.white))
            renderer.scale = scale
            let image = try XCTUnwrap(renderer.cgImage)
            if let dir = ProcessInfo.processInfo.environment["PLATINUM_RENDER_DIR"] {
                let url = URL(fileURLWithPath: dir).appendingPathComponent("font-swiftui-\(Int(scale)).png")
                let dest = try XCTUnwrap(CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil))
                CGImageDestinationAddImage(dest, image, nil); XCTAssertTrue(CGImageDestinationFinalize(dest))
            }
        }
    }

    func testFontsRegister() {
        PlatinumFonts.registerIfNeeded()
        for font in PlatinumFont.allCases {
            let ct = font.ctFont()
            XCTAssertEqual(CTFontCopyPostScriptName(ct) as String, font.rawValue)
        }
        XCTAssertEqual(PlatinumFonts.width("Mac OS", font: .system), PlatinumFonts.width("Mac OS", font: .system).rounded())
    }

    func testGalleryRenders() throws {
        let image = try render(PlatinumGalleryWindow(), name: "gallery")
        XCTAssertEqual(image.width, (656 + 13) * 2)
    }

    func testAlertRenders() throws {
        let alert = PlatinumAlert(icon: .caution, message: "Shut down “Mac OS 9.2.2 Deluxe”?",
                                  informative: "Mac OS shuts down first, as if you chose Shut Down from the Special menu. If it does not respond within 15 seconds, the Mac turns off and unsaved work may be lost.",
                                  buttons: [PlatinumAlertButton("Shut Down", role: .default),
                                            PlatinumAlertButton("Cancel", role: .cancel)]) {}
        let state = PlatinumWindowState(title: "")
        _ = try render(PlatinumWindowChrome(state: state, kind: .modal) { alert }.frame(width: 413, height: 150), name: "alert")
    }

    func testMenuRenders() throws {
        let items = [PlatinumMenu.Item(title: "640 x 480"), PlatinumMenu.Item(title: "800 x 600"),
                     PlatinumMenu.Item(title: "1024 x 768", checked: true), PlatinumMenu.Item(title: "", separator: true),
                     PlatinumMenu.Item(title: "Custom…", enabled: false)]
        let view = PlatinumMenu.MenuView(items: items)
        view.frame = NSRect(x: 0, y: 0, width: view.naturalWidth + 2, height: view.naturalHeight + 2)
        view.highlighted = 1
        let rep = try XCTUnwrap(view.bitmapImageRepForCachingDisplay(in: view.bounds))
        view.cacheDisplay(in: view.bounds, to: rep)
        if let dir = ProcessInfo.processInfo.environment["PLATINUM_RENDER_DIR"] {
            try rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: dir).appendingPathComponent("menu.png"))
        }
        XCTAssertEqual(view.item(at: NSPoint(x: 30, y: CGFloat(PlatinumMenu.topMargin + 16 + 4))), 1)
        XCTAssertNil(view.item(at: NSPoint(x: 30, y: CGFloat(PlatinumMenu.topMargin + 3 * 16 + 2))), "separators are not choosable")
    }
}
