import Foundation

// Minimal, read-only inspection of the on-disk metadata that classic Mac OS
// uses to identify a startup volume. This deliberately does not mount the
// image: it can safely inspect a just-stopped VM without Disk Arbitration UI
// or changing any HFS/HFS+ state.
enum MacDiskImage {
    private static let driverDescriptorSignature: UInt16 = 0x4552 // "ER"
    private static let partitionMapSignature: UInt16 = 0x504D // "PM"
    private static let hfsSignature: UInt16 = 0x4244 // "BD"
    private static let hfsPlusSignature: UInt16 = 0x482B // "H+"
    private static let hfsXSignature: UInt16 = 0x4858 // "HX"

    static func hasBlessedSystemFolder(at url: URL) -> Bool {
        guard let handle = try? FileHandle(forReadingFrom: url),
              let attributes = try? FileManager.default.attributesOfItem(
                atPath: url.path
              ),
              let fileSize = (attributes[.size] as? NSNumber)?.uint64Value,
              fileSize >= 1_536 else {
            return false
        }
        defer { try? handle.close() }

        guard let blockZero = read(
            handle,
            at: 0,
            count: 512,
            fileSize: fileSize
        ) else {
            return false
        }

        var blockSize: UInt64 = 512
        if uint16(blockZero, at: 0) == driverDescriptorSignature,
           let declared = uint16(blockZero, at: 2) {
            let candidate = UInt64(declared)
            if candidate >= 512,
               candidate <= 4_096,
               candidate.isPowerOfTwo {
                blockSize = candidate
            }
        }

        // Apple-partitioned disks describe their HFS volumes in one or more
        // 512-byte (or driver-declared) partition-map entries. A bare volume
        // remains useful for tests and imported images, so fall back to offset
        // zero when no partition map is present.
        guard let firstEntry = read(
            handle,
            at: blockSize,
            count: Int(blockSize),
            fileSize: fileSize
        ), uint16(firstEntry, at: 0) == partitionMapSignature else {
            return isBlessedVolume(
                handle,
                startingAt: 0,
                byteCount: fileSize,
                fileSize: fileSize
            )
        }

        guard let declaredEntries = uint32(firstEntry, at: 4),
              declaredEntries > 0 else {
            return false
        }
        // Real maps are tiny. The bound prevents a damaged image from turning
        // a metadata check into millions of reads.
        let entryCount = min(Int(declaredEntries), 4_096)
        for index in 1...entryCount {
            let (entryOffset, offsetOverflow) = blockSize.multipliedReportingOverflow(
                by: UInt64(index)
            )
            if offsetOverflow { return false }
            guard let entry = read(
                handle,
                at: entryOffset,
                count: Int(blockSize),
                fileSize: fileSize
            ), uint16(entry, at: 0) == partitionMapSignature else {
                continue
            }
            guard let type = asciiString(entry, range: 48..<80),
                  type == "Apple_HFS" || type == "Apple_HFSX",
                  let startBlock = uint32(entry, at: 8),
                  let blockCount = uint32(entry, at: 12) else {
                continue
            }
            let (start, startOverflow) = blockSize.multipliedReportingOverflow(
                by: UInt64(startBlock)
            )
            let (length, lengthOverflow) = blockSize.multipliedReportingOverflow(
                by: UInt64(blockCount)
            )
            guard !startOverflow, !lengthOverflow,
                  start < fileSize, length >= 1_536 else {
                continue
            }
            if isBlessedVolume(
                handle,
                startingAt: start,
                byteCount: length,
                fileSize: fileSize
            ) {
                return true
            }
        }
        return false
    }

    private static func isBlessedVolume(
        _ handle: FileHandle,
        startingAt volumeStart: UInt64,
        byteCount: UInt64,
        fileSize: UInt64
    ) -> Bool {
        guard let headerOffset = adding(volumeStart, 1_024),
              let header = read(
                handle,
                at: headerOffset,
                count: 512,
                fileSize: fileSize
              ), let signature = uint16(header, at: 0) else {
            return false
        }

        if signature == hfsPlusSignature || signature == hfsXSignature {
            return hfsPlusVolumeIsBlessed(header)
        }
        guard signature == hfsSignature else { return false }

        // Mac OS normally initializes HFS+ inside an HFS wrapper. The wrapper
        // itself is blessed even on an otherwise blank HFS+ disk, so only the
        // embedded volume's Finder metadata proves the installed system is
        // bootable.
        if let embeddedSignature = uint16(header, at: 124),
           embeddedSignature == hfsPlusSignature ||
            embeddedSignature == hfsXSignature {
            guard let allocationBlockSize = uint32(header, at: 20),
                  allocationBlockSize >= 512,
                  UInt64(allocationBlockSize).isPowerOfTwo,
                  let allocationStart = uint16(header, at: 28),
                  let embeddedStartBlock = uint16(header, at: 126),
                  let embeddedBlockCount = uint16(header, at: 128),
                  embeddedBlockCount > 0 else {
                return false
            }

            let allocationBase = UInt64(allocationStart) * 512
            let embeddedWithinWrapper = UInt64(embeddedStartBlock) *
                UInt64(allocationBlockSize)
            guard let relativeStart = adding(
                allocationBase,
                embeddedWithinWrapper
            ), let embeddedStart = adding(volumeStart, relativeStart) else {
                return false
            }
            let embeddedLength = UInt64(embeddedBlockCount) *
                UInt64(allocationBlockSize)
            guard let embeddedEnd = adding(relativeStart, embeddedLength),
                  embeddedEnd <= byteCount,
                  let embeddedHeaderOffset = adding(embeddedStart, 1_024),
                  let embeddedHeader = read(
                    handle,
                    at: embeddedHeaderOffset,
                    count: 512,
                    fileSize: fileSize
                  ), let actualSignature = uint16(embeddedHeader, at: 0),
                  actualSignature == hfsPlusSignature ||
                    actualSignature == hfsXSignature else {
                return false
            }
            return hfsPlusVolumeIsBlessed(embeddedHeader)
        }

        // On HFS, the first Finder-info word is the blessed System Folder's
        // catalog node ID. Zero explicitly means there is no startup system.
        return uint32(header, at: 92).map { $0 != 0 } ?? false
    }

    private static func hfsPlusVolumeIsBlessed(_ header: Data) -> Bool {
        // HFS Plus finderInfo[0] has the same meaning as the HFS field: the
        // directory ID containing the bootable system, or zero if none.
        uint32(header, at: 80).map { $0 != 0 } ?? false
    }

    private static func read(
        _ handle: FileHandle,
        at offset: UInt64,
        count: Int,
        fileSize: UInt64
    ) -> Data? {
        guard count > 0,
              let end = adding(offset, UInt64(count)),
              end <= fileSize else {
            return nil
        }
        do {
            try handle.seek(toOffset: offset)
            guard let data = try handle.read(upToCount: count),
                  data.count == count else {
                return nil
            }
            return data
        } catch {
            return nil
        }
    }

    private static func uint16(_ data: Data, at offset: Int) -> UInt16? {
        guard offset >= 0, offset + 2 <= data.count else { return nil }
        return UInt16(data[data.startIndex + offset]) << 8 |
            UInt16(data[data.startIndex + offset + 1])
    }

    private static func uint32(_ data: Data, at offset: Int) -> UInt32? {
        guard offset >= 0, offset + 4 <= data.count else { return nil }
        return UInt32(data[data.startIndex + offset]) << 24 |
            UInt32(data[data.startIndex + offset + 1]) << 16 |
            UInt32(data[data.startIndex + offset + 2]) << 8 |
            UInt32(data[data.startIndex + offset + 3])
    }

    private static func asciiString(
        _ data: Data,
        range: Range<Int>
    ) -> String? {
        guard range.lowerBound >= 0, range.upperBound <= data.count else {
            return nil
        }
        let bytes = data[range]
        let terminator = bytes.firstIndex(of: 0) ?? bytes.endIndex
        return String(data: bytes[..<terminator], encoding: .ascii)
    }

    private static func adding(_ lhs: UInt64, _ rhs: UInt64) -> UInt64? {
        let (value, overflow) = lhs.addingReportingOverflow(rhs)
        return overflow ? nil : value
    }
}

private extension UInt64 {
    var isPowerOfTwo: Bool {
        self != 0 && (self & (self - 1)) == 0
    }
}
