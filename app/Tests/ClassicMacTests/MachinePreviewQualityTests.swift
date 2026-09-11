import AppKit
import XCTest
@testable import ClassicMac

final class MachinePreviewQualityTests: XCTestCase {
    func testUniformNeutralShutdownScreensAreNotUseful() {
        for level in [0.0, 0.5, 1.0] {
            XCTAssertFalse(MachinePreviewQuality.isUseful(makeImage(background: .init(white: level, alpha: 1))))
        }
    }

    func testShutdownScreenWithSmallPointerIsNotUseful() {
        let image = makeImage(background: .gray) { context in
            context.setFillColor(NSColor.black.cgColor)
            context.fill(CGRect(x: 140, y: 100, width: 3, height: 5))
        }
        XCTAssertFalse(MachinePreviewQuality.isUseful(image))
    }

    func testSparseMonochromeDesktopWithMenuBarIsUseful() {
        let image = makeImage(background: .lightGray) { context in
            context.setFillColor(NSColor.white.cgColor)
            context.fill(CGRect(x: 0, y: 220, width: 320, height: 20))
            context.setFillColor(NSColor.black.cgColor)
            context.fill(CGRect(x: 0, y: 219, width: 320, height: 1))
        }
        XCTAssertTrue(MachinePreviewQuality.isUseful(image))
    }

    func testColorFramesAreRetained() {
        XCTAssertTrue(MachinePreviewQuality.isUseful(makeImage(background: .systemBlue)))
    }

    func testEmptyImageIsNotUseful() {
        XCTAssertFalse(MachinePreviewQuality.isUseful(NSImage(size: .zero)))
    }

    private func makeImage(background: NSColor, draw: (CGContext) -> Void = { _ in }) -> NSImage {
        let context = CGContext(
            data: nil, width: 320, height: 240, bitsPerComponent: 8,
            bytesPerRow: 320 * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        )!
        context.setFillColor(background.cgColor)
        context.fill(CGRect(x: 0, y: 0, width: 320, height: 240))
        draw(context)
        return NSImage(cgImage: context.makeImage()!, size: NSSize(width: 320, height: 240))
    }
}
