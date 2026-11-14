import XCTest
@testable import ClassicMac

final class GuestBootFrameTests: XCTestCase {
    private let width = 640, height = 480

    func testBlankFramesStayCovered() {
        for color: [UInt8] in [[0, 0, 0], [255, 255, 255], [128, 128, 128], [255, 255, 200]] {
            let bytes = frame(color)
            XCTAssertFalse(ready(bytes, .quadra800))
            XCTAssertFalse(ready(bytes, .powerMacG4))
        }
    }

    func testEmptyROMCheckerboardStaysCovered() {
        var bytes = frame([255, 255, 255])
        for y in 0..<height {
            for x in 0..<width where (x + y).isMultiple(of: 2) { paint(&bytes, x, y, [0, 0, 0]) }
        }
        XCTAssertFalse(ready(bytes, .quadra800))
        // The centered boot icon must lift the curtain even though the old
        // 12x9 sample grid lands entirely outside its 32-pixel width.
        for y in 220..<252 { for x in 304..<336 { paint(&bytes, x, y, [255, 255, 255]) } }
        for y in 224..<248 { for x in 308..<332 { paint(&bytes, x, y, [0, 0, 0]) } }
        XCTAssertTrue(ready(bytes, .quadra800))
    }

    func testSmallPointerOnWhiteStaysCovered() {
        var bytes = frame([255, 255, 255])
        for y in 100..<105 { for x in 140..<143 { paint(&bytes, x, y, [0, 0, 0]) } }
        XCTAssertFalse(ready(bytes, .quadra800))
    }

    func testWelcomeAndDesktopFramesReveal() {
        for family: MachineFamily in [.quadra800, .powerMacG4] {
            var bytes = frame([170, 170, 170])
            for y in 160..<320 { for x in 160..<480 { paint(&bytes, x, y, [255, 255, 255]) } }
            XCTAssertTrue(ready(bytes, family))
        }
    }

    func testOpenBIOSWithTextStaysCovered() {
        var bytes = frame([255, 255, 200])
        for y in 10..<30 { for x in 10..<300 { paint(&bytes, x, y, [0, 0, 0]) } }
        XCTAssertFalse(ready(bytes, .powerMacG4))
    }

    private func frame(_ rgb: [UInt8]) -> [UInt8] {
        Array(repeating: [rgb[2], rgb[1], rgb[0], 0], count: width * height).flatMap { $0 }
    }

    private func paint(_ bytes: inout [UInt8], _ x: Int, _ y: Int, _ rgb: [UInt8]) {
        let offset = (y * width + x) * 4
        bytes.replaceSubrange(offset..<(offset + 3), with: [rgb[2], rgb[1], rgb[0]])
    }

    private func ready(_ bytes: [UInt8], _ family: MachineFamily) -> Bool {
        bytes.withUnsafeBytes { GuestBootFrame.isReady($0.baseAddress!, width: width, height: height,
                                                     stride: width * 4, family: family) }
    }
}
