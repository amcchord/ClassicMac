import XCTest
@testable import ClassicMac

final class MediaTests: XCTestCase {
    func testStatusSeparatesEmptyDrivesFromMissingDrivesAndPreservesSourcePath() throws {
        let response = "classicmac-media status\r\nCLASSICMAC_MEDIA {\"liveChanges\":true,\"drives\":[{\"device\":\"cd0\",\"inserted\":true,\"path\":\"/tmp/Café \\\"Install\\\".iso\"},{\"device\":\"fd0\",\"inserted\":false}]}\r\n(qemu) "
        let snapshot = try MediaSnapshot.parse(response)
        XCTAssertTrue(snapshot.liveChanges)
        XCTAssertEqual(snapshot[.disc]?.path, "/tmp/Café \"Install\".iso")
        XCTAssertEqual(snapshot[.floppy]?.inserted, false)
        XCTAssertNil(snapshot[.tools])
    }

    func testStatusRejectsUnsupportedEngineAndMalformedOutput() {
        for response in [nil, "unknown command: classicmac-media", "CLASSICMAC_MEDIA {bad}", "CLASSICMAC_MEDIA {}"] as [String?] {
            XCTAssertThrowsError(try MediaSnapshot.parse(response))
        }
    }

    func testExplicitMonitorFailureIsNeverAcceptedAsSuccess() {
        XCTAssertThrowsError(try MediaSnapshot.parse("CLASSICMAC_MEDIA {\"liveChanges\":true,\"drives\":[],\"error\":\"Drive is busy\"}")) { error in
            XCTAssertEqual(error.localizedDescription, "Drive is busy")
        }
    }

    func testMonitorQuotedPathsCannotInjectCommands() throws {
        XCTAssertEqual(try MediaCommand.change(.disc, path: "/tmp/Folder \\\"; quit.iso"), "classicmac-media insert cd0 \"/tmp/Folder \\\\\\\"; quit.iso\"")
        XCTAssertEqual(try MediaCommand.change(.floppy, path: nil), "classicmac-media eject fd0")
        for path in ["relative.iso", "/tmp/a\nquit\n", "/tmp/a\rquit", "/tmp/\u{0}.img", "/tmp/tab\t.img"] {
            XCTAssertThrowsError(try MediaCommand.quotedPath(path))
        }
    }

    func testEjectingBootDiscClearsStartupChoiceAndDoesNotTouchOtherSettings() {
        let original = VMConfig(name: "Test", machineFamily: .quadra800, ramMB: 128, cdImagePath: "/tmp/Install.iso", bootFromCD: true, floppyImagePath: "/tmp/Floppy.img")
        let updated = MediaDevice.disc.setting(path: nil, in: original)
        XCTAssertNil(updated.cdImagePath)
        XCTAssertFalse(updated.bootFromCD)
        XCTAssertEqual(updated.floppyImagePath, original.floppyImagePath)
        XCTAssertEqual(updated.id, original.id)
        XCTAssertEqual(updated.ramMB, original.ramMB)
        let replaced = MediaDevice.disc.setting(path: "/tmp/Game.iso", in: updated)
        XCTAssertFalse(replaced.bootFromCD, "Inserting a game disc must not silently change the startup device")
    }

    func testMissingDirectoryAndEmptyImagesAreRejected() throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        let empty = dir.appendingPathComponent("empty.img")
        try Data().write(to: empty)
        for path in [dir.path, empty.path, dir.appendingPathComponent("missing.iso").path] {
            XCTAssertThrowsError(try MediaCommand.validateImage(path, device: .disc))
        }
        let valid = dir.appendingPathComponent("disc.iso")
        try Data(repeating: 0, count: 2048).write(to: valid)
        XCTAssertNoThrow(try MediaCommand.validateImage(valid.path, device: .disc))
    }
}

@MainActor
final class MediaStateTests: XCTestCase {
    private var suite: String!
    private var defaults: UserDefaults!
    override func setUp() async throws {
        suite = "MediaTests.\(UUID())"
        defaults = UserDefaults(suiteName: suite)!
    }
    override func tearDown() async throws { defaults.removePersistentDomain(forName: suite) }

    func testRecentsDeduplicateAndBoundHistoryWithoutDroppingMissingFiles() {
        let recents = MediaRecents(defaults: defaults)
        for number in 0..<15 { recents.remember("/tmp/missing-\(number).iso", device: .disc) }
        XCTAssertEqual(recents.items.count, 12)
        recents.remember("/tmp/missing-8.iso", device: .disc)
        XCTAssertEqual(recents.items.count, 12)
        XCTAssertEqual(recents.items.first?.path, "/tmp/missing-8.iso")
        XCTAssertEqual(recents.items.first?.available, false)
        recents.remember("/tmp/Tools.iso", device: .tools)
        XCTAssertEqual(recents.items.count, 12)
        XCTAssertEqual(MediaRecents(defaults: defaults).items, recents.items)
        recents.forget(recents.items[0])
        XCTAssertEqual(recents.items.count, 11)
        recents.clear()
        XCTAssertTrue(MediaRecents(defaults: defaults).items.isEmpty)
    }

    func testRunningPowerMacStagesWithoutContactingUnavailableMonitor() async {
        let controller = MediaController(recents: MediaRecents(defaults: defaults))
        let config = VMConfig(name: "Test", machineFamily: .powerMacG4, ramMB: 512)
        let success = await controller.change(.disc, path: nil, config: config, running: true, paused: false)
        XCTAssertTrue(success)
        XCTAssertNil(controller.snapshots[config.id])
        XCTAssertTrue(controller.messages[config.id]?.contains("next startup") == true)
        XCTAssertFalse(controller.busyIDs.contains(config.id))
    }

    func testPausedQuadraRejectsLiveChangeWithoutSavingOrForcingEject() async {
        let controller = MediaController(recents: MediaRecents(defaults: defaults))
        let config = VMConfig(name: "Test", machineFamily: .quadra800, ramMB: 128)
        let success = await controller.change(.floppy, path: nil, config: config, running: true, paused: true)
        XCTAssertFalse(success)
        XCTAssertTrue(controller.errors[config.id]?.contains("Resume") == true)
        XCTAssertNil(controller.messages[config.id])
        XCTAssertFalse(controller.busyIDs.contains(config.id))
    }
}
