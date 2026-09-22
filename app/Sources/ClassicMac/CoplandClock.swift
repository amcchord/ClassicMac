import Foundation

enum CoplandClock {
    // Retain the qualified RTC phase, but never go backwards across boots.
    static let baseline: UInt32 = 3_881_606_400 // 2027-01-01, Mac epoch

    static func initialDate(disk: URL) -> String {
        let seconds = max(baseline, (try? latestCatalogDate(disk: disk)) ?? 0)
        let date = Date(timeIntervalSince1970: Double(seconds) - 2_082_844_800 + 2)
        let format = DateFormatter()
        format.locale = Locale(identifier: "en_US_POSIX")
        format.timeZone = TimeZone(secondsFromGMT: 0)
        format.dateFormat = "yyyy-MM-dd'T'HH:mm:ss"
        return format.string(from: date)
    }

    // Migrate machines created before the RTC sidecar existed. Copland checks
    // creation <= modification for directories. Restarting at a constant date
    // made a previously created folder fail that check. Read actual guest dates;
    // host image mtimes and even the MDB can have been changed by offline tools.
    static func latestCatalogDate(disk: URL) throws -> UInt32 {
        let handle = try FileHandle(forReadingFrom: disk)
        defer { try? handle.close() }
        let size = try handle.seekToEnd()
        func read(_ offset: UInt64, _ count: Int) throws -> Data {
            guard count >= 0, offset <= size, UInt64(count) <= size - offset else { throw CocoaError(.fileReadCorruptFile) }
            try handle.seek(toOffset: offset)
            let bytes = try handle.read(upToCount: count) ?? Data()
            guard bytes.count == count else { throw CocoaError(.fileReadCorruptFile) }
            return bytes
        }
        func u16(_ bytes: Data, _ offset: Int) -> Int {
            Int(bytes[offset]) << 8 | Int(bytes[offset + 1])
        }
        func u32(_ bytes: Data, _ offset: Int) -> UInt32 {
            UInt32(bytes[offset]) << 24 | UInt32(bytes[offset + 1]) << 16 |
            UInt32(bytes[offset + 2]) << 8 | UInt32(bytes[offset + 3])
        }
        let first = try read(512, 512)
        guard first.prefix(2) == Data("PM".utf8) else { throw CocoaError(.fileReadCorruptFile) }
        let partitions = u32(first, 4)
        guard (1...128).contains(partitions) else { throw CocoaError(.fileReadCorruptFile) }
        for index in 1...partitions {
            let entry = try read(UInt64(index) * 512, 512)
            guard entry.prefix(2) == Data("PM".utf8) else { throw CocoaError(.fileReadCorruptFile) }
            guard entry[48..<58] == Data("Apple_HFS\0".utf8) else { continue }
            let start = UInt64(u32(entry, 8)) * 512
            let length = UInt64(u32(entry, 12)) * 512
            guard start <= size, length <= size - start, length >= 2048 else { throw CocoaError(.fileReadCorruptFile) }
            let mdb = try read(start + 1024, 512)
            guard mdb.prefix(2) == Data("BD".utf8) else { throw CocoaError(.fileReadCorruptFile) }
            let blockSize = UInt64(u32(mdb, 20))
            let allocation = UInt64(u16(mdb, 28)) * 512
            let catalogSize = Int(u32(mdb, 146))
            guard blockSize >= 512, blockSize % 512 == 0,
                  catalogSize >= 512, catalogSize <= 16 * 1024 * 1024 else { throw CocoaError(.fileReadCorruptFile) }
            var catalog = Data()
            for extent in 0..<3 where catalog.count < catalogSize {
                let offset = allocation + UInt64(u16(mdb, 150 + extent * 4)) * blockSize
                let count = min(UInt64(catalogSize - catalog.count), UInt64(u16(mdb, 152 + extent * 4)) * blockSize)
                guard offset <= length, count <= length - offset else { throw CocoaError(.fileReadCorruptFile) }
                catalog.append(try read(start + offset, Int(count)))
            }
            // The shipped images' catalogs fit in the three initial extents.
            // Later boots also have the engine's independent persistent clock.
            guard catalog.count == catalogSize else { throw CocoaError(.fileReadCorruptFile) }
            let nodeSize = u16(catalog, 32)
            guard nodeSize >= 512, nodeSize <= 32768, nodeSize.nonzeroBitCount == 1,
                  catalogSize % nodeSize == 0 else { throw CocoaError(.fileReadCorruptFile) }
            var latest: UInt32 = 0
            for base in stride(from: 0, to: catalogSize, by: nodeSize) where catalog[base + 8] == 255 {
                let count = u16(catalog, base + 10)
                guard count < nodeSize / 2 else { throw CocoaError(.fileReadCorruptFile) }
                for record in 0..<count {
                    let offset = u16(catalog, base + nodeSize - 2 * (record + 1))
                    let end = u16(catalog, base + nodeSize - 2 * (record + 2))
                    guard offset >= 14, offset < end, end <= nodeSize - 2 * (count + 1) else { throw CocoaError(.fileReadCorruptFile) }
                    let data = offset + ((Int(catalog[base + offset]) + 2) & ~1)
                    guard data < end else { throw CocoaError(.fileReadCorruptFile) }
                    let type = catalog[base + data]
                    guard type == 1 || type == 2 else { continue }
                    let dates = data + (type == 1 ? 10 : 44)
                    guard dates + 8 <= end else { throw CocoaError(.fileReadCorruptFile) }
                    latest = max(latest, u32(catalog, base + dates), u32(catalog, base + dates + 4))
                }
            }
            return latest
        }
        throw CocoaError(.fileReadCorruptFile)
    }
}
