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
        XCTAssertEqual(metadata.gxMetalVersion, machine.gxMetalVersion)
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
            "--archive-url", source.absoluteString, "--id", "mac-os9-v1", "--gxmetal-version", "2.3.0",
            "--os-version", "Mac OS 9.2.1"]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        try process.run()
        process.waitUntilExit()
        XCTAssertEqual(process.terminationStatus, 0)
        let machine = try XCTUnwrap(MachineCatalog.decode(Data(contentsOf: catalog)).machines.first)
        XCTAssertEqual(machine.diskCapacityBytes, 512)
        XCTAssertEqual(machine.requiredStorageBytes, 1_048_576)
        XCTAssertEqual(machine.minimumAppVersion, "3.0.0")
        try MachineDownloadTransfer.verify(archive, machine: machine)
        let destination = try MachineTemplateInstaller.installVerifiedArchive(archive, machine: machine, name: "Packaged", in: directory)
        XCTAssertEqual(Set(try FileManager.default.contentsOfDirectory(atPath: destination.path)),
                       ["config.json", "disk.img", "template-info.json"])
        XCTAssertEqual(try Data(contentsOf: sourceBundle.appendingPathComponent("config.json")), sourceConfig)
        XCTAssertFalse(try String(contentsOf: destination.appendingPathComponent("config.json"), encoding: .utf8).contains("/Users/source"))
    }

    private func fixtureMachine(bytes: Data, installedBytes: Int64 = 1024,
                                diskCapacityBytes: Int64? = nil, requiredStorageBytes: Int64? = nil) -> DownloadableMachine {
        DownloadableMachine(id: "mac-os-9-v1", name: "Mac OS 9", summary: "A ready-to-run Mac.",
            osVersion: "Mac OS 9.2.1", gxMetalVersion: "2.3.0", minimumAppVersion: "3.0.0",
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
