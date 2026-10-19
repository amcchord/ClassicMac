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
}
