import XCTest
@testable import ClassicMac

@MainActor
final class MacDiskImageTests: XCTestCase {
    func test120GBRawImageIsThinProvisioned() throws {
        guard FileManager.default.isExecutableFile(
            atPath: AppPaths.qemuImgBinary.path
        ) else {
            throw XCTSkip("The development qemu-img binary is not built")
        }
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("\(UUID().uuidString).img")
        defer { try? FileManager.default.removeItem(at: url) }

        if case let .failure(message) = QEMUManager.createRawImage(
            at: url,
            sizeArgument: "120G"
        ) {
            XCTFail(message)
            return
        }

        let values = try url.resourceValues(forKeys: [
            .fileSizeKey,
            .fileAllocatedSizeKey
        ])
        XCTAssertEqual(values.fileSize, 120 * 1_024 * 1_024 * 1_024)
        XCTAssertLessThan(values.fileAllocatedSize ?? .max, 1_024 * 1_024)
    }

    func testPartitionedHFSVolumeReportsItsBlessing() throws {
        var image = partitionedImage()
        let header = volumeStart + 1_024
        put16(0x4244, in: &image, at: header)

        try withImage(image) { url in
            XCTAssertFalse(MacDiskImage.hasBlessedSystemFolder(at: url))
        }

        put32(42, in: &image, at: header + 92)
        try withImage(image) { url in
            XCTAssertTrue(MacDiskImage.hasBlessedSystemFolder(at: url))
        }
    }

    func testBareHFSPlusVolumeReportsItsBlessing() throws {
        var image = Data(repeating: 0, count: 4 * 1_024)
        put16(0x482B, in: &image, at: 1_024)
        put32(12, in: &image, at: 1_024 + 80)

        try withImage(image) { url in
            XCTAssertTrue(MacDiskImage.hasBlessedSystemFolder(at: url))
        }
    }

    func testWrappedHFSPlusUsesEmbeddedVolumeBlessing() throws {
        var image = partitionedImage()
        let wrapperHeader = volumeStart + 1_024
        put16(0x4244, in: &image, at: wrapperHeader)
        put32(4_096, in: &image, at: wrapperHeader + 20)
        put16(4, in: &image, at: wrapperHeader + 28)
        put16(0x482B, in: &image, at: wrapperHeader + 124)
        put16(1, in: &image, at: wrapperHeader + 126)
        put16(4, in: &image, at: wrapperHeader + 128)

        // HFS+ wrappers are themselves blessed even when their embedded
        // volume is blank; that must not trigger a hard-disk startup.
        put32(2, in: &image, at: wrapperHeader + 92)
        let embeddedVolumeStart = volumeStart + (4 * 512) + 4_096
        let embeddedHeader = embeddedVolumeStart + 1_024
        put16(0x482B, in: &image, at: embeddedHeader)

        try withImage(image) { url in
            XCTAssertFalse(MacDiskImage.hasBlessedSystemFolder(at: url))
        }

        put32(101, in: &image, at: embeddedHeader + 80)
        try withImage(image) { url in
            XCTAssertTrue(MacDiskImage.hasBlessedSystemFolder(at: url))
        }
    }

    func testSuccessfulInstallerRunMakesBlessedHardDiskNext() throws {
        var image = partitionedImage()
        let header = volumeStart + 1_024
        put16(0x4244, in: &image, at: header)

        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }

        let bundle = directory.appendingPathComponent(
            "Installer Test.classic",
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: bundle,
            withIntermediateDirectories: true
        )
        var config = VMConfig(
            name: "Installer Test",
            machineFamily: .powerMacG4,
            cdImagePath: "/tmp/install.iso",
            bootFromCD: true,
            bundleURL: bundle
        )
        try image.write(to: config.diskImageURL)

        var next = QEMUManager.configurationForNextBoot(
            afterSuccessfulRun: config
        )
        XCTAssertTrue(next.bootFromCD)

        put32(42, in: &image, at: header + 92)
        try image.write(to: config.diskImageURL)
        next = QEMUManager.configurationForNextBoot(
            afterSuccessfulRun: config
        )
        XCTAssertFalse(next.bootFromCD)
        XCTAssertEqual(next.cdImagePath, config.cdImagePath)

        config.bootFromCD = false
        XCTAssertEqual(
            QEMUManager.configurationForNextBoot(afterSuccessfulRun: config),
            config
        )
    }

    private let volumeStart = 8 * 512

    private func partitionedImage() -> Data {
        var image = Data(repeating: 0, count: 32 * 1_024)
        put16(0x4552, in: &image, at: 0)
        put16(512, in: &image, at: 2)

        let mapEntry = 512
        put16(0x504D, in: &image, at: mapEntry)
        put32(2, in: &image, at: mapEntry + 4)
        put32(1, in: &image, at: mapEntry + 8)
        put32(2, in: &image, at: mapEntry + 12)
        putASCII(
            "Apple_partition_map",
            in: &image,
            at: mapEntry + 48,
            length: 32
        )

        let volumeEntry = 1_024
        put16(0x504D, in: &image, at: volumeEntry)
        put32(2, in: &image, at: volumeEntry + 4)
        put32(8, in: &image, at: volumeEntry + 8)
        put32(48, in: &image, at: volumeEntry + 12)
        putASCII(
            "Apple_HFS",
            in: &image,
            at: volumeEntry + 48,
            length: 32
        )
        return image
    }

    private func withImage(
        _ data: Data,
        body: (URL) throws -> Void
    ) throws {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("\(UUID().uuidString).img")
        try data.write(to: url)
        defer { try? FileManager.default.removeItem(at: url) }
        try body(url)
    }

    private func put16(_ value: UInt16, in data: inout Data, at offset: Int) {
        data[offset] = UInt8((value >> 8) & 0xFF)
        data[offset + 1] = UInt8(value & 0xFF)
    }

    private func put32(_ value: UInt32, in data: inout Data, at offset: Int) {
        data[offset] = UInt8((value >> 24) & 0xFF)
        data[offset + 1] = UInt8((value >> 16) & 0xFF)
        data[offset + 2] = UInt8((value >> 8) & 0xFF)
        data[offset + 3] = UInt8(value & 0xFF)
    }

    private func putASCII(
        _ value: String,
        in data: inout Data,
        at offset: Int,
        length: Int
    ) {
        let bytes = Array(value.utf8.prefix(length))
        data.replaceSubrange(offset..<(offset + bytes.count), with: bytes)
    }
}
