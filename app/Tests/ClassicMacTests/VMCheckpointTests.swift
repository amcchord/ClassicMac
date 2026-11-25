import XCTest
@testable import ClassicMac

final class VMCheckpointTests: XCTestCase {
    private func makeBundle(osx: Bool = false) throws -> (root: URL, config: VMConfig) {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("cp-test-\(UUID().uuidString)", isDirectory: true)
        let bundle = root.appendingPathComponent("Test.classic", isDirectory: true)
        try FileManager.default.createDirectory(at: bundle, withIntermediateDirectories: true)

        let family: MachineFamily = osx ? .powerMacOSX : .quadra800
        let config = VMConfig(name: "Test", machineFamily: family, bundleURL: bundle)
        try Data("ORIGINAL-DISK-BYTES".utf8).write(to: config.diskImageURL)
        if osx {
            try OSXReliabilityTests.makeNVRAMData().write(to: bundle.appendingPathComponent("nvram.img"))
        } else {
            try Data(repeating: 0x11, count: 256).write(to: config.pramImageURL)
        }
        return (root, config)
    }

    func testCreateListRestoreAndDeleteCheckpointWithAPFSClone() throws {
        let (root, config) = try makeBundle(osx: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let cp1 = try VMCheckpointStore.create(for: config, label: "Before update")
        XCTAssertEqual(cp1.label, "Before update")
        XCTAssertEqual(cp1.kind, .manual)
        XCTAssertEqual(cp1.method, .apfsClone)

        let listed = VMCheckpointStore.list(in: config.folder)
        XCTAssertEqual(listed.map(\.id), [cp1.id])

        // Mutate disk and NVRAM
        try Data("MODIFIED-DISK-BYTES".utf8).write(to: config.diskImageURL)
        let modifiedNVRAM = OSXReliabilityTests.makeNVRAMData(gen0: 8, offset0: 0, gen1: 9, offset1: 3600)
        try modifiedNVRAM.write(to: config.folder.appendingPathComponent("nvram.img"))

        // Restore automatically creates "Pre-restore backup" first
        _ = try VMCheckpointStore.restore(cp1.id, for: config, createBackup: true)

        XCTAssertEqual(try Data(contentsOf: config.diskImageURL), Data("ORIGINAL-DISK-BYTES".utf8))
        let restoredNVRAM = try Data(contentsOf: config.folder.appendingPathComponent("nvram.img"))
        XCTAssertEqual(try OSXNVRAM.rtcOffset(data: restoredNVRAM), 0)

        let afterRestore = VMCheckpointStore.list(in: config.folder)
        XCTAssertEqual(afterRestore.count, 2)
        XCTAssertTrue(afterRestore.contains { $0.kind == .preRestore && $0.label == "Pre-restore backup" })

        let session = try XCTUnwrap(VMCheckpointStore.readSessionState(in: config.folder))
        XCTAssertTrue(session.clean)

        // Delete pre-restore backup
        let backup = try XCTUnwrap(afterRestore.first { $0.kind == .preRestore })
        try VMCheckpointStore.delete(backup.id, in: config.folder)
        XCTAssertEqual(VMCheckpointStore.list(in: config.folder).map(\.id), [cp1.id])
    }

    func testFreshInstallAndPreRecoveryBackupTracking() throws {
        let (root, config) = try makeBundle(osx: false)
        defer { try? FileManager.default.removeItem(at: root) }

        VMCheckpointStore.createFreshInstallIfSupported(in: config.folder)
        VMCheckpointStore.createFreshInstallIfSupported(in: config.folder) // idempotent
        let fresh = VMCheckpointStore.list(in: config.folder)
        XCTAssertEqual(fresh.count, 1)
        XCTAssertEqual(fresh.first?.kind, .freshInstall)
        XCTAssertEqual(fresh.first?.label, "Fresh install")

        // Simulate dirty exit
        VMCheckpointStore.markStarted(in: config.folder)
        XCTAssertEqual(VMCheckpointStore.readSessionState(in: config.folder)?.clean, false)
        VMCheckpointStore.markStopped(in: config.folder, clean: false, reason: "force-stop")

        // Next start creates one "Pre-recovery backup"
        let recovery = VMCheckpointStore.createPreRecoveryBackupIfNeeded(for: config)
        XCTAssertNotNil(recovery)
        XCTAssertEqual(recovery?.kind, .preRecovery)
        XCTAssertEqual(recovery?.label, "Pre-recovery backup")

        // Second check before clean exit does not spam another backup
        XCTAssertNil(VMCheckpointStore.createPreRecoveryBackupIfNeeded(for: config))

        // Clean shutdown resets recovery flag
        VMCheckpointStore.markStopped(in: config.folder, clean: true, reason: "clean-exit")
        XCTAssertEqual(VMCheckpointStore.readSessionState(in: config.folder)?.clean, true)
        XCTAssertEqual(VMCheckpointStore.readSessionState(in: config.folder)?.recoveryPrompted, false)
    }
}
