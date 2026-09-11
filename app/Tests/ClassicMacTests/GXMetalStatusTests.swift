import XCTest
@testable import ClassicMac

final class GXMetalStatusTests: XCTestCase {
    private func snapshot(
        renderer: String = "metal", commands: UInt64 = 10,
        contexts: UInt32 = 1, draws: UInt64 = 3, age: Int64 = 20,
        faulted: Bool = false, error: UInt32 = 0
    ) -> GXMetalStatusSnapshot {
        GXMetalStatusSnapshot(schema: 1, protocol: 1, renderer: renderer,
                              completedCommands: commands, activeContexts: contexts,
                              successfulDraws: draws, lastDrawAgeMs: age,
                              faulted: faulted, errorCode: error)
    }

    private func status(_ snapshot: GXMetalStatusSnapshot?) -> GXMetalStatus {
        .resolve(snapshot: snapshot, isPowerMac: true, isRunning: true,
                 isPaused: false)
    }

    private func hmp(_ payload: String) throws -> String {
        let escaped = String(data: try JSONEncoder().encode(payload), encoding: .utf8)!
        return "qom-get / gxmetal-status\r\n\(escaped)\r\n(qemu) "
    }

    func testParsesActualHMPStringEnvelope() throws {
        let payload = #"{"schema":1,"protocol":65536,"renderer":"metal","completedCommands":4294967295,"activeContexts":1,"successfulDraws":77,"lastDrawAgeMs":29,"faulted":false,"errorCode":0}"#
        let result = try XCTUnwrap(GXMetalStatusSnapshot.parseHMP(hmp(payload)))
        XCTAssertEqual(result.protocol, 65536)
        XCTAssertEqual(result.protocolVersion, "1.0")
        XCTAssertEqual(result.completedCommands, 4_294_967_295)
        XCTAssertEqual(result.successfulDraws, 77)
        XCTAssertEqual(status(result), .accelerating)
    }

    func testUnknownMalformedAndMissingTelemetryRemainUnavailable() throws {
        let good = #"{"schema":1,"protocol":1,"renderer":"metal","completedCommands":10,"activeContexts":1,"successfulDraws":77,"lastDrawAgeMs":29,"faulted":false,"errorCode":0}"#
        for payload in [good.replacingOccurrences(of: #""schema":1"#, with: #""schema":2"#),
                        good.replacingOccurrences(of: "metal", with: "unknown"),
                        good.replacingOccurrences(of: #""lastDrawAgeMs":29"#, with: #""lastDrawAgeMs":-4"#),
                        #"{"renderer":"metal"}"#,
                        "{incomplete"] {
            XCTAssertNil(GXMetalStatusSnapshot.parseHMP(try hmp(payload)))
        }
        XCTAssertNil(GXMetalStatusSnapshot.parseHMP("Property 'gxmetal-status' not found\r\n(qemu) "))
        XCTAssertEqual(status(nil), .unavailable)
    }

    func testHostMetalCapabilityDoesNotClaimGuestDriverContact() {
        let unused = snapshot(commands: 0, contexts: 0, draws: 0, age: -1)
        XCTAssertEqual(status(unused), .waitingForGuest)
        XCTAssertFalse(unused.guestHasContacted)
        XCTAssertEqual(unused.guestDetail, "Guest: no GXMetal commands yet")
    }

    func testRenderingIndicatorExpiresAndRequiresLiveContext() {
        XCTAssertEqual(status(snapshot(age: 1_999)), .accelerating)
        XCTAssertEqual(status(snapshot(age: 2_000)), .ready)
        XCTAssertEqual(status(snapshot(age: 90_000)), .ready)
        XCTAssertEqual(status(snapshot(contexts: 0, age: 1)), .ready)
        XCTAssertEqual(status(snapshot(draws: 0, age: -1)), .ready)
    }

    func testFaultOverridesRecentRenderingAndSoftwareIsDistinct() {
        XCTAssertEqual(status(snapshot(faulted: true)), .faulted)
        XCTAssertEqual(status(snapshot(error: 8)), .faulted)
        XCTAssertEqual(status(snapshot(renderer: "software")), .softwareFallback)
        XCTAssertEqual(status(snapshot(renderer: "software", commands: 0,
                                       contexts: 0, draws: 0, age: -1)), .softwareFallback)
    }

    func testLifecycleOverridesOldRenderingEvidence() {
        XCTAssertEqual(GXMetalStatus.resolve(snapshot: snapshot(), isPowerMac: true,
                                             isRunning: false, isPaused: false), .stopped)
        XCTAssertEqual(GXMetalStatus.resolve(snapshot: snapshot(), isPowerMac: true,
                                             isRunning: true, isPaused: true), .paused)
        XCTAssertEqual(GXMetalStatus.resolve(snapshot: snapshot(), isPowerMac: false,
                                             isRunning: true, isPaused: false), .unsupported)
        XCTAssertEqual(GXMetalStatus.resolve(snapshot: nil, isPowerMac: true,
                                             isRunning: true, isPaused: false,
                                             hasChecked: false), .checking)
    }
}
