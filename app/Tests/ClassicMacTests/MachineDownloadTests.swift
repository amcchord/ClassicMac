import XCTest
import CryptoKit
@testable import ClassicMac

final class MachineDownloadTests: XCTestCase {
    private var directory: URL!
    private let source = URL(string: "https://downloads.example.test/os9.tar.gz")!

    override func setUpWithError() throws {
        directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        MachineDownloadURLProtocol.handler = nil
        try? FileManager.default.removeItem(at: directory)
    }

    func testQualifiedCoplandArchiveImport() throws {
        let env = ProcessInfo.processInfo.environment
        guard let catalogPath = env["COPLAND_TEST_CATALOG"], let archivePath = env["COPLAND_TEST_ARCHIVE"] else {
            throw XCTSkip("Set COPLAND_TEST_CATALOG and COPLAND_TEST_ARCHIVE to qualify a real template")
        }
        let machine = try XCTUnwrap(MachineCatalog.decode(Data(contentsOf: URL(fileURLWithPath: catalogPath))).machines.first { $0.family == .powerMac7500 })
        let archive = URL(fileURLWithPath: archivePath)
        try MachineDownloadTransfer.verify(archive, machine: machine)
        let destination = env["COPLAND_TEST_DESTINATION"].map { URL(fileURLWithPath: $0) } ?? directory!
        let bundle = try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Copland Qualification", in: destination)
        try CoplandMachine.validateFirmware(in: bundle)
        let config = try JSONDecoder().decode(VMConfig.self, from: Data(contentsOf: bundle.appendingPathComponent("config.json")))
        XCTAssertEqual(config.machineFamily, .powerMac7500)
        XCTAssertEqual(config.ramMB, 32)
        XCTAssertFalse(config.networking); XCTAssertFalse(config.toolsCDInserted)
        XCTAssertEqual(try bundle.appendingPathComponent("disk.img").resourceValues(forKeys: [.fileSizeKey]).fileSize, Int(machine.diskCapacityBytes!))
    }

    func testQualifiedClassicBaseArchiveImport() throws {
        let env = ProcessInfo.processInfo.environment
        guard let catalogPath = env["CLASSIC_BASE_TEST_CATALOG"],
              let archivePath = env["CLASSIC_BASE_TEST_ARCHIVE"],
              let id = env["CLASSIC_BASE_TEST_ID"] else {
            throw XCTSkip("Set CLASSIC_BASE_TEST_CATALOG, ARCHIVE and ID to qualify a real template")
        }
        let machine = try XCTUnwrap(MachineCatalog.decode(Data(contentsOf:
            URL(fileURLWithPath: catalogPath))).machines.first { $0.id == id })
        let archive = URL(fileURLWithPath: archivePath)
        try MachineDownloadTransfer.verify(archive, machine: machine)
        let destination = env["CLASSIC_BASE_TEST_DESTINATION"].map { URL(fileURLWithPath: $0) } ?? directory!
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let bundle = try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine,
            name: machine.name, in: destination)
        let config = try JSONDecoder().decode(VMConfig.self,
            from: Data(contentsOf: bundle.appendingPathComponent("config.json")))
        XCTAssertEqual(config.machineFamily, machine.family)
        XCTAssertEqual(config.ramMB, 128)
        XCTAssertFalse(config.toolsCDInserted)
        XCTAssertNil(config.cdImagePath)
        XCTAssertFalse(config.bootFromCD)
        XCTAssertEqual(try bundle.appendingPathComponent("disk.img")
            .resourceValues(forKeys: [.fileSizeKey]).fileSize, Int(machine.diskCapacityBytes!))
        if machine.family == .quadra800 {
            let rom = try Data(contentsOf: bundle.appendingPathComponent("Quadra800.rom"))
            XCTAssertEqual(SHA256.hash(data: rom).map { String(format: "%02x", $0) }.joined(),
                "05ad753fb594e656cf078023ec189e09e2a7655a780de993b75b8c51ed6b09ca")
        }
        XCTAssertEqual(VMTemplateMetadata.load(from: bundle)?.templateID, machine.id)
    }

    func testRetroBootSparseDiskKeepsHolesAndRejectsInvalidMaps() throws {
        let config = Data("{}".utf8)
        let values = ["GNU.sparse.major=1", "GNU.sparse.minor=0",
                      "GNU.sparse.name=disk.img", "GNU.sparse.realsize=4096"]
        let pax = Data(values.map { "\($0.utf8.count + 4) \($0)\n" }.joined().utf8)
        let bytes = Data(repeating: 0x69, count: 512)
        for (mapText, valid) in [("2\n512\n512\n4096\n0\n", true),
                                 ("2\n512\n512\n512\n512\n", false),
                                 ("2\n512\n512\n4608\n0\n", false),
                                 ("1\n512\n1024\n", false)] {
            var map = Data(mapText.utf8)
            map.append(Data(repeating: 0, count: 512 - map.count))
            let archive = try makeArchive([
                Entry("config.json", data: config), Entry("PaxHeaders/disk.img", data: pax, type: 120),
                Entry("GNUSparseFile/disk.img", data: map + bytes),
            ])
            let staging = directory.appendingPathComponent(UUID().uuidString)
            try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: false)
            if valid {
                try MachineTemplateInstaller.extract(archive, into: staging, expectedBytes: 4098,
                    expectedDiskBytes: 4096)
                let installed = try Data(contentsOf: staging.appendingPathComponent("disk.img"))
                XCTAssertEqual(installed, Data(repeating: 0, count: 512) + bytes + Data(repeating: 0, count: 3072))
            } else {
                XCTAssertThrowsError(try MachineTemplateInstaller.extract(archive, into: staging,
                    expectedBytes: 4098, expectedDiskBytes: 4096))
            }
        }
    }

    func testPublicRetroBootImports() async throws {
        guard let fixtures = ProcessInfo.processInfo.environment["RETROBOOT_TEST_FIXTURES"] else {
            throw XCTSkip("Set RETROBOOT_TEST_FIXTURES to qualify the public RetroBoot downloads")
        }
        let fixturesDirectory = URL(fileURLWithPath: fixtures)
        let destination = fixturesDirectory.appendingPathComponent("swift-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: false)
        let catalogFile = directory.appendingPathComponent("catalog.json")
        _ = try await MachineDownloadTransfer(source: MachineCatalog.retroBootURL, destination: catalogFile,
            expectedBytes: nil, maximumBytes: MachineCatalog.maximumCatalogBytes).run()
        let entries = try MachineCatalog.decode(Data(contentsOf: catalogFile)).machines
        for (id, file) in [("mac-os-7.5.5-v1", "system7.thin.tar.gz"),
                           ("os921-full-v4", "os9.thin.tar.gz"),
                           ("mac-os-x-10-4-11-tiger-period-v1", "tiger.thin.tar.gz")] {
            let machine = try XCTUnwrap(entries.first { $0.id == id })
            let archive = machine.family == .quadra800 ? directory.appendingPathComponent(file)
                : fixturesDirectory.appendingPathComponent(file)
            if machine.family == .quadra800 {
                _ = try await MachineDownloadTransfer(source: machine.archiveURL, destination: archive,
                    expectedBytes: machine.archiveBytes, maximumBytes: machine.archiveBytes).run()
            }
            try MachineDownloadTransfer.verify(archive, machine: machine)
            let bundle = try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine,
                name: machine.name, in: destination)
            let config = try JSONDecoder().decode(VMConfig.self,
                from: Data(contentsOf: bundle.appendingPathComponent("config.json")))
            XCTAssertEqual(config.machineFamily, machine.family)
            XCTAssertEqual(try bundle.appendingPathComponent("disk.img").resourceValues(forKeys: [.fileSizeKey]).fileSize,
                           Int(try XCTUnwrap(machine.diskCapacityBytes)))
            XCTAssertEqual(VMTemplateMetadata.load(from: bundle)?.archiveSHA256, machine.sha256)
            if machine.family == .powerMacOSX {
                XCTAssertEqual(try Data(contentsOf: bundle.appendingPathComponent("nvram.img")).count, 16_384)
            }
        }
    }

    func testClassicBaseTemplatesPreserveTheirHardwareAndOptionalMedia() throws {
        for family in [MachineFamily.quadra800, .powerMacG4] {
            let original = VMConfig(name: "Base source", machineFamily: family,
                ramMB: 128, diskSizeGB: 2, width: 640, height: 480, depth: 8,
                cdImagePath: "/private/source.iso", bootFromCD: true,
                toolsCDInserted: false, useG4CPU: false,
                useVirtioDisk: family == .powerMacG4, tabletInput: family == .quadra800)
            let config = try JSONEncoder().encode(original)
            let archive = try makeArchive([Entry("config.json", data: config),
                Entry("disk.img", data: Data(repeating: 0, count: 512))])
            let machine = DownloadableMachine(id: "base-\(family.rawValue.lowercased())", name: "Base Mac",
                summary: "Base OS and Internet applications", osVersion: "Mac OS", minimumAppVersion: "4.6.0",
                archiveURL: source, archiveBytes: Int64(try Data(contentsOf: archive).count),
                installedBytes: Int64(config.count + 512), sha256: String(repeating: "a", count: 64),
                diskCapacityBytes: 512, machineFamily: family)
            XCTAssertEqual(try MachineCatalog.decode(JSONEncoder().encode(
                MachineCatalog(schemaVersion: 1, machines: [machine]))).machines.first?.family, family)
            let bundle = try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine,
                name: family.rawValue, in: directory)
            let fresh = try JSONDecoder().decode(VMConfig.self, from: Data(contentsOf: bundle.appendingPathComponent("config.json")))
            XCTAssertEqual(fresh.machineFamily, family)
            XCTAssertFalse(fresh.toolsCDInserted)
            XCTAssertFalse(fresh.useG4CPU)
            XCTAssertEqual(fresh.useVirtioDisk, family == .powerMacG4)
            XCTAssertEqual(fresh.tabletInput, family == .quadra800)
            XCTAssertNil(fresh.cdImagePath)
            XCTAssertFalse(fresh.bootFromCD)
        }
    }

    func testQuadraFirmwareIsBoundedAndIsolatedFromPowerMacArchives() throws {
        let config = try JSONEncoder().encode(VMConfig(name: "Quadra", machineFamily: .quadra800))
        let rom = Data(repeating: 0, count: 1_048_576)
        let archive = try makeArchive([Entry("config.json", data: config),
            Entry("disk.img", data: Data(repeating: 0, count: 512)), Entry("Quadra800.rom", data: rom)])
        let staging = directory.appendingPathComponent("rom-extract")
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: false)
        XCTAssertThrowsError(try MachineTemplateInstaller.extract(archive, into: staging,
            expectedBytes: Int64(config.count + 512 + rom.count)))
        try FileManager.default.removeItem(at: staging)
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: false)
        XCTAssertNoThrow(try MachineTemplateInstaller.extract(archive, into: staging,
            expectedBytes: Int64(config.count + 512 + rom.count), quadra: true))
        let machine = DownloadableMachine(id: "quadra-bad-rom", name: "Quadra", summary: "Base",
            osVersion: "Mac OS 8.1", minimumAppVersion: "4.6.0", archiveURL: source,
            archiveBytes: Int64(try Data(contentsOf: archive).count),
            installedBytes: Int64(config.count + 512 + rom.count), sha256: String(repeating: "a", count: 64),
            diskCapacityBytes: 512, machineFamily: .quadra800)
        XCTAssertThrowsError(try MachineTemplateInstaller.installVerifiedArchive(archive,
            machine: machine, name: "Bad ROM", in: directory))
        XCTAssertFalse(FileManager.default.fileExists(atPath: directory.appendingPathComponent("Bad ROM.classic").path))
        let short = try makeArchive([Entry("config.json", data: config),
            Entry("disk.img", data: Data(repeating: 0, count: 512)), Entry("pram.img", data: Data([1]))])
        try FileManager.default.removeItem(at: staging)
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: false)
        XCTAssertThrowsError(try MachineTemplateInstaller.extract(short, into: staging,
            expectedBytes: Int64(config.count + 513), quadra: true))
    }

    func testCatalogValidationAndVersionComparison() throws {
        let machine = fixtureMachine(bytes: Data("archive".utf8))
        let catalog = MachineCatalog(schemaVersion: 1, machines: [machine])
        XCTAssertEqual(try MachineCatalog.decode(JSONEncoder().encode(catalog)).machines, [machine])
        XCTAssertNoThrow(try machine.checkCompatibility(appVersion: "3.0"))
        XCTAssertNoThrow(try machine.checkCompatibility(appVersion: "3.10.1"))
        XCTAssertThrowsError(try machine.checkCompatibility(appVersion: "2.99.99"))
        XCTAssertThrowsError(try machine.checkCompatibility(appVersion: "unrecognized"))
        XCTAssertThrowsError(try MachineCatalog.decode(JSONEncoder().encode(
            MachineCatalog(schemaVersion: 2, machines: [machine]))))
        XCTAssertThrowsError(try MachineCatalog.decode(JSONEncoder().encode(
            MachineCatalog(schemaVersion: 1, machines: [machine, machine]))))
        XCTAssertThrowsError(try MachineCatalog.decode(Data(repeating: 32, count: 1_048_577)))
        for replacement: [String: Any] in [
            ["archiveURL": "http://downloads.example.test/os9.tar.gz"],
            ["archiveURL": "https://user:password@downloads.example.test/os9.tar.gz"],
            ["archiveURL": "https://downloads.example.test/os9.tar.gz#fragment"],
            ["sha256": String(repeating: "g", count: 64)],
            ["archiveBytes": -1], ["installedBytes": 200_000_000_000],
            ["minimumAppVersion": "3.0-beta"], ["id": "../../machine"],
            ["name": ""], ["name": String(repeating: "x", count: 101)]
        ] {
            var object = try XCTUnwrap(JSONSerialization.jsonObject(with: JSONEncoder().encode(machine)) as? [String: Any])
            object.merge(replacement) { _, new in new }
            let data = try JSONSerialization.data(withJSONObject: ["schemaVersion": 1, "machines": [object]])
            XCTAssertThrowsError(try MachineCatalog.decode(data), "Should reject \(replacement.keys)")
        }
    }

    func testCatalogAndProvenanceIgnoreRetiredGraphicsMetadata() throws {
        let machine = fixtureMachine(bytes: Data([1]))
        var entry = try XCTUnwrap(JSONSerialization.jsonObject(with: JSONEncoder().encode(machine)) as? [String: Any])
        XCTAssertNil(entry["gxMetalVersion"])
        entry["gxMetalVersion"] = "2.3.0"
        let oldCatalog = try JSONSerialization.data(withJSONObject: ["schemaVersion": 1, "machines": [entry]])
        XCTAssertEqual(try MachineCatalog.decode(oldCatalog).machines, [machine])
        let metadata = VMTemplateMetadata(machine: machine)
        var oldMetadata = try XCTUnwrap(JSONSerialization.jsonObject(with: JSONEncoder().encode(metadata)) as? [String: Any])
        XCTAssertNil(oldMetadata["gxMetalVersion"])
        oldMetadata["gxMetalVersion"] = "2.3.0"
        try JSONSerialization.data(withJSONObject: oldMetadata).write(to: directory.appendingPathComponent(VMTemplateMetadata.fileName))
        XCTAssertEqual(VMTemplateMetadata.load(from: directory), metadata)
    }

    func testCoplandCatalogAndFirmwareIsolation() throws {
        let machine = DownloadableMachine(id: "copland-d11e4-v1", name: "Copland",
            summary: "Experimental preview", osVersion: "D11E4", minimumAppVersion: "3.2.0", archiveURL: source, archiveBytes: 1024,
            installedBytes: 4_203_536, sha256: String(repeating: "0", count: 64),
            diskCapacityBytes: 512, machineFamily: .powerMac7500)
        XCTAssertNoThrow(try machine.validate())
        XCTAssertThrowsError(try machine.checkCompatibility(appVersion: "3.0.1"))
        XCTAssertNoThrow(try machine.checkCompatibility(appVersion: "3.2.0"))
        XCTAssertEqual(try MachineCatalog.decode(JSONEncoder().encode(MachineCatalog(schemaVersion: 1, machines: [machine]))).machines[0].family, .powerMac7500)
        XCTAssertEqual(fixtureMachine(bytes: Data([1])).family, .powerMacG4)
        // Arbitrary firmware remains forbidden for legacy templates. A Copland
        // entry also cannot import a wrong ROM, a truncated NVRAM, or mismatched config.
        let config = try JSONEncoder().encode(VMConfig(name: "Copland", machineFamily: .powerMac7500))
        let archive = try makeArchive([Entry("config.json", data: config),
            Entry("disk.img", data: Data(repeating: 0, count: 512)),
            Entry("bootrom.bin", data: Data(repeating: 0, count: 4_194_304)),
            Entry("nvram.bin", data: Data(repeating: 0, count: 8209))])
        let staging = directory.appendingPathComponent("extract", isDirectory: true)
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: false)
        let bytes = Int64(config.count + 512 + 4_194_304 + 8209)
        XCTAssertThrowsError(try MachineTemplateInstaller.extract(archive, into: staging, expectedBytes: bytes))
        try FileManager.default.removeItem(at: staging)
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: false)
        XCTAssertNoThrow(try MachineTemplateInstaller.extract(archive, into: staging, expectedBytes: bytes, copland: true))
        XCTAssertThrowsError(try CoplandMachine.validateFirmware(in: staging))
    }

    func testSparseCatalogMetadataBoundsAndLegacyFallback() throws {
        let capacity: Int64 = 32 * 1_073_741_824
        let machine = fixtureMachine(bytes: Data([1]), installedBytes: capacity + 1024,
            diskCapacityBytes: capacity, requiredStorageBytes: 512 * 1_048_576)
        XCTAssertNoThrow(try machine.validate())
        XCTAssertNoThrow(try machine.checkCompatibility(appVersion: "3.0.0"))
        let decoded = try MachineCatalog.decode(JSONEncoder().encode(MachineCatalog(schemaVersion: 1, machines: [machine])))
        XCTAssertEqual(decoded.machines.first?.diskCapacityBytes, capacity)
        XCTAssertEqual(MachineTemplateStoragePlan(machine: machine, supportsSparseFiles: true).requiredBytes, 512 * 1_048_576)
        XCTAssertEqual(MachineTemplateStoragePlan(machine: machine, supportsSparseFiles: false).requiredBytes, capacity + 1024)
        let legacy = fixtureMachine(bytes: Data([1]), installedBytes: capacity + 1024)
        XCTAssertNil(try MachineCatalog.decode(JSONEncoder().encode(MachineCatalog(schemaVersion: 1, machines: [legacy]))).machines.first?.requiredStorageBytes)
        XCTAssertEqual(MachineTemplateStoragePlan(machine: legacy, supportsSparseFiles: true).requiredBytes, capacity + 1024)
        // A 3.0 decoder ignores the new fields and retains the full tar bound.
        struct LegacyEntry: Decodable { let installedBytes: Int64 }
        XCTAssertEqual(try JSONDecoder().decode(LegacyEntry.self, from: JSONEncoder().encode(machine)).installedBytes, capacity + 1024)
        for replacement: [String: Any] in [
            ["diskCapacityBytes": capacity - 1], ["diskCapacityBytes": capacity + 1024],
            ["diskCapacityBytes": 512], ["diskCapacityBytes": NSNull()],
            ["requiredStorageBytes": 0], ["requiredStorageBytes": -1],
            ["requiredStorageBytes": 1_048_577], ["requiredStorageBytes": capacity + 4 * 1_048_576]
        ] {
            var object = try XCTUnwrap(JSONSerialization.jsonObject(with: JSONEncoder().encode(machine)) as? [String: Any])
            object.merge(replacement) { _, new in new }
            let data = try JSONSerialization.data(withJSONObject: ["schemaVersion": 1, "machines": [object]])
            XCTAssertThrowsError(try MachineCatalog.decode(data), "Should reject \(replacement.keys)")
        }
    }

    func testSparseInstallPreservesBoundaryBytesAndLogicalCapacity() throws {
        try XCTSkipUnless(MachineTemplateInstaller.supportsSparseFiles(at: directory), "Sparse filesystem required")
        let chunk = Int(MachineTemplateInstaller.sparseChunkBytes)
        var disk = Data(repeating: 0, count: 8 * chunk + 512)
        for (offset, byte) in [(17, UInt8(71)), (chunk - 1, 83), (2 * chunk + 5, 97), (disk.count - 1, 109)] { disk[offset] = byte }
        let config = try JSONEncoder().encode(VMConfig(name: "Template", machineFamily: .powerMacG4))
        let archive = try makeArchive([Entry("config.json", data: config), Entry("disk.img", data: disk)])
        let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: Int64(config.count + disk.count),
            diskCapacityBytes: Int64(disk.count), requiredStorageBytes: 4 * Int64(chunk))
        let installed = try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Sparse", in: directory)
        let file = installed.appendingPathComponent("disk.img")
        XCTAssertEqual(try Data(contentsOf: file), disk)
        let sizes = try file.resourceValues(forKeys: [.fileSizeKey, .fileAllocatedSizeKey])
        XCTAssertEqual(sizes.fileSize, disk.count)
        XCTAssertLessThanOrEqual(try XCTUnwrap(sizes.fileAllocatedSize), 3 * chunk)
        XCTAssertGreaterThan(try XCTUnwrap(sizes.fileAllocatedSize), 0)
        let imported = try JSONDecoder().decode(VMConfig.self, from: Data(contentsOf: installed.appendingPathComponent("config.json")))
        XCTAssertEqual(imported.diskSizeGB, 1)
    }

    func testSparseZeroSectorTailPreservesPrecedingChunk() throws {
        try XCTSkipUnless(MachineTemplateInstaller.supportsSparseFiles(at: directory), "Sparse filesystem required")
        let chunk = Int(MachineTemplateInstaller.sparseChunkBytes)
        var disk = Data(repeating: 0, count: chunk + 512)
        disk[0] = 71; disk[chunk - 1] = 83
        let config = try JSONEncoder().encode(VMConfig(name: "Template", machineFamily: .powerMacG4))
        let archive = try makeArchive([Entry("config.json", data: config), Entry("disk.img", data: disk)])
        let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: Int64(config.count + disk.count),
            diskCapacityBytes: Int64(disk.count), requiredStorageBytes: 2 * Int64(chunk))
        let installed = try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Zero tail", in: directory)
        let file = installed.appendingPathComponent("disk.img")
        XCTAssertEqual(try Data(contentsOf: file), disk)
        XCTAssertLessThanOrEqual(try XCTUnwrap(file.resourceValues(forKeys: [.fileAllocatedSizeKey]).fileAllocatedSize), chunk)
    }

    func testUnderstatedSparseBudgetStopsBeforeExcessWritesAndInstallCleansUp() throws {
        try XCTSkipUnless(MachineTemplateInstaller.supportsSparseFiles(at: directory), "Sparse filesystem required")
        let chunk = Int(MachineTemplateInstaller.sparseChunkBytes)
        var disk = Data(repeating: 0, count: 2 * chunk)
        disk[0] = 71; disk[disk.count - 1] = 83
        let config = try JSONEncoder().encode(VMConfig(name: "Template", machineFamily: .powerMacG4))
        let archive = try makeArchive([Entry("config.json", data: config), Entry("disk.img", data: disk)])
        let target = directory.appendingPathComponent("partial")
        try FileManager.default.createDirectory(at: target, withIntermediateDirectories: false)
        XCTAssertThrowsError(try MachineTemplateInstaller.extract(archive, into: target,
            expectedBytes: Int64(config.count + disk.count), expectedDiskBytes: Int64(disk.count),
            maximumStorageBytes: 2 * Int64(chunk))) { error in
                XCTAssertTrue(error.localizedDescription.contains("initial storage"))
            }
        // Config consumes one charged chunk; only the first disk chunk may be
        // written before the second nonzero chunk exceeds the declared bound.
        let partial = try Data(contentsOf: target.appendingPathComponent("disk.img"))
        XCTAssertEqual(partial.count, disk.count)
        XCTAssertEqual(partial.prefix(chunk), disk.prefix(chunk))
        XCTAssertTrue(partial.suffix(chunk).allSatisfy { $0 == 0 })
        let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: Int64(config.count + disk.count),
            diskCapacityBytes: Int64(disk.count), requiredStorageBytes: 2 * Int64(chunk))
        XCTAssertThrowsError(try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Invalid budget", in: directory))
        XCTAssertFalse(FileManager.default.fileExists(atPath: directory.appendingPathComponent("Invalid budget.classic").path))
        XCTAssertFalse(try FileManager.default.contentsOfDirectory(atPath: directory.path).contains { $0.hasPrefix(".classicmac-import-") })
    }

    func testSparseMetadataCannotRelaxExpansionOrChangeDiskCapacity() throws {
        let config = try JSONEncoder().encode(VMConfig(name: "Template", machineFamily: .powerMacG4))
        let disk = Data(repeating: 0, count: 2 * 1_048_576)
        let archive = try makeArchive([Entry("config.json", data: config), Entry("disk.img", data: disk)])
        for (expanded, capacity) in [(Int64(config.count + disk.count - 512), Int64(disk.count - 512)),
                                     (Int64(config.count + disk.count), Int64(disk.count - 512))] {
            let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: expanded,
                diskCapacityBytes: capacity, requiredStorageBytes: 1_048_576)
            XCTAssertNoThrow(try machine.validate())
            XCTAssertThrowsError(try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Wrong metadata", in: directory))
        }
    }

    func testStreamingIntegrityAndCancellation() throws {
        let data = Data(repeating: 91, count: 2_100_000)
        let machine = fixtureMachine(bytes: data)
        let file = directory.appendingPathComponent("archive")
        try data.write(to: file)
        var progress: [Int64] = []
        try MachineDownloadTransfer.verify(file, machine: machine, progress: { progress.append($0) })
        XCTAssertEqual(progress.last, Int64(data.count))
        XCTAssertGreaterThan(progress.count, 1)
        XCTAssertThrowsError(try MachineDownloadTransfer.verify(file, machine: machine, cancelled: { true })) {
            XCTAssertTrue($0 is CancellationError)
        }
        try Data(repeating: 92, count: data.count).write(to: file)
        XCTAssertThrowsError(try MachineDownloadTransfer.verify(file, machine: machine))
        try Data([1]).write(to: file)
        XCTAssertThrowsError(try MachineDownloadTransfer.verify(file, machine: machine))
    }

    func testFreshInstallSanitizesPathsIdentityAndNeverReplacesExistingMachine() throws {
        var original = VMConfig(name: "Source", machineFamily: .powerMacG4,
            useBrowserDisplay: true, cdImagePath: "/private/installer.iso", bootFromCD: true,
            sharedFolderPath: "/Users/source/Secrets")
        original.floppyImagePath = "/private/floppy.img"
        let config = try JSONEncoder().encode(original)
        let entries = [Entry("config.json", data: config), Entry("disk.img", data: Data(repeating: 0, count: 2_097_152))]
        let archive = try makeArchive(entries)
        let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: Int64(config.count + 2_097_152))
        let existing = directory.appendingPathComponent("My Mac.classic")
        try FileManager.default.createDirectory(at: existing, withIntermediateDirectories: false)
        try Data("untouched".utf8).write(to: existing.appendingPathComponent("sentinel"))
        let installed = try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: " My Mac ", in: directory)
        XCTAssertEqual(installed.lastPathComponent, "My Mac 2.classic")
        let fresh = try JSONDecoder().decode(VMConfig.self, from: Data(contentsOf: installed.appendingPathComponent("config.json")))
        XCTAssertNotEqual(fresh.id, original.id)
        XCTAssertEqual(fresh.name, "My Mac")
        XCTAssertNil(fresh.sharedFolderPath)
        XCTAssertNil(fresh.cdImagePath)
        XCTAssertNil(fresh.floppyImagePath)
        XCTAssertFalse(fresh.bootFromCD)
        XCTAssertFalse(fresh.useBrowserDisplay)
        XCTAssertTrue(fresh.toolsCDInserted)
        let metadata = try XCTUnwrap(VMTemplateMetadata.load(from: installed))
        XCTAssertEqual(metadata.osVersion, machine.osVersion)
        XCTAssertEqual(metadata.archiveSHA256, machine.sha256)
        XCTAssertEqual(try String(contentsOf: existing.appendingPathComponent("sentinel"), encoding: .utf8), "untouched")
        let disk = try installed.appendingPathComponent("disk.img").resourceValues(forKeys: [.fileSizeKey, .fileAllocatedSizeKey])
        XCTAssertEqual(disk.fileSize, 2_097_152)
        XCTAssertLessThan(try XCTUnwrap(disk.fileAllocatedSize), 2_097_152)
        XCTAssertFalse(try FileManager.default.contentsOfDirectory(atPath: directory.path).contains { $0.hasPrefix(".classicmac-import-") })
    }

    func testArchiveRejectsTraversalLinksExtensionsDuplicateAndOversizedFiles() throws {
        let config = try JSONEncoder().encode(VMConfig(name: "Template", machineFamily: .powerMacG4))
        let valid = [Entry("config.json", data: config), Entry("disk.img", data: Data(repeating: 0, count: 512))]
        let badEntries: [[Entry]] = [
            [Entry("../outside", data: Data([1]))] + valid,
            [Entry("/tmp/outside", data: Data([1]))] + valid,
            [Entry("disk.img", data: Data(), type: 50, link: "/tmp/outside")] + [valid[0]],
            [Entry("disk.img", data: Data(), type: 49, link: "../outside")] + [valid[0]],
            [Entry("subdirectory", data: Data(), type: 53)] + valid,
            [Entry("PaxHeaders", data: Data([1]), type: 120)] + valid,
            valid + [valid[0]],
            [Entry("config.json", data: Data(repeating: 1, count: 65_537)), valid[1]],
            [Entry("config.json", data: config), Entry("disk.img", data: Data([1]))],
            [Entry("config.json", data: config), Entry("disk.img", data: Data(), declaredSize: Int64.max)],
            [Entry("config.json", data: config, badChecksum: true), valid[1]]
        ]
        for (index, entries) in badEntries.enumerated() {
            let archive = try makeArchive(entries)
            let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: Int64(config.count + 512))
            XCTAssertThrowsError(try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Unsafe \(index)", in: directory))
            XCTAssertFalse(FileManager.default.fileExists(atPath: directory.appendingPathComponent("Unsafe \(index).classic").path))
        }
        XCTAssertFalse(try FileManager.default.contentsOfDirectory(atPath: directory.path).contains { $0.hasPrefix(".classicmac-import-") })
    }

    func testArchiveRequiresExactExpandedSizeAndRejectsTruncationAndTrailingData() throws {
        let config = try JSONEncoder().encode(VMConfig(name: "Template", machineFamily: .powerMacG4))
        let entries = [Entry("config.json", data: config), Entry("disk.img", data: Data(repeating: 0, count: 512))]
        for extraSize: Int64 in [-1, 1] {
            let archive = try makeArchive(entries)
            let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: Int64(config.count + 512) + extraSize)
            XCTAssertThrowsError(try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Broken", in: directory))
        }
        for tail in [Data(), Data(repeating: 0, count: 512), Data(repeating: 0, count: 1024) + Data([1])] {
            let archive = try makeArchive(entries, tail: tail)
            let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: Int64(config.count + 512))
            XCTAssertThrowsError(try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Broken", in: directory))
        }
    }

    func testGNUSizeEncodingAndCancelledInstallCleanup() throws {
        let config = try JSONEncoder().encode(VMConfig(name: "Template", machineFamily: .powerMacG4))
        let archive = try makeArchive([Entry("config.json", data: config), Entry("disk.img", data: Data(repeating: 0, count: 512), base256: true)])
        let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: Int64(config.count + 512))
        XCTAssertNoThrow(try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "GNU", in: directory))
        XCTAssertThrowsError(try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Cancelled", in: directory, cancelled: { true })) {
            XCTAssertTrue($0 is CancellationError)
        }
        XCTAssertFalse(try FileManager.default.contentsOfDirectory(atPath: directory.path).contains { $0.hasPrefix(".classicmac-import-") })
        XCTAssertThrowsError(try MachineTemplateInstaller.checkSpace(at: directory, requiredBytes: Int64.max))
    }

    func testTemplateConfigCannotReferenceExternalDisk() throws {
        var config = VMConfig(name: "Template", machineFamily: .powerMacG4)
        config.diskImageName = "../../outside.img"
        let data = try JSONEncoder().encode(config)
        let archive = try makeArchive([Entry("config.json", data: data), Entry("disk.img", data: Data(repeating: 0, count: 512))])
        let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: Int64(data.count + 512))
        XCTAssertThrowsError(try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Unsafe", in: directory))
    }

    func testPartialDownloadsResumeWithValidatedRange() async throws {
        let whole = Data("abcdefghij".utf8)
        let file = directory.appendingPathComponent("partial")
        try whole.prefix(4).write(to: file)
        MachineDownloadURLProtocol.handler = { request, client, loader in
            XCTAssertEqual(request.value(forHTTPHeaderField: "Range"), "bytes=4-")
            client.urlProtocol(loader, didReceive: HTTPURLResponse(url: request.url!, statusCode: 206, httpVersion: "HTTP/1.1",
                headerFields: ["Content-Range": "bytes 4-9/10", "Content-Length": "6"])!, cacheStoragePolicy: .notAllowed)
            client.urlProtocol(loader, didLoad: whole.suffix(6))
            client.urlProtocolDidFinishLoading(loader)
        }
        _ = try await transfer(to: file, expected: 10).run()
        XCTAssertEqual(try Data(contentsOf: file), whole)
    }

    func testServerIgnoringRangeRestartsInsteadOfAppending() async throws {
        let file = directory.appendingPathComponent("partial")
        try Data("old".utf8).write(to: file)
        respond(status: 200, data: Data("new-file".utf8), headers: ["Content-Length": "8"])
        _ = try await transfer(to: file, expected: 8).run()
        XCTAssertEqual(try Data(contentsOf: file), Data("new-file".utf8))
    }

    func testInvalidRangeAndServerErrorsLeavePartialUntouched() async throws {
        let file = directory.appendingPathComponent("partial")
        let partial = Data("old".utf8)
        for (status, headers) in [(206, ["Content-Range": "bytes 0-7/8"]), (404, [:]), (500, [:])] {
            try partial.write(to: file)
            respond(status: status, data: Data("invalid!".utf8), headers: headers)
            do { _ = try await transfer(to: file, expected: 8).run(); XCTFail("Expected rejection") }
            catch { XCTAssertEqual(try Data(contentsOf: file), partial) }
        }
    }

    func testTransferBoundsTruncationAndEncoding() async throws {
        for (data, headers) in [(Data(repeating: 1, count: 11), [:]), (Data(repeating: 1, count: 3), [:]),
                                (Data(repeating: 1, count: 10), ["Content-Encoding": "gzip"])] {
            let file = directory.appendingPathComponent(UUID().uuidString)
            respond(status: 200, data: data, headers: headers)
            do { _ = try await transfer(to: file, expected: 10).run(); XCTFail("Expected rejection") }
            catch {
                let size = (try? file.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
                XCTAssertLessThanOrEqual(size, 10)
            }
        }
    }

    func testConnectionFailureRetainsPrefixForRetry() async throws {
        let file = directory.appendingPathComponent("partial")
        let connection = MockConnectionFailure()
        let prefix = Data(repeating: 97, count: 2048)
        MachineDownloadURLProtocol.handler = { request, client, loader in
            connection.arm(client: client, loader: loader)
            client.urlProtocol(loader, didReceive: HTTPURLResponse(url: request.url!, statusCode: 200, httpVersion: "HTTP/1.1",
                headerFields: ["Content-Length": "4096", "Content-Type": "application/octet-stream"])!, cacheStoragePolicy: .notAllowed)
            client.urlProtocol(loader, didLoad: prefix)
        }
        do {
            _ = try await transfer(to: file, expected: 4096, progress: { bytes in
                if bytes == Int64(prefix.count) { connection.fail() }
            }).run()
            XCTFail("Expected network failure")
        }
        catch { XCTAssertEqual(try Data(contentsOf: file), prefix) }
    }

    func testCancellationBeforeTransferAndExclusiveCacheLease() async throws {
        let file = directory.appendingPathComponent("cancelled")
        let transfer = transfer(to: file, expected: 10)
        transfer.cancel()
        do { _ = try await transfer.run(); XCTFail("Expected cancellation") }
        catch { XCTAssertTrue(error is CancellationError) }
        XCTAssertFalse(FileManager.default.fileExists(atPath: file.path))
        let digest = String(repeating: "a", count: 64)
        var lease: MachineDownloadLease? = try MachineDownloadLease(directory: directory, digest: digest)
        XCTAssertThrowsError(try MachineDownloadLease(directory: directory, digest: digest))
        withExtendedLifetime(lease) {}
        lease = nil
        XCTAssertNoThrow(try MachineDownloadLease(directory: directory, digest: digest))
    }

    func testCancellingActiveTransferKeepsDownloadedBytes() async throws {
        let file = directory.appendingPathComponent("paused")
        let prefix = Data(repeating: 81, count: 2048)
        let wrotePrefix = expectation(description: "Downloaded bytes persisted")
        MachineDownloadURLProtocol.handler = { request, client, loader in
            client.urlProtocol(loader, didReceive: HTTPURLResponse(url: request.url!, statusCode: 200,
                httpVersion: "HTTP/1.1", headerFields: ["Content-Length": "4096", "Content-Type": "application/octet-stream"])!,
                cacheStoragePolicy: .notAllowed)
            client.urlProtocol(loader, didLoad: prefix)
        }
        let transfer = transfer(to: file, expected: 4096, progress: { bytes in
            if bytes == 2048 { wrotePrefix.fulfill() }
        })
        let operation = Task { try await transfer.run() }
        await fulfillment(of: [wrotePrefix], timeout: 2)
        operation.cancel()
        do { _ = try await operation.value; XCTFail("Expected cancellation") }
        catch { XCTAssertTrue(error is CancellationError) }
        XCTAssertEqual(try Data(contentsOf: file), prefix)
    }

    func testRedirectsCannotChangeOriginOrDowngradeHTTPS() {
        let file = directory.appendingPathComponent("redirect")
        let transfer = transfer(to: file, expected: 10)
        let session = URLSession(configuration: .ephemeral)
        defer { session.invalidateAndCancel() }
        let task = session.dataTask(with: source)
        let response = HTTPURLResponse(url: source, statusCode: 302, httpVersion: "HTTP/1.1", headerFields: nil)!
        for address in ["http://downloads.example.test/os9.tar.gz", "https://other.example.test/os9.tar.gz",
                        "https://downloads.example.test:8443/os9.tar.gz", "https://user@downloads.example.test/os9.tar.gz"] {
            transfer.urlSession(session, task: task, willPerformHTTPRedirection: response,
                newRequest: URLRequest(url: URL(string: address)!)) { XCTAssertNil($0) }
        }
        let sameOrigin = URL(string: "https://downloads.example.test/new-path.tar.gz")!
        transfer.urlSession(session, task: task, willPerformHTTPRedirection: response,
            newRequest: URLRequest(url: sameOrigin)) { XCTAssertEqual($0?.url, sameOrigin) }
    }

    func testPackagingHelperProducesCompatibleSanitizedArchive() throws {
        let sourceBundle = directory.appendingPathComponent("Source.classic")
        try FileManager.default.createDirectory(at: sourceBundle, withIntermediateDirectories: false)
        let sourceConfig = try JSONEncoder().encode(VMConfig(name: "Private source", machineFamily: .powerMacG4,
            cdImagePath: "/Users/source/private.iso", sharedFolderPath: "/Users/source/private"))
        try sourceConfig.write(to: sourceBundle.appendingPathComponent("config.json"))
        try Data(repeating: 0, count: 512).write(to: sourceBundle.appendingPathComponent("disk.img"))
        try Data("private".utf8).write(to: sourceBundle.appendingPathComponent("private-notes.txt"))
        let archive = directory.appendingPathComponent("packaged.tar.gz")
        let catalog = directory.appendingPathComponent("catalog.json")
        let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/python3")
        process.arguments = [repo.appendingPathComponent("scripts/package-machine-template.py").path,
            "--bundle", sourceBundle.path, "--output", archive.path, "--catalog-output", catalog.path,
            "--archive-url", source.absoluteString, "--id", "mac-os9-v1",
            "--os-version", "Mac OS 9.2.1"]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        try process.run()
        process.waitUntilExit()
        XCTAssertEqual(process.terminationStatus, 0)
        let machine = try XCTUnwrap(MachineCatalog.decode(Data(contentsOf: catalog)).machines.first)
        XCTAssertEqual(machine.diskCapacityBytes, 512)
        XCTAssertEqual(machine.requiredStorageBytes, 1_048_576)
        XCTAssertEqual(machine.minimumAppVersion, "4.4.0")
        try MachineDownloadTransfer.verify(archive, machine: machine)
        let destination = try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Packaged", in: directory)
        XCTAssertEqual(Set(try FileManager.default.contentsOfDirectory(atPath: destination.path)),
                       ["config.json", "disk.img", "template-info.json"])
        XCTAssertEqual(try Data(contentsOf: sourceBundle.appendingPathComponent("config.json")), sourceConfig)
        XCTAssertFalse(try String(contentsOf: destination.appendingPathComponent("config.json"), encoding: .utf8).contains("/Users/source"))
    }

    func testInstallPreservesSourceATIGraphicsCardAndNormalizesNames() throws {
        let original = VMConfig(name: "Template", machineFamily: .powerMacG4, graphicsCard: .atiRadeon)
        let config = try JSONEncoder().encode(original)
        let archive = try makeArchive([Entry("config.json", data: config), Entry("disk.img", data: Data(repeating: 0, count: 512))])
        let machine = fixtureMachine(bytes: try Data(contentsOf: archive), installedBytes: Int64(config.count + 512))
        let installed = try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "  ATI Mac\n", in: directory)
        let fresh = try JSONDecoder().decode(VMConfig.self, from: Data(contentsOf: installed.appendingPathComponent("config.json")))
        XCTAssertEqual(fresh.name, "ATI Mac")
        XCTAssertEqual(fresh.graphicsCard, .atiRadeon)
        XCTAssertEqual(try MachineTemplateInstaller.normalizedName("\n My Mac \n"), "My Mac")
        XCTAssertThrowsError(try MachineTemplateInstaller.normalizedName(".hidden"))
        XCTAssertThrowsError(try MachineTemplateInstaller.normalizedName("..."))
        XCTAssertThrowsError(try MachineTemplateInstaller.normalizedName("\n\t\n"))
    }

    func testSparseExtractionPunchesHolesBeforeTightBudgetCheck() throws {
        try XCTSkipUnless(MachineTemplateInstaller.supportsSparseFiles(at: directory), "Sparse filesystem required")
        let config = Data("{}".utf8)
        // 512 extents of 4096 bytes separated by 4096-byte holes (2 MiB nonzero, 4 MiB spanned).
        var sparseBody = Data()
        var mapLines = ["512"]
        var offset = 0
        for _ in 0..<512 {
            mapLines.append("\(offset)")
            mapLines.append("4096")
            offset += 8192
            sparseBody.append(Data(repeating: 0x55, count: 4096))
        }
        let mapText = mapLines.joined(separator: "\n") + "\n"
        var map = Data(mapText.utf8)
        let paddedMapLen = ((map.count + 511) / 512) * 512
        map.append(Data(repeating: 0, count: paddedMapLen - map.count))
        let realSize = 8 * 1_048_576
        let values = ["GNU.sparse.major=1", "GNU.sparse.minor=0",
                      "GNU.sparse.name=disk.img", "GNU.sparse.realsize=\(realSize)"]
        let pax = Data(values.map { "\($0.utf8.count + 4) \($0)\n" }.joined().utf8)
        let archive = try makeArchive([
            Entry("config.json", data: config),
            Entry("PaxHeaders/disk.img", data: pax, type: 120),
            Entry("GNUSparseFile/disk.img", data: map + sparseBody),
        ])
        let staging = directory.appendingPathComponent("tight-sparse")
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: false)
        // Budget covers 1 MiB config charge + 2 MiB sparse extents (smaller than the 4 MiB spanned if unpunched).
        let budget: Int64 = 3 * 1_048_576
        try MachineTemplateInstaller.extract(
            archive, into: staging,
            expectedBytes: Int64(config.count + realSize),
            expectedDiskBytes: Int64(realSize),
            maximumStorageBytes: budget
        )
        let disk = staging.appendingPathComponent("disk.img")
        let attrs = try disk.resourceValues(forKeys: [.fileSizeKey, .fileAllocatedSizeKey])
        XCTAssertEqual(attrs.fileSize, realSize)
        XCTAssertLessThanOrEqual(try XCTUnwrap(attrs.fileAllocatedSize), 2 * 1_048_576 + 65_536)
    }

    func testLibraryFamilyMatchingATIRetentionAndTitles() throws {
        let mega = DownloadableMachine(
            id: "os922-mega-deluxe-v4", name: "Mac OS 9.2.2 Mega Deluxe v4",
            summary: "All 111 games and creative activities.", osVersion: "9.2.2",
            minimumAppVersion: "3.0.0", archiveURL: source, archiveBytes: 1_000,
            installedBytes: 2_000, sha256: String(repeating: "a", count: 64)
        )
        let megaEntry = LibraryEntry(machine: mega, edition: LibraryEdition.edition(for: mega.id))
        XCTAssertEqual(megaEntry.edition?.familyName, "os922-mega-deluxe")
        XCTAssertEqual(megaEntry.title, "Mac OS 9.2.2 Mega Deluxe")
        XCTAssertEqual(megaEntry.versionLabel, "v4")
        XCTAssertEqual(megaEntry.graphicsCard, .atiRadeon)

        let full922 = DownloadableMachine(
            id: "os922-full-v5", name: "Mac OS 9.2.2 (v5)", summary: "Full Mac OS 9.2.2.",
            osVersion: "9.2.2", minimumAppVersion: "3.0.0", archiveURL: source,
            archiveBytes: 1_000, installedBytes: 2_000, sha256: String(repeating: "b", count: 64)
        )
        let full922Entry = LibraryEntry(machine: full922, edition: nil)
        XCTAssertEqual(full922Entry.graphicsCard, .atiRadeon)
        XCTAssertNil(full922Entry.versionLabel)

        // Non-PowerMacG4 families never receive ATI Radeon.
        let quadraMega = DownloadableMachine(
            id: "os922-mega-deluxe-v4", name: "Quadra", summary: "Test",
            osVersion: "8.1", minimumAppVersion: "3.0.0", archiveURL: source,
            archiveBytes: 1_000, installedBytes: 2_000, sha256: String(repeating: "c", count: 64),
            machineFamily: .quadra800
        )
        XCTAssertEqual(LibraryEntry(machine: quadraMega, edition: LibraryEdition.edition(for: quadraMega.id)).graphicsCard, .standard)

        // Double version suppression (case-insensitive, word boundary).
        let deluxeV9Raw = DownloadableMachine(
            id: "os922-deluxe-v9", name: "Mac OS 9.2.2 Deluxe V9", summary: "Summary",
            osVersion: "9.2.2", minimumAppVersion: "3.0.0", archiveURL: source,
            archiveBytes: 1_000, installedBytes: 2_000, sha256: String(repeating: "d", count: 64)
        )
        XCTAssertNil(LibraryEntry(machine: deluxeV9Raw, edition: nil).versionLabel)
        let osX10 = DownloadableMachine(
            id: "mac-os-x-v1", name: "Mac OS X v10", summary: "Summary",
            osVersion: "10.4.11", minimumAppVersion: "3.0.0", archiveURL: source,
            archiveBytes: 1_000, installedBytes: 2_000, sha256: String(repeating: "e", count: 64),
            machineFamily: .powerMacOSX
        )
        XCTAssertEqual(LibraryEntry(machine: osX10, edition: nil).versionLabel, "v1")

        // Duplicate title disambiguation across both merged catalogs.
        let expectedTitles: [(String, MachineFamily, String, String)] = [
            ("system-7.5.5-base-v1", .quadra800, "System 7.5.5", "System 7.5.5 (Clean)"),
            ("mac-os-7.5.5-v1", .quadra800, "Mac OS 7.5.5", "System 7.5.5 (RetroBoot)"),
            ("mac-os-8.1-base-v1", .quadra800, "Mac OS 8.1", "Mac OS 8.1 (Clean)"),
            ("mac-os-8.1-v1", .quadra800, "Mac OS 8.1", "Mac OS 8.1 (Full)"),
            ("mac-os-9.2.1-ati-32gb-v4", .powerMacG4, "Mac OS 9.2.1", "Mac OS 9.2.1 (Clean)"),
            ("os921-full-v4", .powerMacG4, "Mac OS 9.2.1 (v4)", "Mac OS 9.2.1 (Full)"),
        ]
        for (id, family, rawName, expectedTitle) in expectedTitles {
            let m = DownloadableMachine(
                id: id, name: rawName, summary: "Summary", osVersion: "9.2.1",
                minimumAppVersion: "3.0.0", archiveURL: source,
                archiveBytes: 1_000, installedBytes: 2_000, sha256: String(repeating: "f", count: 64),
                machineFamily: family
            )
            let entry = LibraryEntry(machine: m, edition: LibraryEdition.edition(for: m.id))
            XCTAssertEqual(entry.title, expectedTitle)
        }
    }

    func testRetroBootSummaryCleaningAndDefaultRanking() {
        let sys755 = "Mac OS 7.5.5 (System 7.5.3 from the user's CD with Apple's 7.5.5 update) on an emulated Quadra 800 (68040, 128 MB): Easy Install on a 2 GB disk. Includes the Quadra 800 ROM this Mac needs (Apple's; kept private). Needs RetroBoot 1.7.0 or later. RetroBoot lab build."
        let cleaned755 = LibraryEntry.cleanSummary(sys755)
        XCTAssertFalse(cleaned755.contains("from the user's CD"))
        XCTAssertFalse(cleaned755.contains("Quadra 800 ROM"))
        XCTAssertFalse(cleaned755.contains("RetroBoot 1.7.0"))
        XCTAssertFalse(cleaned755.contains("RetroBoot lab build"))
        XCTAssertTrue(cleaned755.contains("System 7.5.3 with Apple's 7.5.5 update"))

        let tigerDeluxe = "Includes the original PowerPC Quake III Arena, period games, iLife ’06, customization utilities and screensavers. Select 2048 MB of Mac OS X memory and the ATI Radeon for the Deluxe setup. Requires RetroBoot 2.0.2 on an x86 PC with Mac OS X support."
        let cleanedTiger = LibraryEntry.cleanSummary(tigerDeluxe)
        XCTAssertFalse(cleanedTiger.contains("2048 MB"))
        XCTAssertFalse(cleanedTiger.contains("x86 PC"))
        XCTAssertTrue(cleanedTiger.contains("Runs on ClassicMac's standard Mac OS X display; 1024 MB RAM recommended."))

        let catalog = [
            DownloadableMachine(id: "copland-d11e4-activities-v3", name: "Copland", summary: "S", osVersion: "Copland D11E4", minimumAppVersion: "3.2.1", archiveURL: source, archiveBytes: 1, installedBytes: 512, sha256: String(repeating: "1", count: 64), machineFamily: .powerMac7500),
            DownloadableMachine(id: "system-7.5.5-base-v1", name: "System 7.5.5", summary: "S", osVersion: "System 7.5.5", minimumAppVersion: "4.6.0", archiveURL: source, archiveBytes: 1, installedBytes: 512, sha256: String(repeating: "2", count: 64), machineFamily: .quadra800),
            DownloadableMachine(id: "mac-os-8.1-base-v1", name: "Mac OS 8.1", summary: "S", osVersion: "Mac OS 8.1", minimumAppVersion: "4.6.0", archiveURL: source, archiveBytes: 1, installedBytes: 512, sha256: String(repeating: "3", count: 64), machineFamily: .quadra800),
            DownloadableMachine(id: "mac-os-x-10-4-11-tiger-deluxe-v1", name: "Tiger Deluxe", summary: "S", osVersion: "10.4.11", minimumAppVersion: "3.0.0", archiveURL: source, archiveBytes: 1, installedBytes: 512, sha256: String(repeating: "4", count: 64), machineFamily: .powerMacOSX),
            DownloadableMachine(id: "os922-mega-deluxe-v4", name: "Mega Deluxe", summary: "S", osVersion: "9.2.2", minimumAppVersion: "3.0.0", archiveURL: source, archiveBytes: 1, installedBytes: 512, sha256: String(repeating: "5", count: 64), machineFamily: .powerMacG4),
        ]
        let ranked = LibraryEntry.entries(from: catalog, includeOlder: false)
        XCTAssertEqual(ranked.map(\.id), [
            "os922-mega-deluxe-v4",
            "mac-os-x-10-4-11-tiger-deluxe-v1",
            "mac-os-8.1-base-v1",
            "system-7.5.5-base-v1",
            "copland-d11e4-activities-v3",
        ])
        XCTAssertEqual(ranked.map(\.category), [.macOS9, .macOSX, .macOS8, .system7, .copland])
        XCTAssertFalse(LibraryEntry.hasOlderVersions(in: catalog))
        XCTAssertTrue(ranked[0].matches(query: "Mega", category: .macOS9))
        XCTAssertFalse(ranked[0].matches(query: "Mega", category: .macOSX))
    }

    @MainActor
    func testDownloadModelFreeSpaceCacheAndPartialCatalogWarning() async throws {
        let cache = directory.appendingPathComponent("cache", isDirectory: true)
        let save = directory.appendingPathComponent("save", isDirectory: true)
        try FileManager.default.createDirectory(at: save, withIntermediateDirectories: true)

        let digest = String(repeating: "a", count: 64)
        let requiredStorage: Int64 = 12_509_511_680
        let tiger = DownloadableMachine(
            id: "mac-os-x-10-4-11-tiger-deluxe-v1", name: "Tiger Deluxe",
            summary: "Tiger", osVersion: "10.4.11", minimumAppVersion: "3.0.0",
            archiveURL: source, archiveBytes: 8_419_440_145,
            installedBytes: 34_359_755_072, sha256: digest,
            diskCapacityBytes: 34_359_738_368, requiredStorageBytes: requiredStorage,
            machineFamily: .powerMacOSX
        )

        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [MachineDownloadURLProtocol.self]
        let primaryURL = URL(string: "https://downloads.example.test/catalog1.json")!
        let secondaryURL = URL(string: "https://downloads.example.test/catalog2.json")!
        let encodedCatalog = try JSONEncoder().encode(MachineCatalog(schemaVersion: 1, machines: [tiger]))
        MachineDownloadURLProtocol.handler = { request, client, loader in
            if request.url == primaryURL {
                client.urlProtocol(loader, didReceive: HTTPURLResponse(url: request.url!, statusCode: 200, httpVersion: "HTTP/1.1",
                    headerFields: ["Content-Length": "\(encodedCatalog.count)"])!, cacheStoragePolicy: .notAllowed)
                client.urlProtocol(loader, didLoad: encodedCatalog)
                client.urlProtocolDidFinishLoading(loader)
            } else {
                client.urlProtocol(loader, didReceive: HTTPURLResponse(url: request.url!, statusCode: 503, httpVersion: "HTTP/1.1",
                    headerFields: ["Content-Length": "0"])!, cacheStoragePolicy: .notAllowed)
                client.urlProtocolDidFinishLoading(loader)
            }
        }

        let model = MachineDownloadModel(catalogURLs: [primaryURL, secondaryURL], cacheDirectory: cache, transferConfiguration: config)
        model.loadCatalog()
        while model.isBusy { try await Task.sleep(nanoseconds: 20_000_000) }
        XCTAssertEqual(model.machines, [tiger])
        XCTAssertNotNil(model.partialCatalogWarning)
        XCTAssertNil(model.errorMessage)

        // Same-volume free-space calculation includes remaining archiveBytes + spaceReserve + requiredStorageBytes.
        let requiredFull = model.totalRequiredFreeBytes(for: tiger, saveFolder: save, supportsSparse: true)
        XCTAssertEqual(requiredFull, requiredStorage + MachineTemplateInstaller.spaceReserve + 8_419_440_145)

        let partFile = cache.appendingPathComponent(digest + ".tar.gz.part")
        try Data(repeating: 0, count: 1_000).write(to: partFile)
        XCTAssertTrue(model.hasPartialDownload(tiger))
        XCTAssertFalse(model.hasCompletedCache(tiger))
        XCTAssertEqual(model.totalRequiredFreeBytes(for: tiger, saveFolder: save, supportsSparse: true),
                       requiredStorage + MachineTemplateInstaller.spaceReserve + 8_419_440_145 - 1_000)

        // Exact completed archive check (tested with a small machine fixture).
        let small = fixtureMachine(bytes: Data(repeating: 1, count: 64), installedBytes: 1_048_576,
                                   diskCapacityBytes: 524_288, requiredStorageBytes: 1_048_576)
        let smallPart = cache.appendingPathComponent(small.sha256 + ".tar.gz.part")
        try Data(repeating: 1, count: 64).write(to: smallPart)
        XCTAssertFalse(model.hasPartialDownload(small))
        XCTAssertTrue(model.hasCompletedCache(small))
        XCTAssertEqual(model.totalRequiredFreeBytes(for: small, saveFolder: save, supportsSparse: true),
                       1_048_576 + MachineTemplateInstaller.spaceReserve)

        // Cancel behavior: cancelling with 0 bytes transitions to .idle; with > 0 bytes transitions to .paused.
        let emptyArchiveMachine = fixtureMachine(bytes: Data(repeating: 2, count: 4096), installedBytes: 1_048_576)
        let zeroStarted = expectation(description: "0-byte transfer started")
        MachineDownloadURLProtocol.handler = { request, client, loader in
            client.urlProtocol(loader, didReceive: HTTPURLResponse(url: request.url!, statusCode: 200, httpVersion: "HTTP/1.1",
                headerFields: ["Content-Length": "4096"])!, cacheStoragePolicy: .notAllowed)
            zeroStarted.fulfill()
        }
        model.install(emptyArchiveMachine, name: "Zero Cancel", directory: save) { _ in XCTFail("Should not install") }
        await fulfillment(of: [zeroStarted], timeout: 2)
        model.cancel()
        while model.isBusy { try await Task.sleep(nanoseconds: 20_000_000) }
        XCTAssertEqual(model.phase, .idle)
        XCTAssertEqual(model.activityLabel, "")

        let partialStarted = expectation(description: "Partial transfer wrote bytes")
        MachineDownloadURLProtocol.handler = { request, client, loader in
            client.urlProtocol(loader, didReceive: HTTPURLResponse(url: request.url!, statusCode: 200, httpVersion: "HTTP/1.1",
                headerFields: ["Content-Length": "4096"])!, cacheStoragePolicy: .notAllowed)
            client.urlProtocol(loader, didLoad: Data(repeating: 7, count: 1024))
            partialStarted.fulfill()
        }
        model.install(emptyArchiveMachine, name: "Paused Cancel", directory: save) { _ in XCTFail("Should not install") }
        await fulfillment(of: [partialStarted], timeout: 2)
        try await Task.sleep(nanoseconds: 50_000_000)
        model.cancel()
        while model.isBusy { try await Task.sleep(nanoseconds: 20_000_000) }
        XCTAssertEqual(model.phase, .paused)
        XCTAssertEqual(model.activityLabel, "Paused. Your download is saved.")
        XCTAssertTrue(model.hasPartialDownload(emptyArchiveMachine))

        XCTAssertEqual(DownloadProgressView.speed(12_500_000), "12.5 MB/s")
        XCTAssertEqual(DownloadProgressView.remaining(30), "30 s left")
    }

    func testThinArchiveResolutionAndMetadataDecoding() throws {
        let fullURL = URL(string: "https://downloads.example.test/full.tar.gz")!
        let thinURL = URL(string: "https://downloads.example.test/thin.tar.gz")!
        let fullSHA = String(repeating: "a", count: 64)
        let thinSHA = String(repeating: "b", count: 64)

        let payload: [String: Any] = [
            "schemaVersion": 1,
            "machines": [[
                "id": "os922-thin-v1",
                "name": "Mac OS 9.2.2",
                "summary": "Thin archive test",
                "osVersion": "9.2.2",
                "minimumAppVersion": "4.8.1",
                "archiveURL": fullURL.absoluteString,
                "archiveBytes": 5_000_000,
                "installedBytes": 10_000_000,
                "sha256": fullSHA,
                "tags": ["games", "3d"],
                "highlights": ["Includes Quake", "ATI Radeon ready"],
                "recommendedSettings": [
                    "ram_mb": 768,
                    "osx_ram_mb": 1536,
                    "graphics": "ati",
                    "guest_adapter": "ne2k",
                    "cpu": "g3",
                ],
                "thinArchive": [
                    "format": "retroboot-thin-1",
                    "archiveURL": thinURL.absoluteString,
                    "archiveBytes": 1_250_000,
                    "sha256": thinSHA,
                ],
            ]]
        ]
        let decoded = try MachineCatalog.decode(JSONSerialization.data(withJSONObject: payload))
        let machine = try XCTUnwrap(decoded.machines.first)
        XCTAssertEqual(machine.archiveURL, thinURL)
        XCTAssertEqual(machine.archiveBytes, 1_250_000)
        XCTAssertEqual(machine.sha256, thinSHA)
        XCTAssertEqual(machine.tags, ["games", "3d"])
        XCTAssertEqual(machine.highlights, ["Includes Quake", "ATI Radeon ready"])
        XCTAssertEqual(machine.recommendedSettings?.ram_mb, 768)
        XCTAssertEqual(machine.recommendedSettings?.osx_ram_mb, 1536)
        XCTAssertEqual(machine.recommendedSettings?.graphics, "ati")
        XCTAssertEqual(machine.recommendedSettings?.guest_adapter, "ne2k")
        XCTAssertEqual(machine.recommendedSettings?.cpu, "g3")

        // Invalid thinArchive variants preserve the outer archive metadata.
        for badThin: [String: Any] in [
            ["format": "unknown-format", "archiveURL": thinURL.absoluteString, "archiveBytes": 1_250_000, "sha256": thinSHA],
            ["format": "retroboot-thin-1", "archiveURL": "http://downloads.example.test/thin.tar.gz", "archiveBytes": 1_250_000, "sha256": thinSHA],
            ["format": "retroboot-thin-1", "archiveURL": thinURL.absoluteString, "archiveBytes": 0, "sha256": thinSHA],
            ["format": "retroboot-thin-1", "archiveURL": thinURL.absoluteString, "archiveBytes": 1_250_000, "sha256": String(repeating: "B", count: 64)],
        ] {
            var badPayload = payload
            var machines = try XCTUnwrap(badPayload["machines"] as? [[String: Any]])
            machines[0]["thinArchive"] = badThin
            badPayload["machines"] = machines
            let fallback = try XCTUnwrap(MachineCatalog.decode(JSONSerialization.data(withJSONObject: badPayload)).machines.first)
            XCTAssertEqual(fallback.archiveURL, fullURL)
            XCTAssertEqual(fallback.archiveBytes, 5_000_000)
            XCTAssertEqual(fallback.sha256, fullSHA)
        }
    }

    func testTransferRespectsBoundedResourceTimeout() async throws {
        let file = directory.appendingPathComponent("stalled-catalog.json")
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [MachineDownloadURLProtocol.self]
        MachineDownloadURLProtocol.handler = { _, _, _ in
            // Intentionally stall with no response so resourceTimeout fires.
        }
        let start = Date()
        let transfer = MachineDownloadTransfer(
            source: URL(string: "https://downloads.example.test/stalled.json")!,
            destination: file,
            expectedBytes: nil,
            maximumBytes: MachineCatalog.maximumCatalogBytes,
            configuration: config,
            requestTimeout: 0.2,
            resourceTimeout: 0.35
        )
        do {
            _ = try await transfer.run()
            XCTFail("Expected timeout")
        } catch {
            let elapsed = Date().timeIntervalSince(start)
            XCTAssertLessThan(elapsed, 5.0)
        }
    }

    func testRecommendedSettingsAppliedOnImportAndClampedByHostRAM() throws {
        // Test template config.json recommendedSettings overriding/applying on G4 (ram_mb + cpu == "g3").
        let g4Source = VMConfig(name: "Template", machineFamily: .powerMacG4, ramMB: 512, useG4CPU: true)
        var g4Dict = try XCTUnwrap(JSONSerialization.jsonObject(with: JSONEncoder().encode(g4Source)) as? [String: Any])
        g4Dict["recommendedSettings"] = ["ram_mb": 2048, "cpu": "g3"]
        let g4ConfigData = try JSONSerialization.data(withJSONObject: g4Dict)
        let g4Archive = try makeArchive([
            Entry("config.json", data: g4ConfigData),
            Entry("disk.img", data: Data(repeating: 0, count: 512)),
        ])
        let g4Machine = fixtureMachine(
            bytes: try Data(contentsOf: g4Archive),
            installedBytes: Int64(g4ConfigData.count + 512)
        )
        let g4Bundle = try MachineTemplateInstaller.installVerifiedArchive(
            g4Archive, machine: g4Machine, name: "G3 Template", in: directory
        )
        let g4Imported = try JSONDecoder().decode(VMConfig.self, from: Data(contentsOf: g4Bundle.appendingPathComponent("config.json")))
        XCTAssertEqual(g4Imported.ramMB, 896)
        XCTAssertFalse(g4Imported.useG4CPU)

        // Test catalog recommendedSettings on Quadra 800.
        let quadraConfig = try JSONEncoder().encode(VMConfig(name: "Quadra", machineFamily: .quadra800, ramMB: 128))
        let quadraArchive = try makeArchive([
            Entry("config.json", data: quadraConfig),
            Entry("disk.img", data: Data(repeating: 0, count: 512)),
        ])
        let quadraMachine = DownloadableMachine(
            id: "quadra-rec-v1", name: "Quadra", summary: "Quadra", osVersion: "8.1",
            minimumAppVersion: "4.8.1", archiveURL: source,
            archiveBytes: Int64(try Data(contentsOf: quadraArchive).count),
            installedBytes: Int64(quadraConfig.count + 512),
            sha256: SHA256.hash(data: try Data(contentsOf: quadraArchive)).map { String(format: "%02x", $0) }.joined(),
            machineFamily: .quadra800,
            recommendedSettings: RecommendedSettings(ram_mb: 256)
        )
        let quadraBundle = try MachineTemplateInstaller.installVerifiedArchive(
            quadraArchive, machine: quadraMachine, name: "Quadra Rec", in: directory
        )
        let quadraImported = try JSONDecoder().decode(VMConfig.self, from: Data(contentsOf: quadraBundle.appendingPathComponent("config.json")))
        XCTAssertEqual(quadraImported.ramMB, 256)

        // Test OS X host physical RAM caps (>= 4 GB -> 2048, >= 2 GB -> 1024, < 2 GB -> 512).
        var osxConfig = VMConfig(name: "Tiger", machineFamily: .powerMacOSX, ramMB: 1024)
        MachineTemplateInstaller.applyRecommendedSettings(
            RecommendedSettings(osx_ram_mb: 4096),
            to: &osxConfig,
            physicalMemory: 8 * 1_073_741_824
        )
        XCTAssertEqual(osxConfig.ramMB, 2048)

        MachineTemplateInstaller.applyRecommendedSettings(
            RecommendedSettings(osx_ram_mb: 2048),
            to: &osxConfig,
            physicalMemory: 2 * 1_073_741_824
        )
        XCTAssertEqual(osxConfig.ramMB, 1024)

        MachineTemplateInstaller.applyRecommendedSettings(
            RecommendedSettings(osx_ram_mb: 2048),
            to: &osxConfig,
            physicalMemory: 1_073_741_824
        )
        XCTAssertEqual(osxConfig.ramMB, 512)

        MachineTemplateInstaller.applyRecommendedSettings(
            RecommendedSettings(ram_mb: 128),
            to: &osxConfig,
            physicalMemory: 8 * 1_073_741_824
        )
        XCTAssertEqual(osxConfig.ramMB, 256)
    }

    func testTransferMeterSlidingWindowResumeAndFormatting() {
        var meter = TransferMeter()
        let t0 = Date(timeIntervalSinceReferenceDate: 1_000)

        // Resumed download starting at 100 MB: no rate until >= 0.15s elapsed.
        meter.record(done: 100_000_000, total: 200_000_000, at: t0)
        XCTAssertNil(meter.bytesPerSecond)
        XCTAssertNil(meter.etaSeconds)

        meter.record(done: 101_000_000, total: 200_000_000, at: t0.addingTimeInterval(0.10))
        XCTAssertNil(meter.bytesPerSecond)

        // At t0 + 1.0s: moved 10 MB over 1.0s from resumed anchor (not 110 MB).
        meter.record(done: 110_000_000, total: 200_000_000, at: t0.addingTimeInterval(1.0))
        XCTAssertEqual(try XCTUnwrap(meter.bytesPerSecond), 10_000_000, accuracy: 1.0)
        XCTAssertEqual(try XCTUnwrap(meter.etaSeconds), 9.0, accuracy: 0.01)

        // Advance beyond the 4-second window: samples older than 4s are pruned.
        meter.record(done: 120_000_000, total: 200_000_000, at: t0.addingTimeInterval(3.0))
        meter.record(done: 122_000_000, total: 200_000_000, at: t0.addingTimeInterval(5.5))
        // Samples within 4s of 5.5s are at 3.0s (120 MB) and 5.5s (122 MB): 2 MB / 2.5 s = 800_000 B/s.
        XCTAssertEqual(try XCTUnwrap(meter.bytesPerSecond), 800_000, accuracy: 1.0)

        // Done < lastDone resets the window cleanly.
        meter.record(done: 1_000_000, total: 200_000_000, at: t0.addingTimeInterval(6.0))
        XCTAssertNil(meter.bytesPerSecond)
        meter.record(done: 3_000_000, total: 200_000_000, at: t0.addingTimeInterval(7.0))
        XCTAssertEqual(try XCTUnwrap(meter.bytesPerSecond), 2_000_000, accuracy: 1.0)

        // Completed transfer clears ETA.
        meter.record(done: 200_000_000, total: 200_000_000, at: t0.addingTimeInterval(8.0))
        XCTAssertNil(meter.etaSeconds)

        // Formatting checks.
        XCTAssertEqual(TransferMeter.formatSpeed(12_500_000), "12.5 MB/s")
        XCTAssertEqual(TransferMeter.formatSpeed(512_000), "512 KB/s")
        XCTAssertEqual(TransferMeter.formatETA(14), "14 s left")
        XCTAssertEqual(TransferMeter.formatETA(135), "2m 15s left")
        XCTAssertEqual(TransferMeter.formatETA(3900), "1h 05m left")
    }

    private func fixtureMachine(bytes: Data, installedBytes: Int64 = 1024,
                                diskCapacityBytes: Int64? = nil, requiredStorageBytes: Int64? = nil) -> DownloadableMachine {
        DownloadableMachine(id: "mac-os-9-v1", name: "Mac OS 9", summary: "A ready-to-run Mac.",
            osVersion: "Mac OS 9.2.1", minimumAppVersion: "3.0.0",
            archiveURL: source, archiveBytes: Int64(bytes.count), installedBytes: installedBytes,
            sha256: SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined(),
            diskCapacityBytes: diskCapacityBytes, requiredStorageBytes: requiredStorageBytes)
    }

    private func transfer(to file: URL, expected: Int64,
                          progress: @escaping (Int64) -> Void = { _ in }) -> MachineDownloadTransfer {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [MachineDownloadURLProtocol.self]
        return MachineDownloadTransfer(source: source, destination: file, expectedBytes: expected,
                                       maximumBytes: expected, configuration: configuration, progress: progress)
    }

    private func respond(status: Int, data: Data, headers: [String: String]) {
        MachineDownloadURLProtocol.handler = { request, client, loader in
            client.urlProtocol(loader, didReceive: HTTPURLResponse(url: request.url!, statusCode: status,
                httpVersion: "HTTP/1.1", headerFields: headers)!, cacheStoragePolicy: .notAllowed)
            client.urlProtocol(loader, didLoad: data)
            client.urlProtocolDidFinishLoading(loader)
        }
    }

    private struct Entry {
        let name: String
        let data: Data
        var type: UInt8 = 48
        var link = ""
        var declaredSize: Int64?
        var badChecksum = false
        var base256 = false
        init(_ name: String, data: Data, type: UInt8 = 48, link: String = "", declaredSize: Int64? = nil,
             badChecksum: Bool = false, base256: Bool = false) {
            self.name = name; self.data = data; self.type = type; self.link = link
            self.declaredSize = declaredSize; self.badChecksum = badChecksum; self.base256 = base256
        }
    }

    private func makeArchive(_ entries: [Entry], tail: Data = Data(repeating: 0, count: 1024)) throws -> URL {
        var tar = Data()
        for entry in entries {
            var block = Data(repeating: 0, count: 512)
            func put(_ text: String, at offset: Int) { block.replaceSubrange(offset..<(offset + text.utf8.count), with: text.utf8) }
            put(entry.name, at: 0)
            put("0000600\0", at: 100)
            put("0000000\0", at: 108)
            put("0000000\0", at: 116)
            let size = entry.declaredSize ?? Int64(entry.data.count)
            if entry.base256 || size > 8_589_934_591 {
                block[124] = 0x80
                var remaining = UInt64(size)
                for offset in stride(from: 135, through: 125, by: -1) {
                    block[offset] = UInt8(remaining & 0xff)
                    remaining >>= 8
                }
            } else { put(String(format: "%011llo", size) + "\0", at: 124) }
            put("00000000000\0", at: 136)
            put("        ", at: 148)
            block[156] = entry.type
            put(entry.link, at: 157)
            put("ustar\0", at: 257)
            put("00", at: 263)
            let checksum = block.reduce(0) { $0 + Int($1) } + (entry.badChecksum ? 1 : 0)
            put(String(format: "%06o", checksum) + "\0 ", at: 148)
            tar.append(block)
            tar.append(entry.data)
            tar.append(Data(repeating: 0, count: (512 - entry.data.count % 512) % 512))
        }
        tar.append(tail)
        let raw = directory.appendingPathComponent(UUID().uuidString + ".tar")
        let archive = directory.appendingPathComponent(UUID().uuidString + ".tar.gz")
        try tar.write(to: raw)
        FileManager.default.createFile(atPath: archive.path, contents: nil)
        let output = try FileHandle(forWritingTo: archive)
        defer { try? output.close() }
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/gzip")
        process.arguments = ["-c", raw.path]
        process.standardOutput = output
        try process.run()
        process.waitUntilExit()
        XCTAssertEqual(process.terminationStatus, 0)
        return archive
    }
}

private final class MockConnectionFailure: @unchecked Sendable {
    private let lock = NSLock()
    private var failure: (() -> Void)?
    func arm(client: URLProtocolClient, loader: URLProtocol) {
        lock.lock()
        failure = { client.urlProtocol(loader, didFailWithError: URLError(.networkConnectionLost)) }
        lock.unlock()
    }
    func fail() {
        lock.lock()
        let failure = failure
        self.failure = nil
        lock.unlock()
        failure?()
    }
}

private final class MachineDownloadURLProtocol: URLProtocol {
    private final class HandlerState: @unchecked Sendable {
        let lock = NSLock()
        var handler: ((URLRequest, URLProtocolClient, URLProtocol) -> Void)?
    }
    private static let state = HandlerState()
    static var handler: ((URLRequest, URLProtocolClient, URLProtocol) -> Void)? {
        get { state.lock.lock(); defer { state.lock.unlock() }; return state.handler }
        set { state.lock.lock(); defer { state.lock.unlock() }; state.handler = newValue }
    }
    override class func canInit(with request: URLRequest) -> Bool { request.url?.host == "downloads.example.test" }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        guard let client, let handler = Self.handler else { return }
        handler(request, client, self)
    }
    override func stopLoading() {}
}
