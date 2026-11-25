import Darwin
import Foundation

enum GuestPrinterProvisioner {
    static let defaultName = "ClassicMac Printer"
    static let defaultHost = "10.0.2.4"
    static let defaultQueue = "classicmac"
    static let laserWriter = "LaserWriter 8"
    static let papaType = "LaserWriter"
    static let papaZone = "=LPR"
    static let desktopPrinterAliasID: Int16 = 18772
    static let curPrinterID: Int16 = -8192

    static let printerExtensions = [
        "Desktop Printer Spooler",
        "Desktop PrintMonitor",
        "Desktop Printer Menu",
    ]
    static let printerControlStrip = ["Printer Selector"]

    static let printerFileTypes: Set<String> = ["dtpl", "dplt", "dtpc"]
    static let printerCreator = "dcdc"
    static let spoolerTypeCreator = ("appe", "dtps")
    static let configTypeCreator = ("dtpc", "dcdc")
    static let dbTypeCreator = ("dtpl", "dcdc")

    static let sizeResource = Data([0x5C, 0xC0, 0x00, 0x05, 0x78, 0x00, 0x00, 0x03, 0xE8, 0x00])
    static let sysHeapRefAttrs: UInt8 = 0xC0 // resSysRef (0x80) | resSysHeap (0x40)

    private static let macEpochOffset: TimeInterval = 2_082_844_800 // 1904-01-01 to 1970-01-01

    struct ResourceEntry: Equatable, Sendable {
        let type: String
        let id: Int16
        let name: String
        let attributes: UInt8
        let data: Data

        init(type: String, id: Int16, name: String = "", attributes: UInt8 = 0, data: Data) {
            self.type = type
            self.id = id
            self.name = name
            self.attributes = attributes
            self.data = data
        }
    }

    struct PAPAInfo: Equatable, Sendable {
        let name: String
        let type: String
        let zone: String
        let host: String
        let queue: String
        let tagType: String
    }

    // MARK: Structured Classic Mac Resource Builders

    static func pstr(_ text: String, limit: Int = 255) -> Data {
        let encoded = text.data(using: .macOSRoman, allowLossyConversion: true) ?? Data()
        let capped = encoded.prefix(min(limit, 255))
        var out = Data([UInt8(capped.count)])
        out.append(capped)
        return out
    }

    static func buildPAPA(
        name: String = defaultName,
        host: String = defaultHost,
        queue: String = defaultQueue
    ) -> Data {
        var header = Data()
        header.append(pstr(name, limit: 32))
        header.append(pstr(papaType, limit: 32))
        header.append(pstr(papaZone, limit: 32))

        var buf = Data(repeating: 0, count: 1024)
        let headerCopyCount = min(header.count, 104)
        buf.replaceSubrange(0..<headerCopyCount, with: header.prefix(headerCopyCount))

        var tags = Data([0x54, 0x41, 0x47, 0x53, 0x00, 0x02, 0x00, 0x04]) // "TAGS\0\2\0\4"
        let pairs: [(String, String)] = [
            ("Q   ", queue),
            ("TCP ", host),
            ("TYPE", papaZone),
        ]
        for (tag, value) in pairs {
            let tagBytes = tag.data(using: .macOSRoman, allowLossyConversion: true) ?? Data(count: 4)
            let valBytes = value.data(using: .macOSRoman, allowLossyConversion: true) ?? Data()
            tags.append(tagBytes.prefix(4))
            appendBEUInt16(&tags, UInt16(valBytes.count))
            tags.append(valBytes)
            if (valBytes.count & 1) != 0 {
                tags.append(0)
            }
        }
        let maxTags = 1024 - 104
        let tagsSlice = tags.prefix(maxTags)
        buf.replaceSubrange(104..<(104 + tagsSlice.count), with: tagsSlice)
        return buf
    }

    static func parsePAPALPR(_ data: Data) -> PAPAInfo? {
        guard data.count >= 108 else { return nil }
        let b = [UInt8](data)
        var pos = 0
        var fields: [String] = []
        for _ in 0..<3 {
            guard pos < 104 else { return nil }
            let ln = Int(b[pos])
            pos += 1
            guard pos + ln <= 104 else { return nil }
            let str = String(bytes: b[pos..<(pos + ln)], encoding: .macOSRoman) ?? ""
            fields.append(str)
            pos += ln
        }

        var tagsOut: [String: String] = [:]
        if Array(b[104..<108]) == Array("TAGS".utf8) {
            var p = 112
            while p + 6 <= b.count {
                let tagBytes = Array(b[p..<(p + 4)])
                if tagBytes == [0, 0, 0, 0] {
                    break
                }
                let vlen = Int((UInt16(b[p + 4]) << 8) | UInt16(b[p + 5]))
                p += 6
                guard p + vlen <= b.count else { break }
                let tagKey = String(bytes: tagBytes, encoding: .macOSRoman) ?? ""
                let valStr = String(bytes: b[p..<(p + vlen)], encoding: .macOSRoman) ?? ""
                tagsOut[tagKey] = valStr
                p += vlen + (vlen & 1)
            }
        }

        return PAPAInfo(
            name: fields[0],
            type: fields[1],
            zone: fields[2],
            host: tagsOut["TCP "] ?? "",
            queue: tagsOut["Q   "] ?? "",
            tagType: tagsOut["TYPE"] ?? ""
        )
    }

    static func buildSTRList(_ items: [String]) -> Data {
        var out = Data()
        appendBEUInt16(&out, UInt16(items.count))
        for item in items {
            out.append(pstr(item))
        }
        return out
    }

    static func buildCNFG(driver: String = laserWriter) -> Data {
        var buf = Data(repeating: 0, count: 40)
        var prefix = Data("nlprNLPR".utf8)
        prefix.append(pstr(driver, limit: 31))
        let count = min(prefix.count, 40)
        buf.replaceSubrange(0..<count, with: prefix.prefix(count))
        return buf
    }

    static func buildAlias(
        kind: UInt16,
        volumeName: String,
        volumeCreationDate: UInt32,
        parentName: String,
        parentCNID: UInt32,
        targetName: String,
        targetCNID: UInt32,
        targetCreationDate: UInt32,
        fullPath: String,
        typeCreator: (String, String)? = nil,
        cnidChain: [UInt32]? = nil
    ) -> Data {
        var hdr = Data(repeating: 0, count: 150)
        writeBEUInt16(&hdr, at: 6, value: 2)
        writeBEUInt16(&hdr, at: 8, value: kind)
        let vp = pstr(volumeName, limit: 27)
        hdr.replaceSubrange(10..<(10 + vp.count), with: vp)
        writeBEUInt32(&hdr, at: 38, value: volumeCreationDate)
        hdr.replaceSubrange(42..<46, with: Data([0x42, 0x44, 0x00, 0x00])) // "BD\0\0"
        writeBEUInt32(&hdr, at: 46, value: parentCNID)
        let tp = pstr(targetName, limit: 63)
        hdr.replaceSubrange(50..<(50 + tp.count), with: tp)
        writeBEUInt32(&hdr, at: 114, value: targetCNID)
        writeBEUInt32(&hdr, at: 118, value: targetCreationDate)
        if let (ftype, fcreator) = typeCreator {
            let tBytes = (ftype.data(using: .macOSRoman, allowLossyConversion: true) ?? Data(count: 4)).prefix(4)
            let cBytes = (fcreator.data(using: .macOSRoman, allowLossyConversion: true) ?? Data(count: 4)).prefix(4)
            hdr.replaceSubrange(122..<(122 + tBytes.count), with: tBytes)
            hdr.replaceSubrange(126..<(126 + cBytes.count), with: cBytes)
        }
        writeBEUInt16(&hdr, at: 130, value: 0xFFFF) // -1
        writeBEUInt16(&hdr, at: 132, value: 0xFFFF) // -1

        var tags = Data()
        func addTag(_ code: UInt16, _ value: Data) {
            appendBEUInt16(&tags, code)
            appendBEUInt16(&tags, UInt16(value.count))
            tags.append(value)
            if (value.count & 1) != 0 {
                tags.append(0)
            }
        }

        addTag(0, parentName.data(using: .macOSRoman, allowLossyConversion: true) ?? Data())
        let chain = cnidChain ?? [parentCNID]
        var chainBytes = Data()
        for c in chain {
            appendBEUInt32(&chainBytes, c)
        }
        addTag(1, chainBytes)
        addTag(2, fullPath.data(using: .macOSRoman, allowLossyConversion: true) ?? Data())
        appendBEUInt16(&tags, 0xFFFF)
        appendBEUInt16(&tags, 0)

        var out = hdr
        out.append(tags)
        writeBEUInt16(&out, at: 4, value: UInt16(out.count))
        return out
    }

    // MARK: Resource Fork Parser / Builder / Upserter

    static func parseResourceFork(_ fork: Data) -> [ResourceEntry] {
        guard fork.count >= 16 else { return [] }
        let b = [UInt8](fork)
        let doff = Int(readBEUInt32(b, at: 0))
        let moff = Int(readBEUInt32(b, at: 4))
        let dlen = Int(readBEUInt32(b, at: 8))
        let mlen = Int(readBEUInt32(b, at: 12))
        guard moff + mlen <= b.count, doff + dlen <= b.count, mlen >= 28 else { return [] }

        let rmap = Array(b[moff..<(moff + mlen)])
        let tlOff = Int(readBEUInt16(rmap, at: 24))
        let nlOff = Int(readBEUInt16(rmap, at: 26))
        guard tlOff + 2 <= rmap.count, nlOff <= rmap.count else { return [] }

        let ntypes = (Int(readBEUInt16(rmap, at: tlOff)) + 1) & 0xFFFF
        var entries: [ResourceEntry] = []

        for i in 0..<ntypes {
            let typeEntryOff = tlOff + 2 + 8 * i
            guard typeEntryOff + 8 <= rmap.count else { break }
            let rtype = String(bytes: rmap[typeEntryOff..<(typeEntryOff + 4)], encoding: .macOSRoman) ?? "????"
            let count = Int(readBEUInt16(rmap, at: typeEntryOff + 4))
            let refRel = Int(readBEUInt16(rmap, at: typeEntryOff + 6))

            for j in 0...count {
                let refOff = tlOff + refRel + 12 * j
                guard refOff + 12 <= rmap.count else { break }
                let rid = Int16(bitPattern: readBEUInt16(rmap, at: refOff))
                let noff = Int(readBEUInt16(rmap, at: refOff + 2))
                let attrOff = readBEUInt32(rmap, at: refOff + 4)
                let attrs = UInt8((attrOff >> 24) & 0xFF)
                let dataRel = Int(attrOff & 0x00FF_FFFF)
                let dataOff = doff + dataRel
                guard dataOff + 4 <= b.count else { break }
                let length = Int(readBEUInt32(b, at: dataOff))
                guard dataOff + 4 + length <= b.count else { break }

                var name = ""
                if noff != 0xFFFF, nlOff + noff < rmap.count {
                    let nlen = Int(rmap[nlOff + noff])
                    if nlOff + noff + 1 + nlen <= rmap.count {
                        name = String(bytes: rmap[(nlOff + noff + 1)..<(nlOff + noff + 1 + nlen)], encoding: .macOSRoman) ?? ""
                    }
                }
                let blob = Data(b[(dataOff + 4)..<(dataOff + 4 + length)])
                entries.append(ResourceEntry(type: rtype, id: rid, name: name, attributes: attrs, data: blob))
            }
        }
        return entries
    }

    static func buildResourceFork(_ resources: [ResourceEntry]) -> Data {
        buildResourceForkInternal(resources, systemReserved: Data(repeating: 0, count: 240), rmapMeta: Data(repeating: 0, count: 8))
    }

    static func upsertResourceFork(_ fork: Data, updates: [ResourceEntry]) -> Data {
        guard fork.count >= 256 else {
            return buildResourceFork(updates)
        }
        let b = [UInt8](fork)
        let doff = Int(readBEUInt32(b, at: 0))
        let moff = Int(readBEUInt32(b, at: 4))
        let dlen = Int(readBEUInt32(b, at: 8))
        let mlen = Int(readBEUInt32(b, at: 12))
        guard moff + mlen <= b.count, doff + dlen <= b.count, mlen >= 28 else {
            return buildResourceFork(updates)
        }

        var items = parseResourceFork(fork)
        var indexByKey: [String: Int] = [:]
        for (idx, item) in items.enumerated() {
            indexByKey["\(item.type)#\(item.id)"] = idx
        }
        for update in updates {
            let key = "\(update.type)#\(update.id)"
            if let idx = indexByKey[key] {
                items[idx] = update
            } else {
                indexByKey[key] = items.count
                items.append(update)
            }
        }

        let sysReserved = Data(b[16..<256])
        let rmapMeta = Data(b[(moff + 16)..<(moff + 24)])
        return buildResourceForkInternal(items, systemReserved: sysReserved, rmapMeta: rmapMeta)
    }

    private static func buildResourceForkInternal(
        _ resources: [ResourceEntry],
        systemReserved: Data,
        rmapMeta: Data
    ) -> Data {
        var dataSection = Data()
        var offsets: [Int] = []
        var typeOrder: [String] = []
        var byType: [String: [Int]] = [:]

        for (idx, item) in resources.enumerated() {
            offsets.append(dataSection.count)
            appendBEUInt32(&dataSection, UInt32(item.data.count))
            dataSection.append(item.data)
            if byType[item.type] == nil {
                typeOrder.append(item.type)
                byType[item.type] = [idx]
            } else {
                byType[item.type]?.append(idx)
            }
        }

        var typeList = Data()
        appendBEUInt16(&typeList, UInt16(bitPattern: Int16(truncatingIfNeeded: typeOrder.count - 1)))
        var refs = Data()
        var names = Data()
        let refBase = 2 + 8 * typeOrder.count

        for rtype in typeOrder {
            guard let idxs = byType[rtype] else { continue }
            let typeBytes = (rtype.data(using: .macOSRoman, allowLossyConversion: true) ?? Data(count: 4)).prefix(4)
            typeList.append(typeBytes)
            appendBEUInt16(&typeList, UInt16(idxs.count - 1))
            appendBEUInt16(&typeList, UInt16(refBase + refs.count))

            for idx in idxs {
                let item = resources[idx]
                var noff: UInt16 = 0xFFFF
                if !item.name.isEmpty, let nraw = item.name.data(using: .macOSRoman, allowLossyConversion: true) {
                    let capped = nraw.prefix(255)
                    noff = UInt16(names.count)
                    names.append(UInt8(capped.count))
                    names.append(capped)
                }
                let attrOff = (UInt32(item.attributes) << 24) | (UInt32(offsets[idx]) & 0x00FF_FFFF)
                appendBEUInt16(&refs, UInt16(bitPattern: item.id))
                appendBEUInt16(&refs, noff)
                appendBEUInt32(&refs, attrOff)
                appendBEUInt32(&refs, 0)
            }
        }

        let typesAndRefs = typeList + refs
        var rmap = Data(repeating: 0, count: 16)
        rmap.append(rmapMeta.prefix(8))
        appendBEUInt16(&rmap, 28)
        appendBEUInt16(&rmap, UInt16(28 + typesAndRefs.count))
        rmap.append(typesAndRefs)
        rmap.append(names)

        var header = Data()
        appendBEUInt32(&header, 256)
        appendBEUInt32(&header, UInt32(256 + dataSection.count))
        appendBEUInt32(&header, UInt32(dataSection.count))
        appendBEUInt32(&header, UInt32(rmap.count))
        rmap.replaceSubrange(0..<16, with: header)

        var reserved = systemReserved.prefix(240)
        if reserved.count < 240 {
            reserved.append(Data(repeating: 0, count: 240 - reserved.count))
        }
        return header + reserved + dataSection + rmap
    }

    static func chooseGuestPPD(names: [String]) -> String {
        let cleaned = names.filter { !$0.isEmpty && !$0.hasPrefix(".") }
        let preferred = [
            "LaserWriter Color 12/600 PS",
            "Color LaserWriter 12/600 PS",
            "LaserWriter Color 12/660 PS",
            "LaserWriter 8500",
            "LaserWriter 16/600 PS",
            "Generic",
        ]
        for pref in preferred {
            if let match = cleaned.first(where: { $0.lowercased().hasPrefix(pref.lowercased()) }) {
                return match
            }
        }
        if let lw = cleaned.first(where: { $0.lowercased().contains("laserwriter") }) {
            return lw
        }
        return cleaned.first ?? "Generic"
    }

    // MARK: Offline Mac OS 9 Disk Provisioning (hdiutil)

    static func ensurePrinterIfApplicable(for config: VMConfig) {
        let bootingFromCD = config.bootFromCD && config.cdImagePath?.isEmpty == false
        guard config.printingEnabled,
              config.machineFamily == .powerMacG4,
              !bootingFromCD,
              FileManager.default.fileExists(atPath: config.diskImageURL.path) else {
            return
        }
        do {
            try provisionMacOS9Disk(at: config.diskImageURL)
        } catch {
            NSLog("ClassicMac GuestPrinterProvisioner warning: %@", error.localizedDescription)
        }
    }

    private static func provisionMacOS9Disk(at diskURL: URL) throws {
        let fm = FileManager.default
        let mountRoot = fm.temporaryDirectory.appendingPathComponent("cm-print-mnt-\(UUID().uuidString)", isDirectory: true)
        try fm.createDirectory(at: mountRoot, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: mountRoot) }

        let attached = try hdiutilAttach(diskURL: diskURL, mountRoot: mountRoot)
        defer {
            for target in attached.detachTargets.reversed() {
                hdiutilDetach(target)
            }
        }

        guard let volumeURL = findSystemVolume(in: attached.mountPoints) else {
            return
        }
        let sysFolderURL = volumeURL.appendingPathComponent("System Folder", isDirectory: true)
        let extFolderURL = sysFolderURL.appendingPathComponent("Extensions", isDirectory: true)
        let lw8URL = extFolderURL.appendingPathComponent(laserWriter)
        guard fm.fileExists(atPath: lw8URL.path) else {
            return
        }

        let desktopURL = volumeURL.appendingPathComponent("Desktop Folder", isDirectory: true)
        if hasNonClassicMacPrinter(desktopURL: desktopURL, volumeURL: volumeURL) {
            return
        }

        // Enable disabled Desktop Printing extensions / Control Strip module if present.
        enableDisabledItems(
            from: sysFolderURL.appendingPathComponent("Extensions (Disabled)", isDirectory: true),
            to: extFolderURL,
            names: printerExtensions
        )
        enableDisabledItems(
            from: sysFolderURL.appendingPathComponent("Control Strip Modules (Disabled)", isDirectory: true),
            to: sysFolderURL.appendingPathComponent("Control Strip Modules", isDirectory: true),
            names: printerControlStrip
        )

        let printerFolderURL = desktopURL.appendingPathComponent(defaultName, isDirectory: true)
        let volSettingsURL = volumeURL.appendingPathComponent("TheVolumeSettingsFolder", isDirectory: true)
        try fm.createDirectory(at: printerFolderURL, withIntermediateDirectories: true)
        try fm.createDirectory(at: volSettingsURL, withIntermediateDirectories: true)

        // Pick guest PPD & build alias if available.
        let ppdDirURL = extFolderURL.appendingPathComponent("Printer Descriptions", isDirectory: true)
        let ppdNames = (try? fm.contentsOfDirectory(atPath: ppdDirURL.path)) ?? []
        let ppd = chooseGuestPPD(names: ppdNames)
        var ppdAlias: Data?
        if ppd != "Generic" {
            let ppdURL = ppdDirURL.appendingPathComponent(ppd)
            if fm.fileExists(atPath: ppdURL.path) {
                ppdAlias = makeFileAlias(
                    kind: 0,
                    volumeURL: volumeURL,
                    parentURL: ppdDirURL,
                    targetURL: ppdURL,
                    cnidChainURLs: [ppdDirURL, extFolderURL, sysFolderURL]
                )
            }
        }

        // 1. Write Configuration (dtpc/dcdc, invisible 0x4000).
        var cfgResources = [
            ResourceEntry(type: "PAPA", id: curPrinterID, data: buildPAPA()),
            ResourceEntry(type: "PORT", id: 128, data: Data([0xFF])),
            ResourceEntry(type: "SIZE", id: -1, data: sizeResource),
            ResourceEntry(type: "STR#", id: 128, data: buildSTRList([papaZone, defaultName, papaType])),
            ResourceEntry(type: "cnfg", id: 1, data: buildCNFG(driver: laserWriter)),
            ResourceEntry(type: "stop", id: 0, data: Data([0x00, 0x01])),
        ]
        if let ppdAlias {
            cfgResources.append(ResourceEntry(type: "alis", id: curPrinterID, data: ppdAlias))
        }
        let cfgFork = buildResourceFork(cfgResources)
        let cfgURL = printerFolderURL.appendingPathComponent("Configuration")
        try writeResourceForkFile(at: cfgURL, forkData: cfgFork, finderInfo: finderInfoBytes(type: "dtpc", creator: "dcdc", flags: 0x4000))

        // 2. Copy Desktop Printer Spooler (data + resource fork + FinderInfo).
        let spoolerSrc = extFolderURL.appendingPathComponent("Desktop Printer Spooler")
        if fm.fileExists(atPath: spoolerSrc.path) {
            let spoolerDst = printerFolderURL.appendingPathComponent("Desktop Printer Spooler")
            if fm.fileExists(atPath: spoolerDst.path) {
                try? fm.removeItem(at: spoolerDst)
            }
            try fm.copyItem(at: spoolerSrc, to: spoolerDst)
            let srcRsrc = spoolerSrc.appendingPathComponent("..namedfork/rsrc")
            if let rsrcData = try? Data(contentsOf: srcRsrc), !rsrcData.isEmpty {
                try rsrcData.write(to: spoolerDst.appendingPathComponent("..namedfork/rsrc"))
            }
            if let srcFInfo = getFinderInfo(at: spoolerSrc) {
                setFinderInfo(srcFInfo, at: spoolerDst)
            }
        }

        // 3. Register Desktop Printer folder in TheVolumeSettingsFolder:DesktopPrinters DB.
        let dbURL = volSettingsURL.appendingPathComponent("DesktopPrinters DB")
        if let dbAlias = makeFileAlias(
            kind: 1,
            volumeURL: volumeURL,
            parentURL: desktopURL,
            targetURL: printerFolderURL,
            cnidChainURLs: [desktopURL]
        ) {
            let dbFork = buildResourceFork([ResourceEntry(type: "alis", id: desktopPrinterAliasID, data: dbAlias)])
            try writeResourceForkFile(at: dbURL, forkData: dbFork, finderInfo: finderInfoBytes(type: "dtpl", creator: "dcdc", flags: 0x4000))
        }

        // 4. Update System STR -8192 & alis -8192, and LaserWriter 8 PAPA -8192.
        let sysFileURL = sysFolderURL.appendingPathComponent("System")
        let sysRsrcURL = sysFileURL.appendingPathComponent("..namedfork/rsrc")
        if let sysRsrcData = try? Data(contentsOf: sysRsrcURL), !sysRsrcData.isEmpty,
           let lw8Alias = makeFileAlias(
               kind: 0,
               volumeURL: volumeURL,
               parentURL: extFolderURL,
               targetURL: lw8URL,
               cnidChainURLs: [extFolderURL, sysFolderURL],
               typeCreatorOverride: ("PRER", "vgrd")
           ) {
            let updatedSys = upsertResourceFork(sysRsrcData, updates: [
                ResourceEntry(type: "STR ", id: curPrinterID, name: "", attributes: sysHeapRefAttrs, data: pstr(laserWriter)),
                ResourceEntry(type: "alis", id: curPrinterID, name: "", attributes: 0, data: lw8Alias),
            ])
            try updatedSys.write(to: sysRsrcURL)
        }

        let lw8RsrcURL = lw8URL.appendingPathComponent("..namedfork/rsrc")
        if let lw8RsrcData = try? Data(contentsOf: lw8RsrcURL), !lw8RsrcData.isEmpty {
            let updatedLW8 = upsertResourceFork(lw8RsrcData, updates: [
                ResourceEntry(type: "PAPA", id: curPrinterID, name: "", attributes: 0, data: buildPAPA()),
            ])
            try updatedLW8.write(to: lw8RsrcURL)
        }
    }

    // MARK: Mounted HFS+ helpers

    private static func findSystemVolume(in mountPoints: [URL]) -> URL? {
        let fm = FileManager.default
        for mp in mountPoints {
            let sf = mp.appendingPathComponent("System Folder", isDirectory: true)
            if fm.fileExists(atPath: sf.path) {
                return mp
            }
        }
        return nil
    }

    private static func isManagedPrinterName(_ name: String) -> Bool {
        let low = name.lowercased()
        return low == defaultName.lowercased() || low == "retroboot printer"
    }

    private static func hasNonClassicMacPrinter(desktopURL: URL, volumeURL: URL) -> Bool {
        let fm = FileManager.default
        if let entries = try? fm.contentsOfDirectory(at: desktopURL, includingPropertiesForKeys: [.isDirectoryKey]) {
            for entry in entries {
                let name = entry.lastPathComponent
                let isDir = (try? entry.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) ?? false
                if !isDir {
                    if let finfo = getFinderInfo(at: entry), finfo.count >= 8 {
                        let ftype = String(bytes: finfo[0..<4], encoding: .macOSRoman) ?? ""
                        let fcreator = String(bytes: finfo[4..<8], encoding: .macOSRoman) ?? ""
                        if printerFileTypes.contains(ftype) || fcreator == printerCreator {
                            if !isManagedPrinterName(name) {
                                return true
                            }
                        }
                    }
                    continue
                }
                let cfgURL = entry.appendingPathComponent("Configuration")
                let spoolURL = entry.appendingPathComponent("Desktop Printer Spooler")
                let cfgIsPrinter = getFinderInfo(at: cfgURL).map { info in
                    info.count >= 8 &&
                    String(bytes: info[0..<4], encoding: .macOSRoman) == configTypeCreator.0 &&
                    String(bytes: info[4..<8], encoding: .macOSRoman) == configTypeCreator.1
                } ?? false
                let spoolIsPrinter = getFinderInfo(at: spoolURL).map { info in
                    info.count >= 8 &&
                    String(bytes: info[0..<4], encoding: .macOSRoman) == spoolerTypeCreator.0 &&
                    String(bytes: info[4..<8], encoding: .macOSRoman) == spoolerTypeCreator.1
                } ?? false
                if cfgIsPrinter || spoolIsPrinter {
                    if !isManagedPrinterName(name) {
                        return true
                    }
                    if let rsrc = try? Data(contentsOf: cfgURL.appendingPathComponent("..namedfork/rsrc")) {
                        for entry in parseResourceFork(rsrc) where entry.type == "PAPA" && entry.id == curPrinterID {
                            if let papa = parsePAPALPR(entry.data),
                               papa.host != defaultHost && papa.host != "10.0.2.2" {
                                return true
                            }
                        }
                    }
                }
            }
        }

        let dbURL = volumeURL.appendingPathComponent("TheVolumeSettingsFolder/DesktopPrinters DB")
        if let dbRsrc = try? Data(contentsOf: dbURL.appendingPathComponent("..namedfork/rsrc")) {
            let entries = parseResourceFork(dbRsrc)
            for item in entries where item.type == "alis" && item.data.count >= 118 {
                let targetCNID = readBEUInt32([UInt8](item.data), at: 114)
                if let desktopEntries = try? fm.contentsOfDirectory(at: desktopURL, includingPropertiesForKeys: nil) {
                    for de in desktopEntries {
                        var st = stat()
                        if stat(de.path, &st) == 0, UInt32(st.st_ino) == targetCNID, !isManagedPrinterName(de.lastPathComponent) {
                            return true
                        }
                    }
                }
            }
        }
        return false
    }

    private static func enableDisabledItems(from srcDir: URL, to dstDir: URL, names: [String]) {
        let fm = FileManager.default
        guard let srcList = try? fm.contentsOfDirectory(atPath: srcDir.path) else { return }
        let dstList = Set(((try? fm.contentsOfDirectory(atPath: dstDir.path)) ?? []).map { $0.lowercased() })
        try? fm.createDirectory(at: dstDir, withIntermediateDirectories: true)
        for target in names {
            let key = target.lowercased()
            guard !dstList.contains(key),
                  let actual = srcList.first(where: { $0.lowercased() == key }) else { continue }
            let src = srcDir.appendingPathComponent(actual)
            let dst = dstDir.appendingPathComponent(actual)
            if !fm.fileExists(atPath: dst.path) {
                try? fm.moveItem(at: src, to: dst)
            }
        }
    }

    private static func writeResourceForkFile(at url: URL, forkData: Data, finderInfo: Data) throws {
        let fm = FileManager.default
        if fm.fileExists(atPath: url.path) {
            try fm.removeItem(at: url)
        }
        fm.createFile(atPath: url.path, contents: Data())
        setFinderInfo(finderInfo, at: url)
        try forkData.write(to: url.appendingPathComponent("..namedfork/rsrc"))
    }

    private static func finderInfoBytes(type: String, creator: String, flags: UInt16) -> Data {
        var data = Data(repeating: 0, count: 32)
        let t = (type.data(using: .macOSRoman, allowLossyConversion: true) ?? Data(count: 4)).prefix(4)
        let c = (creator.data(using: .macOSRoman, allowLossyConversion: true) ?? Data(count: 4)).prefix(4)
        data.replaceSubrange(0..<t.count, with: t)
        data.replaceSubrange(4..<4 + c.count, with: c)
        writeBEUInt16(&data, at: 8, value: flags)
        return data
    }

    private static func getFinderInfo(at url: URL) -> Data? {
        var buf = [UInt8](repeating: 0, count: 32)
        let size = getxattr(url.path, "com.apple.FinderInfo", &buf, buf.count, 0, 0)
        guard size >= 8 else { return nil }
        return Data(buf[..<size])
    }

    private static func setFinderInfo(_ info: Data, at url: URL) {
        var padded = info.prefix(32)
        if padded.count < 32 {
            padded.append(Data(repeating: 0, count: 32 - padded.count))
        }
        _ = padded.withUnsafeBytes { ptr in
            setxattr(url.path, "com.apple.FinderInfo", ptr.baseAddress, 32, 0, 0)
        }
    }

    private static func makeFileAlias(
        kind: UInt16,
        volumeURL: URL,
        parentURL: URL,
        targetURL: URL,
        cnidChainURLs: [URL],
        typeCreatorOverride: (String, String)? = nil
    ) -> Data? {
        guard let volCNID = cnid(of: volumeURL),
              let parentCNID = cnid(of: parentURL),
              let targetCNID = cnid(of: targetURL) else {
            return nil
        }
        _ = volCNID
        let volName = (try? volumeURL.resourceValues(forKeys: [.volumeNameKey]).volumeName) ?? volumeURL.lastPathComponent
        let volCDate = macCreationDate(of: volumeURL)
        let targetCDate = macCreationDate(of: targetURL)
        let chain = cnidChainURLs.compactMap { cnid(of: $0) }
        let typeCreator: (String, String)?
        if let override = typeCreatorOverride {
            typeCreator = override
        } else if let finfo = getFinderInfo(at: targetURL), finfo.count >= 8 {
            let t = String(bytes: finfo[0..<4], encoding: .macOSRoman) ?? ""
            let c = String(bytes: finfo[4..<8], encoding: .macOSRoman) ?? ""
            typeCreator = (t, c)
        } else {
            typeCreator = nil
        }

        let volPrefix = volumeURL.standardizedFileURL.path
        let rel = targetURL.standardizedFileURL.path
            .replacingOccurrences(of: volPrefix, with: "")
            .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            .replacingOccurrences(of: "/", with: ":")
        let fullPath = rel.isEmpty ? volName : "\(volName):\(rel)"

        return buildAlias(
            kind: kind,
            volumeName: volName,
            volumeCreationDate: volCDate,
            parentName: parentURL.lastPathComponent,
            parentCNID: parentCNID,
            targetName: targetURL.lastPathComponent,
            targetCNID: targetCNID,
            targetCreationDate: targetCDate,
            fullPath: fullPath,
            typeCreator: typeCreator,
            cnidChain: chain.isEmpty ? [parentCNID] : chain
        )
    }

    private static func cnid(of url: URL) -> UInt32? {
        var st = stat()
        guard stat(url.path, &st) == 0 else { return nil }
        return UInt32(truncatingIfNeeded: st.st_ino)
    }

    private static func macCreationDate(of url: URL) -> UInt32 {
        let date = (try? url.resourceValues(forKeys: [.creationDateKey]).creationDate) ?? Date()
        let seconds = date.timeIntervalSince1970 + macEpochOffset
        if seconds <= 0 { return 0 }
        return UInt32(min(seconds, Double(UInt32.max)))
    }

    // MARK: hdiutil attach / detach

    private struct AttachedDisk {
        let mountPoints: [URL]
        let detachTargets: [String]
    }

    private static func hdiutilAttach(diskURL: URL, mountRoot: URL) throws -> AttachedDisk {
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/usr/bin/hdiutil")
        proc.arguments = [
            "attach",
            "-imagekey", "diskimage-class=CRawDiskImage",
            "-nobrowse",
            "-plist",
            "-mountroot", mountRoot.path,
            diskURL.path,
        ]
        let outPipe = Pipe()
        let errPipe = Pipe()
        proc.standardOutput = outPipe
        proc.standardError = errPipe
        proc.standardInput = FileHandle.nullDevice
        try proc.run()
        let outData = outPipe.fileHandleForReading.readDataToEndOfFile()
        proc.waitUntilExit()
        guard proc.terminationStatus == 0 else {
            let err = String(data: errPipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
            throw NSError(domain: "GuestPrinterProvisioner", code: Int(proc.terminationStatus),
                          userInfo: [NSLocalizedDescriptionKey: "hdiutil attach failed: \(err)"])
        }

        var mountPoints: [URL] = []
        var detachTargets: [String] = []
        if let plist = try? PropertyListSerialization.propertyList(from: outData, format: nil) as? [String: Any],
           let entities = plist["system-entities"] as? [[String: Any]] {
            for entity in entities {
                if let mp = entity["mount-point"] as? String, !mp.isEmpty {
                    mountPoints.append(URL(fileURLWithPath: mp, isDirectory: true))
                }
                if let dev = entity["dev-entry"] as? String, !dev.isEmpty, detachTargets.isEmpty {
                    // Detaching the parent whole-disk device detaches all partitions cleanly.
                    detachTargets.append(dev)
                }
            }
        }
        return AttachedDisk(mountPoints: mountPoints, detachTargets: detachTargets)
    }

    private static func hdiutilDetach(_ target: String) {
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/usr/bin/hdiutil")
        proc.arguments = ["detach", target]
        proc.standardOutput = FileHandle.nullDevice
        proc.standardError = FileHandle.nullDevice
        try? proc.run()
        proc.waitUntilExit()
        if proc.terminationStatus != 0 {
            let forceProc = Process()
            forceProc.executableURL = URL(fileURLWithPath: "/usr/bin/hdiutil")
            forceProc.arguments = ["detach", "-force", target]
            forceProc.standardOutput = FileHandle.nullDevice
            forceProc.standardError = FileHandle.nullDevice
            try? forceProc.run()
            forceProc.waitUntilExit()
        }
    }

    // MARK: Big-endian byte helpers

    private static func appendBEUInt16(_ data: inout Data, _ value: UInt16) {
        var be = value.bigEndian
        data.append(Data(bytes: &be, count: 2))
    }

    private static func appendBEUInt32(_ data: inout Data, _ value: UInt32) {
        var be = value.bigEndian
        data.append(Data(bytes: &be, count: 4))
    }

    private static func writeBEUInt16(_ data: inout Data, at offset: Int, value: UInt16) {
        var be = value.bigEndian
        withUnsafeBytes(of: &be) { ptr in
            data.replaceSubrange(offset..<(offset + 2), with: ptr)
        }
    }

    private static func writeBEUInt32(_ data: inout Data, at offset: Int, value: UInt32) {
        var be = value.bigEndian
        withUnsafeBytes(of: &be) { ptr in
            data.replaceSubrange(offset..<(offset + 4), with: ptr)
        }
    }

    private static func readBEUInt16(_ bytes: [UInt8], at offset: Int) -> UInt16 {
        (UInt16(bytes[offset]) << 8) | UInt16(bytes[offset + 1])
    }

    private static func readBEUInt32(_ bytes: [UInt8], at offset: Int) -> UInt32 {
        (UInt32(bytes[offset]) << 24)
            | (UInt32(bytes[offset + 1]) << 16)
            | (UInt32(bytes[offset + 2]) << 8)
            | UInt32(bytes[offset + 3])
    }
}
