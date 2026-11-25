import Foundation

enum OSXNVRAMError: LocalizedError, Equatable {
    case damaged
    case unreadable
    case ambiguousClock
    case damagedPartition
    case incompleteClock
    case invalidClock

    var errorDescription: String? {
        switch self {
        case .damaged:
            return "This Mac's OS X startup memory is damaged. Restore a checkpoint or import the Mac again."
        case .unreadable:
            return "This Mac's OS X startup memory could not be read."
        case .ambiguousClock:
            return "This Mac's startup-memory clock record is ambiguous."
        case .damagedPartition:
            return "This Mac's startup-memory partition record is damaged."
        case .incompleteClock:
            return "This Mac's startup-memory clock record is incomplete."
        case .invalidClock:
            return "This Mac's startup-memory clock record is not valid."
        }
    }
}

/// Validates PowerPC Mac OS X 16 KB flash NVRAM (`nvram.img`) and computes
/// QEMU's `-rtc base=...` argument from signed 24-bit `XPRAM[0xEC]`.
enum OSXNVRAM {
    static let byteCount = 16_384
    static let bankSize = 8_192
    private static let xpramPartitionName = Data("APL,MacOS75".utf8)
    private static let maxRTCOffset = 86_400

    static func headerSum(_ header: Data) -> UInt8 {
        var total = 0
        for (index, byte) in header.enumerated() where index != 1 {
            total += Int(byte)
        }
        while total > 255 {
            total = (total & 255) + (total >> 8)
        }
        return UInt8(total)
    }

    static func adler32(_ data: Data) -> UInt32 {
        let mod: UInt32 = 65_521
        var s1: UInt32 = 1
        var s2: UInt32 = 0
        for byte in data {
            s1 = (s1 + UInt32(byte)) % mod
            s2 = (s2 + s1) % mod
        }
        return (s2 << 16) | s1
    }

    static func validBanks(in data: Data) -> [Data] {
        guard data.count == byteCount else { return [] }
        var banks: [Data] = []
        let bytes = [UInt8](data)
        for offset in [0, bankSize] {
            let bank = Data(bytes[offset..<(offset + bankSize)])
            let header = Data(bank[0..<16])
            let generationMagic = be16(bank, at: 2)
            let checksum = be32(bank, at: 16)
            if bank[0] == 0x5A,
               generationMagic == 2,
               bank[1] == headerSum(header),
               checksum == adler32(Data(bank[20..<bankSize])) {
                banks.append(bank)
            }
        }
        return banks
    }

    /// One complete bank may survive an interrupted native flash update.
    @discardableResult
    static func validate(data: Data) throws -> Data {
        guard !validBanks(in: data).isEmpty else {
            throw OSXNVRAMError.damaged
        }
        return data
    }

    @discardableResult
    static func validate(at url: URL) throws -> Data {
        let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey])
        guard values?.isRegularFile == true, values?.isSymbolicLink == false else {
            throw OSXNVRAMError.unreadable
        }
        guard values?.fileSize == byteCount else {
            throw OSXNVRAMError.damaged
        }
        let data: Data
        do {
            data = try Data(contentsOf: url)
        } catch {
            throw OSXNVRAMError.unreadable
        }
        return try validate(data: data)
    }

    /// Native PPC XNU subtracts signed 24-bit `XPRAM[0xEC]` from its local RTC.
    /// Select the valid newest bank as the pinned firmware does.
    static func rtcOffset(data: Data) throws -> Int {
        let validated = try validate(data: data)
        var banks = validBanks(in: validated)
        banks.sort { be32($0, at: 20) < be32($1, at: 20) }
        if banks.count == 2 {
            let gen0 = be32(banks[0], at: 20)
            let gen1 = be32(banks[1], at: 20)
            if (gen0 == gen1 && banks[0] != banks[1]) || (gen1 - gen0) >= 0x8000_0000 {
                throw OSXNVRAMError.ambiguousClock
            }
        }
        let bank = banks[banks.count - 1]
        var offset = 0
        var found: [Int] = []
        while offset < bank.count {
            guard offset + 16 <= bank.count else {
                throw OSXNVRAMError.damagedPartition
            }
            let header = Data(bank[offset..<(offset + 16)])
            guard header[1] == headerSum(header) else {
                throw OSXNVRAMError.damagedPartition
            }
            let length = Int(be16(header, at: 2)) * 16
            guard length >= 16, offset + length <= bank.count else {
                throw OSXNVRAMError.damagedPartition
            }
            var nameEnd = 16
            while nameEnd > 4 && header[nameEnd - 1] == 0 {
                nameEnd -= 1
            }
            if Data(header[4..<nameEnd]) == xpramPartitionName {
                guard length >= 16 + 256 else {
                    throw OSXNVRAMError.incompleteClock
                }
                let raw = be32(bank, at: offset + 16 + 0xEC) & 0x00FF_FFFF
                let signed = (raw & 0x0080_0000) != 0 ? Int(raw) - 0x0100_0000 : Int(raw)
                found.append(signed)
            }
            offset += length
        }
        guard found.count == 1, (-maxRTCOffset...maxRTCOffset).contains(found[0]) else {
            throw OSXNVRAMError.invalidClock
        }
        return found[0]
    }

    static func rtcBase(data: Data, now: Date = Date()) throws -> String {
        let offset = try rtcOffset(data: data)
        if offset == 0 {
            return "base=utc"
        }
        let instant = now.addingTimeInterval(TimeInterval(offset))
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(secondsFromGMT: 0)!
        let c = calendar.dateComponents([.year, .month, .day, .hour, .minute, .second], from: instant)
        return String(
            format: "base=%04d-%02d-%02dT%02d:%02d:%02d",
            c.year ?? 1970, c.month ?? 1, c.day ?? 1,
            c.hour ?? 0, c.minute ?? 0, c.second ?? 0
        )
    }

    static func rtcBase(at url: URL, now: Date = Date()) -> String {
        guard FileManager.default.fileExists(atPath: url.path),
              let data = try? validate(at: url),
              let base = try? rtcBase(data: data, now: now) else {
            return "base=utc"
        }
        return base
    }

    private static func be16(_ data: Data, at index: Int) -> UInt16 {
        let base = data.startIndex + index
        return (UInt16(data[base]) << 8) | UInt16(data[base + 1])
    }

    private static func be32(_ data: Data, at index: Int) -> UInt32 {
        let base = data.startIndex + index
        return (UInt32(data[base]) << 24)
            | (UInt32(data[base + 1]) << 16)
            | (UInt32(data[base + 2]) << 8)
            | UInt32(data[base + 3])
    }
}
