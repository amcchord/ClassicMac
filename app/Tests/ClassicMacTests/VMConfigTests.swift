import XCTest
import Darwin
@testable import ClassicMac

final class VMConfigTests: XCTestCase {
    func testVirtioStartupIsExplicitAndLimitedToPowerMac() throws {
        let encoder = JSONEncoder(), decoder = JSONDecoder()
        var legacy = try XCTUnwrap(JSONSerialization.jsonObject(with: encoder.encode(
            VMConfig(name: "Legacy", machineFamily: .powerMacG4))) as? [String: Any])
        legacy.removeValue(forKey: "useVirtioDisk")
        XCTAssertFalse(try decoder.decode(VMConfig.self, from: JSONSerialization.data(withJSONObject: legacy)).useVirtioDisk)
        for family in MachineFamily.allCases {
            let config = VMConfig(name: "Template", machineFamily: family, useVirtioDisk: true)
            XCTAssertEqual(try decoder.decode(VMConfig.self, from: encoder.encode(config)).useVirtioDisk,
                           family == .powerMacG4)
        }
    }

    func testDiskSizeChoicesReach120GBForBothMachines() {
        for family in [MachineFamily.quadra800, .powerMacG4] {
            XCTAssertEqual(family.diskSizePresets.last, 120)
            XCTAssertTrue(family.diskSizePresets.contains(64))
        }
    }

    func testCoplandControlWriteAfterGuestExitThrowsInsteadOfSignalling() throws {
        let pipe = CoplandMachine.controlPipe()
        XCTAssertEqual(fcntl(pipe.fileHandleForWriting.fileDescriptor, F_GETNOSIGPIPE), 1)
        try pipe.fileHandleForReading.close()
        XCTAssertThrowsError(try pipe.fileHandleForWriting.write(contentsOf: Data("continue\n".utf8)))
        try pipe.fileHandleForWriting.close()
    }

    func testCoplandPinsHardwareAndClearsUnsupportedFeatures() throws {
        let config = VMConfig(name: "Copland", machineFamily: .powerMac7500,
                              ramMB: 1024, width: 1920, height: 1080, depth: 24,
                              customResolution: true, useBrowserDisplay: true,
                              cdImagePath: "/tmp/disc.iso", bootFromCD: true,
                              toolsCDInserted: true, sharedFolderPath: "/tmp/shared")
        let decoded = try JSONDecoder().decode(VMConfig.self, from: JSONEncoder().encode(config))
        XCTAssertEqual(decoded.ramMB, 32)
        XCTAssertEqual(decoded.width, 640); XCTAssertEqual(decoded.height, 480)
        XCTAssertEqual(decoded.depth, 8)
        XCTAssertFalse(decoded.networking); XCTAssertFalse(decoded.sound)
        XCTAssertFalse(decoded.useBrowserDisplay); XCTAssertFalse(decoded.toolsCDInserted)
        XCTAssertFalse(decoded.bootFromCD); XCTAssertFalse(decoded.tabletInput)
        XCTAssertNil(decoded.cdImagePath); XCTAssertNil(decoded.sharedFolderPath)
        XCTAssertNil(decoded.floppyImagePath)
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

    func testDepth32MapsToMillionsColorDepthWithoutDowngrading() throws {
        let json = """
        {
          "id": "\(UUID().uuidString)",
          "name": "Cross Platform Bundle",
          "machineFamily": "powerMacG4",
          "depth": 32
        }
        """
        let decoded = try JSONDecoder().decode(VMConfig.self, from: Data(json.utf8))
        XCTAssertEqual(decoded.depth, ColorDepth.millions.rawValue)

        let constructed = VMConfig(name: "Direct", machineFamily: .quadra800, depth: 32)
        XCTAssertEqual(constructed.depth, ColorDepth.millions.rawValue)
    }

    func testFormattedOSLabelMatchesAllFamiliesAndVersions() {
        XCTAssertEqual(VMConfig.formattedOSLabel("10.4.11", family: .powerMacOSX), "Mac OS X 10.4.11")
        XCTAssertEqual(VMConfig.formattedOSLabel("10.4.11", family: .powerMacG4), "Mac OS X 10.4.11")
        XCTAssertEqual(VMConfig.formattedOSLabel("7.5.5", family: .quadra800), "System 7.5.5")
        XCTAssertEqual(VMConfig.formattedOSLabel("8.1", family: .quadra800), "Mac OS 8.1")
        XCTAssertEqual(VMConfig.formattedOSLabel("9.2.2", family: .powerMacG4), "Mac OS 9.2.2")
        XCTAssertEqual(VMConfig.formattedOSLabel("D11E4", family: .powerMac7500), "D11E4")
        XCTAssertEqual(VMConfig.formattedOSLabel("Mac OS 9.2.1", family: .powerMacG4), "Mac OS 9.2.1")
        XCTAssertEqual(VMConfig.formattedOSLabel("6.0.8", family: .quadra800), "6.0.8")
    }

    @MainActor
    func testVMStoreRetainsUnavailablePathsAcrossReloads() throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("vmstore-unavail-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let indexURL = root.appendingPathComponent("library.json")
        let mediaDir = root.appendingPathComponent("Media", isDirectory: true)
        try FileManager.default.createDirectory(at: mediaDir, withIntermediateDirectories: true)

        let presentBundle = root.appendingPathComponent("Present.classic", isDirectory: true)
        let removableBundle = root.appendingPathComponent("External.classic", isDirectory: true)
        let store = VMStore(indexURL: indexURL, mediaDir: mediaDir, migrateLegacy: false)

        let vm1 = VMConfig(name: "Present", machineFamily: .powerMacG4, bundleURL: presentBundle)
        let vm2 = VMConfig(name: "External", machineFamily: .powerMacG4, bundleURL: removableBundle)
        XCTAssertTrue(store.save(vm1))
        XCTAssertTrue(store.save(vm2))
        XCTAssertEqual(store.vms.count, 2)

        // Simulate unmounted drive by temporarily moving the external bundle away.
        let parked = root.appendingPathComponent("Parked.classic", isDirectory: true)
        try FileManager.default.moveItem(at: removableBundle, to: parked)
        store.reload()

        XCTAssertEqual(store.vms.map(\.id), [vm1.id])
        XCTAssertEqual(store.unavailablePaths, [removableBundle.standardizedFileURL.path])
        let savedIndex = try JSONDecoder().decode([String].self, from: Data(contentsOf: indexURL))
        XCTAssertTrue(savedIndex.contains(removableBundle.standardizedFileURL.path))

        // Re-mount and reload: the missing VM returns automatically.
        try FileManager.default.moveItem(at: parked, to: removableBundle)
        store.reload()
        XCTAssertEqual(Set(store.vms.map(\.id)), [vm1.id, vm2.id])
        XCTAssertTrue(store.unavailablePaths.isEmpty)
    }

    @MainActor
    func testVMStoreOpenBundleAssignsNewUUIDForFinderDuplicate() throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("vmstore-dup-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let indexURL = root.appendingPathComponent("library.json")
        let mediaDir = root.appendingPathComponent("Media", isDirectory: true)
        let originalDir = root.appendingPathComponent("Original.classic", isDirectory: true)
        let duplicateDir = root.appendingPathComponent("Original copy.classic", isDirectory: true)
        let store = VMStore(indexURL: indexURL, mediaDir: mediaDir, migrateLegacy: false)

        let originalID = UUID()
        let vm = VMConfig(id: originalID, name: "Original", machineFamily: .powerMacG4, bundleURL: originalDir)
        XCTAssertTrue(store.save(vm))
        try FileManager.default.copyItem(at: originalDir, to: duplicateDir)

        store.openBundle(at: duplicateDir, autostart: false)
        XCTAssertNil(store.lastError)
        XCTAssertEqual(store.vms.count, 2)

        let openedDuplicate = try XCTUnwrap(store.vms.first {
            $0.folder.standardizedFileURL.path == duplicateDir.standardizedFileURL.path
        })
        XCTAssertNotEqual(openedDuplicate.id, originalID)
        XCTAssertEqual(store.selectedID, openedDuplicate.id)

        let persisted = try JSONDecoder().decode(
            VMConfig.self,
            from: Data(contentsOf: duplicateDir.appendingPathComponent("config.json"))
        )
        XCTAssertEqual(persisted.id, openedDuplicate.id)

        // Re-opening the same path keeps its UUID stable.
        store.openBundle(at: duplicateDir, autostart: false)
        XCTAssertEqual(store.selectedID, openedDuplicate.id)
        XCTAssertEqual(store.vms.count, 2)
    }

    @MainActor
    func testVMStoreSanitizedBundleNameAndTemplateOSCache() throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("vmstore-cache-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let indexURL = root.appendingPathComponent("library.json")
        let mediaDir = root.appendingPathComponent("Media", isDirectory: true)
        let store = VMStore(indexURL: indexURL, mediaDir: mediaDir, migrateLegacy: false)

        XCTAssertEqual(store.sanitizedBundleName("..hidden"), "hidden")
        XCTAssertEqual(store.sanitizedBundleName("..."), "Machine")
        XCTAssertEqual(store.uniqueBundleURL(in: root, name: "..Mac").lastPathComponent, "Mac.classic")

        let bundle = root.appendingPathComponent("Cached.classic", isDirectory: true)
        let vm = VMConfig(name: "Cached", machineFamily: .powerMacG4, bundleURL: bundle)
        XCTAssertTrue(store.save(vm))
        XCTAssertNil(store.templateOSVersion(for: vm))

        let machine = DownloadableMachine(
            id: "os922-deluxe-v6", name: "Mac OS 9.2.2", summary: "Summary",
            osVersion: "9.2.2", minimumAppVersion: "4.0.0",
            archiveURL: URL(string: "https://example.com/a.tar.gz")!,
            archiveBytes: 100, installedBytes: 1000,
            sha256: String(repeating: "a", count: 64)
        )
        let infoURL = bundle.appendingPathComponent(VMTemplateMetadata.fileName)
        try JSONEncoder().encode(VMTemplateMetadata(machine: machine)).write(to: infoURL)

        // Negative cache holds until save/reload invalidates it.
        XCTAssertNil(store.templateOSVersion(for: vm))
        store.reload()
        XCTAssertEqual(store.templateOSVersion(for: vm), "9.2.2")

        // Positive cache avoids disk reads until invalidated.
        try FileManager.default.removeItem(at: infoURL)
        XCTAssertEqual(store.templateOSVersion(for: vm), "9.2.2")
        XCTAssertTrue(store.save(vm))
        XCTAssertNil(store.templateOSVersion(for: vm))
    }

    @MainActor
    func testVMStoreMoveToTrashValidatesBundleAndCleansUnreferencedMedia() throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("vmstore-trash-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let indexURL = root.appendingPathComponent("library.json")
        let mediaDir = root.appendingPathComponent("Media", isDirectory: true)
        try FileManager.default.createDirectory(at: mediaDir, withIntermediateDirectories: true)
        let store = VMStore(indexURL: indexURL, mediaDir: mediaDir, migrateLegacy: false)

        // 1. Symlinked bundle must be rejected.
        let realTarget = root.appendingPathComponent("TargetDir", isDirectory: true)
        try FileManager.default.createDirectory(at: realTarget, withIntermediateDirectories: true)
        let symlinkBundle = root.appendingPathComponent("Symlink.classic")
        XCTAssertEqual(symlink(realTarget.path, symlinkBundle.path), 0)
        let symVM = VMConfig(name: "Symlink", machineFamily: .powerMacG4, bundleURL: symlinkBundle)
        try JSONEncoder().encode(symVM).write(to: realTarget.appendingPathComponent("config.json"))
        store.moveToTrash(symVM)
        XCTAssertNotNil(store.lastError)
        XCTAssertTrue(FileManager.default.fileExists(atPath: realTarget.path))
        store.lastError = nil

        // 2. Non-.classic directory must be rejected.
        let nonClassic = root.appendingPathComponent("NotClassic", isDirectory: true)
        try FileManager.default.createDirectory(at: nonClassic, withIntermediateDirectories: true)
        let nonClassicVM = VMConfig(name: "Bad", machineFamily: .powerMacG4, bundleURL: nonClassic)
        try JSONEncoder().encode(nonClassicVM).write(to: nonClassic.appendingPathComponent("config.json"))
        store.moveToTrash(nonClassicVM)
        XCTAssertNotNil(store.lastError)
        XCTAssertTrue(FileManager.default.fileExists(atPath: nonClassic.path))
        store.lastError = nil

        // 3. Ancestor of another VM must be rejected.
        let parentBundle = root.appendingPathComponent("Parent.classic", isDirectory: true)
        let childBundle = parentBundle.appendingPathComponent("Child.classic", isDirectory: true)
        let parentVM = VMConfig(name: "Parent", machineFamily: .powerMacG4, bundleURL: parentBundle)
        let childVM = VMConfig(name: "Child", machineFamily: .powerMacG4, bundleURL: childBundle)
        XCTAssertTrue(store.save(parentVM))
        XCTAssertTrue(store.save(childVM))
        store.moveToTrash(parentVM)
        XCTAssertNotNil(store.lastError)
        XCTAssertTrue(FileManager.default.fileExists(atPath: childBundle.path))
        store.lastError = nil

        // 4. Valid bundle cleans unreferenced mediaDir files, keeps shared & external media.
        let sharedMedia = mediaDir.appendingPathComponent("shared.iso")
        let orphanMedia = mediaDir.appendingPathComponent("orphan.iso")
        let externalMedia = root.appendingPathComponent("external.iso")
        try Data([1]).write(to: sharedMedia)
        try Data([2]).write(to: orphanMedia)
        try Data([3]).write(to: externalMedia)

        let keepBundle = root.appendingPathComponent("Keep.classic", isDirectory: true)
        let keepVM = VMConfig(name: "Keep", machineFamily: .powerMacG4, cdImagePath: sharedMedia.path, bundleURL: keepBundle)
        XCTAssertTrue(store.save(keepVM))

        let deleteBundle = root.appendingPathComponent("Delete.classic", isDirectory: true)
        let deleteVM = VMConfig(
            name: "Delete", machineFamily: .quadra800,
            cdImagePath: orphanMedia.path, floppyImagePath: sharedMedia.path,
            bundleURL: deleteBundle
        )
        XCTAssertTrue(store.save(deleteVM))

        store.moveToTrash(deleteVM)
        XCTAssertNil(store.lastError)
        XCTAssertFalse(FileManager.default.fileExists(atPath: deleteBundle.path))
        XCTAssertFalse(FileManager.default.fileExists(atPath: orphanMedia.path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: sharedMedia.path))

        // External media outside AppPaths.mediaDir must never be deleted.
        let extBundle = root.appendingPathComponent("ExternalMedia.classic", isDirectory: true)
        let extVM = VMConfig(
            name: "ExternalMedia", machineFamily: .powerMacG4,
            cdImagePath: externalMedia.path, bundleURL: extBundle
        )
        XCTAssertTrue(store.save(extVM))
        store.moveToTrash(extVM)
        XCTAssertNil(store.lastError)
        XCTAssertFalse(FileManager.default.fileExists(atPath: extBundle.path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: externalMedia.path))
    }

    @MainActor
    func testVMStoreCreateVMAsyncCreatesOrRollsBackCleanly() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("vmstore-async-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let store = VMStore(
            indexURL: root.appendingPathComponent("library.json"),
            mediaDir: root.appendingPathComponent("Media", isDirectory: true),
            migrateLegacy: false
        )
        let bundle = root.appendingPathComponent("Async.classic", isDirectory: true)
        let config = VMConfig(name: "Async Mac", machineFamily: .powerMacG4, bundleURL: bundle)

        if FileManager.default.isExecutableFile(atPath: AppPaths.qemuImgBinary.path) {
            let created = await store.createVMAsync(config)
            XCTAssertNotNil(created)
            XCTAssertTrue(FileManager.default.fileExists(atPath: config.diskImageURL.path))
            XCTAssertTrue(FileManager.default.fileExists(atPath: config.configURL.path))
            XCTAssertEqual(store.selectedID, config.id)
        } else {
            let created = await store.createVMAsync(config)
            XCTAssertNil(created)
            XCTAssertNotNil(store.lastError)
            XCTAssertFalse(FileManager.default.fileExists(atPath: bundle.path))
        }
    }

    func testQuadraBootFromFloppyDefaultsSanitizesAndRoundTrips() throws {
        let encoder = JSONEncoder(), decoder = JSONDecoder()
        let q = VMConfig(
            name: "Quadra Floppy",
            machineFamily: .quadra800,
            bootFromCD: false,
            floppyImagePath: "/tmp/DiskTools.img",
            bootFromFloppy: true
        )
        let decoded = try decoder.decode(VMConfig.self, from: encoder.encode(q))
        XCTAssertTrue(decoded.bootFromFloppy)

        let conflict = VMConfig(
            name: "Conflict",
            machineFamily: .quadra800,
            cdImagePath: "/tmp/CD.iso",
            bootFromCD: true,
            floppyImagePath: "/tmp/DiskTools.img",
            bootFromFloppy: true
        )
        XCTAssertFalse(conflict.bootFromFloppy)

        let nonQuadra = VMConfig(
            name: "G4",
            machineFamily: .powerMacG4,
            bootFromCD: false,
            floppyImagePath: "/tmp/DiskTools.img",
            bootFromFloppy: true
        )
        XCTAssertFalse(nonQuadra.bootFromFloppy)
    }
}
