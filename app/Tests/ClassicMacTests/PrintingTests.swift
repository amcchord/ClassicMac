import Darwin
import XCTest
@testable import ClassicMac

final class PrintingTests: XCTestCase {

    // MARK: - VMConfig Printing Settings

    func testVMConfigPrintingDefaultsRoundTripLegacyAndCoplandSanitization() throws {
        let encoder = JSONEncoder()
        let decoder = JSONDecoder()

        let defaults = VMConfig(name: "Default", machineFamily: .powerMacG4)
        XCTAssertFalse(defaults.printingEnabled)
        XCTAssertNil(defaults.printerName)

        let configured = VMConfig(
            name: "Office Mac",
            machineFamily: .powerMacG4,
            printingEnabled: true,
            printerName: "  HP_LaserJet  "
        )
        XCTAssertTrue(configured.printingEnabled)
        XCTAssertEqual(configured.printerName, "HP_LaserJet")

        let decoded = try decoder.decode(VMConfig.self, from: encoder.encode(configured))
        XCTAssertTrue(decoded.printingEnabled)
        XCTAssertEqual(decoded.printerName, "HP_LaserJet")

        // Whitespace-only printerName normalizes to nil.
        let blankPrinter = VMConfig(
            name: "Blank",
            machineFamily: .quadra800,
            printingEnabled: true,
            printerName: "   \n "
        )
        XCTAssertNil(blankPrinter.printerName)
        XCTAssertTrue(blankPrinter.printingEnabled)

        // Legacy JSON missing printing keys defaults cleanly.
        var legacyObj = try XCTUnwrap(
            JSONSerialization.jsonObject(with: encoder.encode(configured)) as? [String: Any]
        )
        legacyObj.removeValue(forKey: "printingEnabled")
        legacyObj.removeValue(forKey: "printerName")
        let legacyDecoded = try decoder.decode(
            VMConfig.self,
            from: JSONSerialization.data(withJSONObject: legacyObj)
        )
        XCTAssertFalse(legacyDecoded.printingEnabled)
        XCTAssertNil(legacyDecoded.printerName)

        // Copland disables printing and clears printerName.
        let copland = VMConfig(
            name: "Copland",
            machineFamily: .powerMac7500,
            printingEnabled: true,
            printerName: "pdf"
        )
        XCTAssertFalse(copland.machineFamily.supportsPrinting)
        XCTAssertFalse(copland.printingEnabled)
        XCTAssertNil(copland.printerName)
    }

    // MARK: - QEMURunner Arguments

    @MainActor
    func testQEMURunnerPrintingArgumentsAcrossFamilies() {
        let ports = QEMUManager.PrinterForwardPorts(lpd: 51515, ipp: 51631, raw: 59100)
        let expectedSuffix = "guestfwd=tcp:10.0.2.4:515-cmd:/usr/bin/nc 127.0.0.1 51515,"
            + "guestfwd=tcp:10.0.2.4:631-cmd:/usr/bin/nc 127.0.0.1 51631,"
            + "guestfwd=tcp:10.0.2.4:9100-cmd:/usr/bin/nc 127.0.0.1 59100"

        for (family, model) in [
            (MachineFamily.powerMacG4, "sungem"),
            (MachineFamily.quadra800, "dp83932"),
            (MachineFamily.powerMacOSX, "sungem"),
        ] {
            let netOn = VMConfig(name: "NetOn", machineFamily: family, networking: true, printingEnabled: true)
            let baseline = QEMUManager.buildArguments(for: netOn)
            XCTAssertEqual(optionValues("-nic", in: baseline), ["user,model=\(model)"])

            let withPrint = QEMUManager.buildArguments(for: netOn, printerPorts: ports)
            XCTAssertEqual(optionValues("-nic", in: withPrint), ["user,model=\(model),\(expectedSuffix)"])

            let netOff = VMConfig(name: "NetOff", machineFamily: family, networking: false, printingEnabled: true)
            let baselineOff = QEMUManager.buildArguments(for: netOff)
            XCTAssertEqual(optionValues("-nic", in: baselineOff), ["none"])

            let withPrintRestricted = QEMUManager.buildArguments(for: netOff, printerPorts: ports)
            XCTAssertEqual(
                optionValues("-nic", in: withPrintRestricted),
                ["user,restrict=on,model=\(model),\(expectedSuffix)"]
            )
        }
    }

    private func optionValues(_ option: String, in arguments: [String]) -> [String] {
        arguments.indices.compactMap { index in
            guard arguments[index] == option, arguments.indices.contains(index + 1) else {
                return nil
            }
            return arguments[index + 1]
        }
    }

    // MARK: - HostPrinterManager Helpers & Save as PDF

    func testHostPrinterManagerSanitizationEOTAndTestPS() throws {
        XCTAssertEqual(HostPrinterManager.sanitizeJobName("Report: Q4/2026.ps"), "Report Q4 2026")
        XCTAssertEqual(HostPrinterManager.sanitizeJobName("   ...___   "), "Mac OS Print")
        XCTAssertEqual(HostPrinterManager.sanitizeJobName("Drawing (Final).pdf"), "Drawing (Final)")
        XCTAssertEqual(HostPrinterManager.sanitizeJobName(String(repeating: "A", count: 90)).count, 64)

        let rawWithEOT = Data([0x04, 0x04]) + Data("Hello\u{04}World".utf8) + Data([0x04])
        XCTAssertEqual(HostPrinterManager.stripEOT(rawWithEOT), Data("Hello\u{04}World".utf8))

        let ps = HostPrinterManager.generateTestPostScript(targetLabel: "Save as PDF (Test)", version: "4.9.0")
        let psText = try XCTUnwrap(String(data: ps, encoding: .ascii))
        XCTAssertTrue(psText.hasPrefix("%!PS-Adobe-3.0"))
        XCTAssertTrue(psText.contains("ClassicMac 4.9.0 - Printer Test Page"))
        XCTAssertTrue(psText.contains("Target: Save as PDF \\(Test\\)"))
        XCTAssertTrue(psText.contains("ClassicMac printing is working."))
    }

    func testSaveAsPDFWritesAtomicallyAndHandlesCollisions() throws {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("cm-pdf-out-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: dir) }

        let pdfPayload = Data("%PDF-1.4\n1 0 obj\n<<>>\nendobj\ntrailer\n<<>>\n%%EOF\n".utf8)
        let first = try HostPrinterManager.submitJob(
            data: Data([0x04]) + pdfPayload + Data([0x04]),
            jobName: "Invoice 01.ps",
            targetPrinter: HostPrinterManager.pdfTargetValue,
            pdfOutputDir: dir
        )
        let firstURL = try XCTUnwrap(first.savedPDFURL)
        XCTAssertTrue(FileManager.default.fileExists(atPath: firstURL.path))
        XCTAssertEqual(try Data(contentsOf: firstURL), pdfPayload)
        XCTAssertEqual(first.jobName, "Invoice 01")

        let second = try HostPrinterManager.submitJob(
            data: pdfPayload,
            jobName: "Invoice 01",
            targetPrinter: HostPrinterManager.pdfTargetValue,
            pdfOutputDir: dir
        )
        let secondURL = try XCTUnwrap(second.savedPDFURL)
        XCTAssertNotEqual(firstURL, secondURL)
        XCTAssertTrue(FileManager.default.fileExists(atPath: secondURL.path))

        XCTAssertThrowsError(
            try HostPrinterManager.submitJob(
                data: Data([0x04, 0x04]),
                jobName: "Empty",
                targetPrinter: HostPrinterManager.pdfTargetValue,
                pdfOutputDir: dir
            )
        )
    }

    // MARK: - Live Loopback PrintServer (LPD, IPP, Raw 9100)

    func testPrintServerLPDStandardAndOS9Quirks() throws {
        final class Capture: @unchecked Sendable {
            let lock = NSLock()
            let sem = DispatchSemaphore(value: 0)
            var jobs: [(Data, String, String, String)] = []
        }
        let capture = Capture()
        let server = PrintServer(
            targetPrinter: "pdf",
            pdfOutputDir: FileManager.default.temporaryDirectory,
            jobHandler: { data, jobName, source, queue in
                capture.lock.lock()
                capture.jobs.append((data, jobName, source, queue))
                capture.lock.unlock()
                capture.sem.signal()
                return PrintJobResult(jobName: jobName, target: "pdf", bytes: data.count, savedPDFURL: nil)
            }
        )
        try server.start()
        defer { server.stop() }

        // 1. Queue query (cmd 3) returns "no entries\n".
        let qfd = try connectLoopback(port: server.boundLPDPort)
        try sendAll(qfd, Data([0x03]) + Data("classicmac\n".utf8))
        let qResp = try recvUntilEOF(qfd)
        Darwin.close(qfd)
        XCTAssertEqual(String(data: qResp, encoding: .utf8), "no entries\n")

        // 2. Classic Mac OS 9 LPD with NUL padding and duplicate subcommand header re-send.
        let fd = try connectLoopback(port: server.boundLPDPort)
        try sendAll(fd, Data([0x02]) + Data("classicmac\n".utf8))
        XCTAssertEqual(try recvExact(fd, count: 1), Data([0x00]))

        let controlBytes = Data("Hmac\nPuser\nJClassic Letter\n".utf8)
        let ctrlSub = Data("\u{02}\(controlBytes.count) cfA001mac\n".utf8)
        // Leading NUL padding before subcommand.
        try sendAll(fd, Data([0x00, 0x00]) + ctrlSub)
        XCTAssertEqual(try recvExact(fd, count: 1), Data([0x00]))
        // Re-send subcommand header right after ACK (Desktop PrintMonitor quirk), expect 2nd ACK.
        try sendAll(fd, ctrlSub)
        XCTAssertEqual(try recvExact(fd, count: 1), Data([0x00]))
        try sendAll(fd, controlBytes + Data([0x00]))
        XCTAssertEqual(try recvExact(fd, count: 1), Data([0x00]))

        let psPayload = Data("\u{04}%!PS-Adobe-3.0\nshowpage\n\u{04}".utf8)
        let dataSub = Data("\u{03}\(psPayload.count) dfA001mac\n".utf8)
        try sendAll(fd, dataSub)
        XCTAssertEqual(try recvExact(fd, count: 1), Data([0x00]))
        try sendAll(fd, psPayload + Data([0x00]))
        XCTAssertEqual(try recvExact(fd, count: 1), Data([0x00]))
        Darwin.close(fd)

        XCTAssertEqual(capture.sem.wait(timeout: .now() + 5), .success)
        capture.lock.lock()
        let job = capture.jobs.first
        capture.lock.unlock()
        XCTAssertEqual(job?.1, "Classic Letter")
        XCTAssertEqual(job?.2, "lpd")
        XCTAssertEqual(job?.3, "classicmac")
        XCTAssertEqual(job?.0, Data("%!PS-Adobe-3.0\nshowpage\n".utf8))

        // 3. LPD count == 0 reads until EOF.
        let fd0 = try connectLoopback(port: server.boundLPDPort)
        try sendAll(fd0, Data("\u{02}myq\n".utf8))
        XCTAssertEqual(try recvExact(fd0, count: 1), Data([0x00]))
        try sendAll(fd0, Data("\u{03}0 dfA002mac\n".utf8))
        XCTAssertEqual(try recvExact(fd0, count: 1), Data([0x00]))
        try sendAll(fd0, Data("%PDF-1.4\n%%EOF\n".utf8))
        Darwin.shutdown(fd0, SHUT_WR)
        XCTAssertEqual(try recvExact(fd0, count: 1), Data([0x00]))
        Darwin.close(fd0)

        XCTAssertEqual(capture.sem.wait(timeout: .now() + 5), .success)
        capture.lock.lock()
        let job0 = capture.jobs.last
        capture.lock.unlock()
        XCTAssertEqual(job0?.0, Data("%PDF-1.4\n%%EOF\n".utf8))
        XCTAssertEqual(job0?.3, "myq")
    }

    func testPrintServerRaw9100ExtractsMacRomanTitle() throws {
        final class Capture: @unchecked Sendable {
            let sem = DispatchSemaphore(value: 0)
            var captured: (Data, String, String)?
        }
        let capture = Capture()
        let server = PrintServer(
            targetPrinter: "pdf",
            pdfOutputDir: FileManager.default.temporaryDirectory,
            jobHandler: { data, jobName, source, _ in
                capture.captured = (data, jobName, source)
                capture.sem.signal()
                return PrintJobResult(jobName: jobName, target: "pdf", bytes: data.count, savedPDFURL: nil)
            }
        )
        try server.start()
        defer { server.stop() }

        let fd = try connectLoopback(port: server.boundRawPort)
        // MacRoman 0x8E is é
        var ps = Data("%!PS-Adobe-3.0\r%%Title: (R".utf8)
        ps.append(0x8E)
        ps.append(Data("sum".utf8))
        ps.append(0x8E)
        ps.append(Data(")\rshowpage\r".utf8))
        try sendAll(fd, Data([0x04]) + ps + Data([0x04]))
        Darwin.close(fd)

        XCTAssertEqual(capture.sem.wait(timeout: .now() + 5), .success)
        XCTAssertEqual(capture.captured?.1, "R\u{00E9}sum\u{00E9}")
        XCTAssertEqual(capture.captured?.2, "raw")
        XCTAssertEqual(capture.captured?.0, ps)
    }

    func testPrintServerIPPOperations100ContinueAndChunked() throws {
        final class Capture: @unchecked Sendable {
            let lock = NSLock()
            var jobs: [(Data, String, String, String)] = []
        }
        let capture = Capture()
        let server = PrintServer(
            targetPrinter: "pdf",
            pdfOutputDir: FileManager.default.temporaryDirectory,
            jobHandler: { data, jobName, source, queue in
                capture.lock.lock()
                capture.jobs.append((data, jobName, source, queue))
                capture.lock.unlock()
                return PrintJobResult(jobName: jobName, target: "pdf", bytes: data.count, savedPDFURL: nil)
            }
        )
        try server.start()
        defer { server.stop() }

        let fd = try connectLoopback(port: server.boundIPPPort)
        defer { Darwin.close(fd) }

        // 1. GET / returns plain text.
        try sendAll(fd, Data("GET / HTTP/1.1\r\nHost: 10.0.2.4\r\n\r\n".utf8))
        let getResp = try readHTTPResponse(fd)
        XCTAssertTrue(getResp.header.contains("200 OK"))
        XCTAssertTrue(String(data: getResp.body, encoding: .utf8)?.contains("ClassicMac Printer") == true)

        // 2. Get-Printer-Attributes (0x000B)
        let getAttrsReq = buildIPPRequest(opID: 0x000B, reqID: 10, attrs: [])
        let attrsResp = try postIPP(fd: fd, path: "/printers/classicmac", ippBody: getAttrsReq)
        XCTAssertEqual(ippStatus(attrsResp), 0x0000)

        // 3. Validate-Job (0x0004)
        let valResp = try postIPP(fd: fd, path: "/printers/classicmac", ippBody: buildIPPRequest(opID: 0x0004, reqID: 11, attrs: []))
        XCTAssertEqual(ippStatus(valResp), 0x0000)

        // 4. Create-Job (0x0005) + multi-chunk Send-Document (0x0006)
        let createReq = buildIPPRequest(opID: 0x0005, reqID: 12, attrs: [
            (0x42, "job-name", Data("MultiPart".utf8))
        ])
        let createResp = try postIPP(fd: fd, path: "/printers/classicmac", ippBody: createReq)
        XCTAssertEqual(ippStatus(createResp), 0x0000)
        let jid1 = try XCTUnwrap(extractIPPJobID(createResp))

        let send1 = buildIPPRequest(
            opID: 0x0006,
            reqID: 13,
            attrs: [
                (0x21, "job-id", PrintServer.ippInt32Data(jid1)),
                (0x22, "last-document", Data([0x00])),
            ],
            payload: Data("%!PS-Adobe-3.0\n".utf8)
        )
        let send1Resp = try postIPP(fd: fd, path: "/printers/classicmac", ippBody: send1)
        XCTAssertEqual(ippStatus(send1Resp), 0x0000)

        let send2 = buildIPPRequest(
            opID: 0x0006,
            reqID: 14,
            attrs: [
                (0x21, "job-id", PrintServer.ippInt32Data(jid1)),
                (0x22, "last-document", Data([0x01])),
            ],
            payload: Data("showpage\n".utf8)
        )
        let send2Resp = try postIPP(fd: fd, path: "/printers/classicmac", ippBody: send2)
        XCTAssertEqual(ippStatus(send2Resp), 0x0000)

        // 5. Create-Job + Cancel-Job (0x0008), then Send-Document returns 0x0407.
        let create2Resp = try postIPP(fd: fd, path: "/printers/classicmac", ippBody: buildIPPRequest(opID: 0x0005, reqID: 15, attrs: []))
        let jid2 = try XCTUnwrap(extractIPPJobID(create2Resp))
        let cancelResp = try postIPP(
            fd: fd,
            path: "/printers/classicmac",
            ippBody: buildIPPRequest(opID: 0x0008, reqID: 16, attrs: [(0x21, "job-id", PrintServer.ippInt32Data(jid2))])
        )
        XCTAssertEqual(ippStatus(cancelResp), 0x0000)
        let sendCanceled = try postIPP(
            fd: fd,
            path: "/printers/classicmac",
            ippBody: buildIPPRequest(
                opID: 0x0006,
                reqID: 17,
                attrs: [(0x21, "job-id", PrintServer.ippInt32Data(jid2))],
                payload: Data("ignored".utf8)
            )
        )
        XCTAssertEqual(ippStatus(sendCanceled), 0x0407)

        // 6. Get-Job-Attributes (0x0009) & Get-Jobs (0x000A)
        let getJobResp = try postIPP(
            fd: fd,
            path: "/printers/classicmac",
            ippBody: buildIPPRequest(opID: 0x0009, reqID: 18, attrs: [(0x21, "job-id", PrintServer.ippInt32Data(jid1))])
        )
        XCTAssertEqual(ippStatus(getJobResp), 0x0000)
        let getJobsResp = try postIPP(fd: fd, path: "/printers/classicmac", ippBody: buildIPPRequest(opID: 0x000A, reqID: 19, attrs: []))
        XCTAssertEqual(ippStatus(getJobsResp), 0x0000)

        // 7. Print-Job (0x0002) with Expect: 100-continue and Transfer-Encoding: chunked.
        let printJobIPP = buildIPPRequest(
            opID: 0x0002,
            reqID: 20,
            attrs: [(0x42, "job-name", Data("DirectIPPDoc".utf8))],
            payload: Data("%PDF-1.4\n%%EOF\n".utf8)
        )
        let reqHdr = "POST /printers/office HTTP/1.1\r\nHost: 10.0.2.4\r\nContent-Type: application/ipp\r\nExpect: 100-continue\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
        try sendAll(fd, Data(reqHdr.utf8))
        let cont = try readHTTPStatusLineAndEmptyLine(fd)
        XCTAssertTrue(cont.contains("100 Continue"))

        let half = printJobIPP.count / 2
        let part1 = printJobIPP.prefix(half)
        let part2 = printJobIPP.dropFirst(half)
        var chunked = Data("\(String(part1.count, radix: 16))\r\n".utf8)
        chunked.append(part1)
        chunked.append(Data("\r\n\(String(part2.count, radix: 16))\r\n".utf8))
        chunked.append(part2)
        chunked.append(Data("\r\n0\r\n\r\n".utf8))
        try sendAll(fd, chunked)
        let printResp = try readHTTPResponse(fd)
        XCTAssertEqual(ippStatus(printResp.body), 0x0000)

        capture.lock.lock()
        let recorded = capture.jobs
        capture.lock.unlock()
        XCTAssertEqual(recorded.count, 2)
        XCTAssertEqual(recorded[0].0, Data("%!PS-Adobe-3.0\nshowpage\n".utf8))
        XCTAssertEqual(recorded[0].1, "MultiPart")
        XCTAssertEqual(recorded[0].2, "ipp")
        XCTAssertEqual(recorded[0].3, "classicmac")
        XCTAssertEqual(recorded[1].0, Data("%PDF-1.4\n%%EOF\n".utf8))
        XCTAssertEqual(recorded[1].1, "DirectIPPDoc")
        XCTAssertEqual(recorded[1].3, "office")
    }

    // MARK: - GuestPrinterProvisioner Pure Swift Resource & Record Tests

    func testGuestPrinterProvisionerBuildersAndResourceForkUpsert() throws {
        let papa = GuestPrinterProvisioner.buildPAPA(
            name: "ClassicMac Printer",
            host: "10.0.2.4",
            queue: "classicmac"
        )
        XCTAssertEqual(papa.count, 1024)
        let parsed = try XCTUnwrap(GuestPrinterProvisioner.parsePAPALPR(papa))
        XCTAssertEqual(parsed.name, "ClassicMac Printer")
        XCTAssertEqual(parsed.type, "LaserWriter")
        XCTAssertEqual(parsed.zone, "=LPR")
        XCTAssertEqual(parsed.host, "10.0.2.4")
        XCTAssertEqual(parsed.queue, "classicmac")
        XCTAssertEqual(parsed.tagType, "=LPR")

        let cnfg = GuestPrinterProvisioner.buildCNFG()
        XCTAssertEqual(cnfg.count, 40)
        XCTAssertTrue(cnfg.starts(with: Data("nlprNLPR".utf8)))
        XCTAssertEqual(cnfg[8], UInt8("LaserWriter 8".count))

        let strList = GuestPrinterProvisioner.buildSTRList(["=LPR", "ClassicMac Printer", "LaserWriter"])
        XCTAssertEqual(strList[0], 0)
        XCTAssertEqual(strList[1], 3)

        let alias = GuestPrinterProvisioner.buildAlias(
            kind: 0,
            volumeName: "Macintosh HD",
            volumeCreationDate: 0xABCD1234,
            parentName: "Extensions",
            parentCNID: 22,
            targetName: "LaserWriter 8",
            targetCNID: 99,
            targetCreationDate: 0x12345678,
            fullPath: "Macintosh HD:System Folder:Extensions:LaserWriter 8",
            typeCreator: ("PRER", "vgrd"),
            cnidChain: [22, 10]
        )
        XCTAssertGreaterThan(alias.count, 150)
        let encodedLen = (UInt16(alias[4]) << 8) | UInt16(alias[5])
        XCTAssertEqual(Int(encodedLen), alias.count)

        XCTAssertEqual(
            GuestPrinterProvisioner.chooseGuestPPD(names: [".DS_Store", "Generic", "LaserWriter 8500"]),
            "LaserWriter 8500"
        )
        XCTAssertEqual(
            GuestPrinterProvisioner.chooseGuestPPD(names: ["Custom LaserWriter Foo", "Other"]),
            "Custom LaserWriter Foo"
        )
        XCTAssertEqual(GuestPrinterProvisioner.chooseGuestPPD(names: []), "Generic")

        // Upsert resource fork preserves existing attributes, names, order, and adds/replaces targets.
        let initialFork = GuestPrinterProvisioner.buildResourceFork([
            GuestPrinterProvisioner.ResourceEntry(type: "STR ", id: 100, name: "KeepMe", attributes: 0, data: Data([1, 2, 3])),
            GuestPrinterProvisioner.ResourceEntry(type: "STR ", id: -8192, name: "Old", attributes: 0, data: Data([9])),
        ])
        let updatedFork = GuestPrinterProvisioner.upsertResourceFork(initialFork, updates: [
            GuestPrinterProvisioner.ResourceEntry(
                type: "STR ",
                id: -8192,
                name: "",
                attributes: GuestPrinterProvisioner.sysHeapRefAttrs,
                data: GuestPrinterProvisioner.pstr("LaserWriter 8")
            ),
            GuestPrinterProvisioner.ResourceEntry(type: "alis", id: -8192, name: "", attributes: 0, data: alias),
        ])
        let parsedEntries = GuestPrinterProvisioner.parseResourceFork(updatedFork)
        XCTAssertEqual(parsedEntries.count, 3)
        XCTAssertEqual(parsedEntries[0].type, "STR ")
        XCTAssertEqual(parsedEntries[0].id, 100)
        XCTAssertEqual(parsedEntries[0].name, "KeepMe")
        XCTAssertEqual(parsedEntries[0].data, Data([1, 2, 3]))
        XCTAssertEqual(parsedEntries[1].id, -8192)
        XCTAssertEqual(parsedEntries[1].attributes, GuestPrinterProvisioner.sysHeapRefAttrs)
        XCTAssertEqual(parsedEntries[1].data, GuestPrinterProvisioner.pstr("LaserWriter 8"))
        XCTAssertEqual(parsedEntries[2].type, "alis")
        XCTAssertEqual(parsedEntries[2].data, alias)
    }

    // MARK: - Socket Test Helpers

    private func connectLoopback(port: UInt16) throws -> Int32 {
        let fd = Darwin.socket(AF_INET, SOCK_STREAM, 0)
        guard fd >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
        var addr = sockaddr_in()
        addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = port.bigEndian
        addr.sin_addr = in_addr(s_addr: inet_addr("127.0.0.1"))
        let res = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                Darwin.connect(fd, sa, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard res == 0 else {
            Darwin.close(fd)
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .ECONNREFUSED)
        }
        return fd
    }

    private func sendAll(_ fd: Int32, _ data: Data) throws {
        try data.withUnsafeBytes { ptr in
            var sent = 0
            while sent < data.count {
                let n = Darwin.send(fd, ptr.baseAddress!.advanced(by: sent), data.count - sent, 0)
                if n <= 0 { throw POSIXError(.EIO) }
                sent += n
            }
        }
    }

    private func recvExact(_ fd: Int32, count: Int) throws -> Data {
        var buf = Data()
        while buf.count < count {
            var tmp = [UInt8](repeating: 0, count: count - buf.count)
            let n = tmp.withUnsafeMutableBytes { Darwin.recv(fd, $0.baseAddress, $0.count, 0) }
            if n <= 0 { throw POSIXError(.EIO) }
            buf.append(contentsOf: tmp[..<n])
        }
        return buf
    }

    private func recvUntilEOF(_ fd: Int32) throws -> Data {
        var buf = Data()
        var tmp = [UInt8](repeating: 0, count: 4096)
        while true {
            let n = tmp.withUnsafeMutableBytes { Darwin.recv(fd, $0.baseAddress, $0.count, 0) }
            if n <= 0 { break }
            buf.append(contentsOf: tmp[..<n])
        }
        return buf
    }

    private func buildIPPRequest(
        opID: UInt16,
        reqID: UInt32,
        attrs: [(UInt8, String, Data)],
        payload: Data = Data()
    ) -> Data {
        var out = Data([1, 1])
        var opBE = opID.bigEndian
        var reqBE = reqID.bigEndian
        out.append(Data(bytes: &opBE, count: 2))
        out.append(Data(bytes: &reqBE, count: 4))
        out.append(0x01) // operation-attributes-tag
        out.append(PrintServer.ippAttr(0x47, "attributes-charset", Data("utf-8".utf8)))
        out.append(PrintServer.ippAttr(0x48, "attributes-natural-language", Data("en".utf8)))
        for (tag, name, val) in attrs {
            out.append(PrintServer.ippAttr(tag, name, val))
        }
        out.append(0x03)
        out.append(payload)
        return out
    }

    private func postIPP(fd: Int32, path: String, ippBody: Data) throws -> Data {
        let hdr = "POST \(path) HTTP/1.1\r\nHost: 10.0.2.4\r\nContent-Type: application/ipp\r\nContent-Length: \(ippBody.count)\r\n\r\n"
        var req = Data(hdr.utf8)
        req.append(ippBody)
        try sendAll(fd, req)
        return try readHTTPResponse(fd).body
    }

    private func readHTTPStatusLineAndEmptyLine(_ fd: Int32) throws -> String {
        var buf = Data()
        let sep = Data("\r\n\r\n".utf8)
        while buf.range(of: sep) == nil {
            buf.append(try recvExact(fd, count: 1))
        }
        return String(data: buf, encoding: .isoLatin1) ?? ""
    }

    private func readHTTPResponse(_ fd: Int32) throws -> (header: String, body: Data) {
        var buf = Data()
        let sep = Data("\r\n\r\n".utf8)
        while true {
            let chunk = try recvExact(fd, count: 1)
            buf.append(chunk)
            if let range = buf.range(of: sep) {
                let header = String(data: buf[..<range.lowerBound], encoding: .isoLatin1) ?? ""
                var clen = 0
                for line in header.components(separatedBy: "\r\n") {
                    if line.lowercased().hasPrefix("content-length:") {
                        clen = Int(line.dropFirst("content-length:".count).trimmingCharacters(in: .whitespaces)) ?? 0
                    }
                }
                let body = clen > 0 ? try recvExact(fd, count: clen) : Data()
                return (header, body)
            }
        }
    }

    private func ippStatus(_ body: Data) -> UInt16 {
        guard body.count >= 4 else { return 0xFFFF }
        let b = [UInt8](body)
        return (UInt16(b[2]) << 8) | UInt16(b[3])
    }

    private func extractIPPJobID(_ body: Data) -> Int32? {
        let needle = Data("job-id".utf8)
        guard let range = body.range(of: needle) else { return nil }
        let after = range.upperBound
        guard after + 6 <= body.count else { return nil }
        let b = [UInt8](body[after..<(after + 6)])
        guard b[0] == 0x00, b[1] == 0x04 else { return nil }
        let raw = (UInt32(b[2]) << 24) | (UInt32(b[3]) << 16) | (UInt32(b[4]) << 8) | UInt32(b[5])
        return Int32(bitPattern: raw)
    }
}
