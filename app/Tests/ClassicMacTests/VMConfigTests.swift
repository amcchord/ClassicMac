import XCTest
@testable import ClassicMac

final class VMConfigTests: XCTestCase {
    func testDiskSizeChoicesReach120GBForBothMachines() {
        for family in MachineFamily.allCases {
            XCTAssertEqual(family.diskSizePresets.last, 120)
            XCTAssertTrue(family.diskSizePresets.contains(64))
        }
    }

    func testCustomResolutionIsClampedAndNameIsTrimmed() {
        let config = VMConfig(
            name: "  Studio Mac  ",
            machineFamily: .powerMacG4,
            ramMB: 1_024,
            width: 5_000,
            height: 100,
            customResolution: true
        )

        XCTAssertEqual(config.name, "Studio Mac")
        XCTAssertEqual(config.ramMB, 896)
        XCTAssertEqual(config.width, VMConfig.maxWidth)
        XCTAssertEqual(config.height, VMConfig.minHeight)
        XCTAssertFalse(config.useEnhancedFramebuffer)
    }

    func testStandardQuadraNormalizesToSupportedPresetAndDepth() {
        let config = VMConfig(
            name: "Quadra",
            machineFamily: .quadra800,
            width: 1_300,
            height: 1_000,
            depth: ColorDepth.millions.rawValue,
            useEnhancedFramebuffer: false,
            customResolution: true
        )

        XCTAssertFalse(config.customResolution)
        XCTAssertEqual(config.width, 1_280)
        XCTAssertEqual(config.height, 1_024)
        XCTAssertEqual(config.depth, ColorDepth.greys256.rawValue)
    }

    func testBlankNameFallsBackToModelDefault() {
        let config = VMConfig(name: "  \n", machineFamily: .powerMacG4)

        XCTAssertEqual(config.name, MachineFamily.powerMacG4.defaultName)
    }

    func testNativeWindowIsDefaultAndBrowserDisplayRoundTrips() throws {
        let native = VMConfig(name: "Native Window")
        XCTAssertFalse(native.useBrowserDisplay)

        let browser = VMConfig(
            name: "Browser Viewer",
            useBrowserDisplay: true
        )
        let decoded = try JSONDecoder().decode(
            VMConfig.self,
            from: JSONEncoder().encode(browser)
        )
        XCTAssertTrue(decoded.useBrowserDisplay)
    }

    func testConfigWithoutViewerSettingMigratesToNativeWindow() throws {
        let encoded = try JSONEncoder().encode(
            VMConfig(name: "Legacy", useBrowserDisplay: true)
        )
        var object = try XCTUnwrap(
            JSONSerialization.jsonObject(with: encoded) as? [String: Any]
        )
        object.removeValue(forKey: "useBrowserDisplay")
        let legacyData = try JSONSerialization.data(withJSONObject: object)

        let decoded = try JSONDecoder().decode(VMConfig.self, from: legacyData)
        XCTAssertFalse(decoded.useBrowserDisplay)
    }

    func testPowerMacRestrictsStartupDepthToDirectColorModes() {
        let config = VMConfig(
            name: "Power Mac",
            machineFamily: .powerMacG4,
            depth: ColorDepth.greys256.rawValue
        )

        XCTAssertEqual(config.depth, ColorDepth.thousands.rawValue)
        XCTAssertEqual(config.resolutionLabel, "1024x768x16")
        XCTAssertTrue(config.useG4CPU)
        XCTAssertTrue(config.toolsCDInserted)
    }

    func testQuadraNeverEnablesPowerPCG4CPU() {
        let config = VMConfig(name: "Quadra", machineFamily: .quadra800, useG4CPU: true)

        XCTAssertFalse(config.useG4CPU)
    }

    func testCopiedMediaNamesNeverReuseAnExistingFile() throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }

        let first = directory.appendingPathComponent("Install.iso")
        try Data().write(to: first)

        let candidate = VMStore.uniqueFileURL(
            in: directory,
            suggestedName: "Install.iso"
        )

        XCTAssertEqual(candidate.lastPathComponent, "Install 2.iso")
    }

    func testLegacyPowerMacToolsSettingMigratesOnOnce() throws {
        let legacy = VMConfig(
            name: "Power Mac",
            machineFamily: .powerMacG4,
            toolsCDInserted: false
        )
        let encoded = try JSONEncoder().encode(legacy)
        var object = try XCTUnwrap(
            JSONSerialization.jsonObject(with: encoded) as? [String: Any]
        )
        object.removeValue(forKey: "toolsDeliveryVersion")
        object["toolsCDInserted"] = false
        let legacyData = try JSONSerialization.data(withJSONObject: object)

        let migrated = try JSONDecoder().decode(VMConfig.self, from: legacyData)
        XCTAssertTrue(migrated.toolsCDInserted)

        var explicitOff = migrated
        explicitOff.toolsCDInserted = false
        let roundTrip = try JSONDecoder().decode(
            VMConfig.self,
            from: JSONEncoder().encode(explicitOff)
        )
        XCTAssertFalse(roundTrip.toolsCDInserted)
    }

    func testLegacyQuadraToolsSettingIsNotForcedOn() throws {
        let legacy = VMConfig(
            name: "Quadra",
            machineFamily: .quadra800,
            toolsCDInserted: false
        )
        let encoded = try JSONEncoder().encode(legacy)
        var object = try XCTUnwrap(
            JSONSerialization.jsonObject(with: encoded) as? [String: Any]
        )
        object.removeValue(forKey: "toolsDeliveryVersion")
        let legacyData = try JSONSerialization.data(withJSONObject: object)

        let migrated = try JSONDecoder().decode(VMConfig.self, from: legacyData)
        XCTAssertFalse(migrated.toolsCDInserted)
    }
}
