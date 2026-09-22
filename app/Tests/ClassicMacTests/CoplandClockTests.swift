import XCTest
@testable import ClassicMac

final class CoplandClockTests: XCTestCase {
    private func image(created: UInt32, modified: UInt32) -> Data {
        var disk = Data(repeating: 0, count: 16384)
        func word(_ offset: Int, _ value: UInt16) {
            disk[offset] = UInt8(value >> 8); disk[offset + 1] = UInt8(truncatingIfNeeded: value)
        }
        func long(_ offset: Int, _ value: UInt32) {
            word(offset, UInt16(value >> 16)); word(offset + 2, UInt16(truncatingIfNeeded: value))
        }
        disk.replaceSubrange(512..<514, with: Data("PM".utf8))
        long(516, 1); long(520, 4); long(524, 28)
        disk.replaceSubrange(560..<570, with: Data("Apple_HFS\0".utf8))
        let mdb = 3072
        disk.replaceSubrange(mdb..<(mdb + 2), with: Data("BD".utf8))
        long(mdb + 20, 512); word(mdb + 28, 3)
        long(mdb + 146, 1024); word(mdb + 150, 2); word(mdb + 152, 2)
        let catalog = 4608, leaf = catalog + 512
        word(catalog + 32, 512)
        disk[leaf + 8] = 255; word(leaf + 10, 1)
        word(leaf + 510, 14); word(leaf + 508, 92)
        disk.replaceSubrange((leaf + 14)..<(leaf + 22), with: Data([7, 0, 0, 0, 0, 2, 1, 65]))
        disk[leaf + 22] = 1
        long(leaf + 32, created); long(leaf + 36, modified)
        return disk
    }

    private func withDisk(_ data: Data, _ action: (URL) throws -> Void) throws {
        let file = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try data.write(to: file)
        defer { try? FileManager.default.removeItem(at: file) }
        try action(file)
    }

    func testSeedsFromGuestDatesInsteadOfHostFileTime() throws {
        try withDisk(image(created: CoplandClock.baseline + 86400, modified: CoplandClock.baseline + 172800)) { file in
            XCTAssertEqual(try CoplandClock.latestCatalogDate(disk: file), CoplandClock.baseline + 172800)
            XCTAssertEqual(CoplandClock.initialDate(disk: file), "2027-01-03T00:00:02")
        }
    }

    func testCreationDateStillWinsAfterGuestClockWasRewound() throws {
        try withDisk(image(created: CoplandClock.baseline + 86400, modified: CoplandClock.baseline)) { file in
            XCTAssertEqual(CoplandClock.initialDate(disk: file), "2027-01-02T00:00:02")
        }
    }

    func testTruncatedAndOutOfBoundsImagesFallBackWithoutReadingArbitraryOffsets() throws {
        try withDisk(Data(repeating: 0, count: 32)) { file in
            XCTAssertThrowsError(try CoplandClock.latestCatalogDate(disk: file))
            XCTAssertEqual(CoplandClock.initialDate(disk: file), "2027-01-01T00:00:02")
        }
        var bad = image(created: 0, modified: 0)
        bad[3072 + 150] = 255; bad[3072 + 151] = 255
        try withDisk(bad) { file in
            XCTAssertThrowsError(try CoplandClock.latestCatalogDate(disk: file))
        }
    }
}
