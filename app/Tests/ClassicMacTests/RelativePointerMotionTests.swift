import XCTest
@testable import ClassicMac

final class RelativePointerMotionTests: XCTestCase {
    func testSlowTrackpadMotionAccumulatesInBothDirections() {
        var motion = RelativePointerMotion()
        var sumX = 0, sumY = 0
        for _ in 0..<100 {
            let delta = motion.take(dx: 0.25, dy: -0.25, scale: 1)
            sumX += delta.x; sumY += delta.y
        }
        XCTAssertEqual(sumX, 25)
        XCTAssertEqual(sumY, -25)
    }
    func testZoomPreservesRemainderAndDirectionReversalsCancel() {
        var motion = RelativePointerMotion()
        XCTAssertEqual(motion.take(dx: 1, dy: -1, scale: 2).x, 0)
        let next = motion.take(dx: 1, dy: -1, scale: 2)
        XCTAssertEqual(next.x, 1); XCTAssertEqual(next.y, -1)
        _ = motion.take(dx: 0.4, dy: 0.4, scale: 1)
        _ = motion.take(dx: -0.4, dy: -0.4, scale: 1)
        XCTAssertEqual(motion.take(dx: 1, dy: 1, scale: 1).x, 1)
    }

    func testLegacyNativeNormalizesByDPIAccumulatesAndClamps() {
        var motion = RelativePointerMotion()
        // Default Auto DPI = 1000 -> ratio = 5.0
        XCTAssertEqual(motion.take(dx: 2, dy: -2, scale: 2, legacyNative: true, dpi: nil).x, 0)
        let second = motion.take(dx: 3, dy: -3, scale: 2, legacyNative: true, dpi: nil)
        XCTAssertEqual(second.x, 1)
        XCTAssertEqual(second.y, -1)

        // Custom DPI = 400 -> ratio = 2.0
        var custom = RelativePointerMotion()
        let delta = custom.take(dx: 10, dy: -6, scale: 1, legacyNative: true, dpi: 400)
        XCTAssertEqual(delta.x, 5)
        XCTAssertEqual(delta.y, -3)

        // Clamped to +/-32767
        var extreme = RelativePointerMotion()
        let clamped = extreme.take(dx: 500_000, dy: -500_000, scale: 1, legacyNative: true, dpi: 200)
        XCTAssertEqual(clamped.x, 32_767)
        XCTAssertEqual(clamped.y, -32_767)
    }
}
