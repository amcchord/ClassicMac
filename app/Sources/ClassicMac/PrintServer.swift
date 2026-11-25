import Darwin
import Foundation

final class PrintServer: @unchecked Sendable {
    typealias JobHandler = @Sendable (_ data: Data, _ jobName: String, _ source: String, _ queue: String) throws -> PrintJobResult

    static let printerDisplayName = "ClassicMac Printer"
    static let maxHeaderBytes = 65_536

    private let targetPrinter: String?
    private let pdfOutputDir: URL
    private let jobHandler: JobHandler
    private let requestedLPDPort: UInt16
    private let requestedIPPPort: UInt16
    private let requestedRawPort: UInt16

    var onError: (@Sendable (Error) -> Void)?
    var onPrinted: (@Sendable (PrintJobResult) -> Void)?

    private(set) var boundLPDPort: UInt16 = 0
    private(set) var boundIPPPort: UInt16 = 0
    private(set) var boundRawPort: UInt16 = 0

    var ports: QEMUManager.PrinterForwardPorts {
        QEMUManager.PrinterForwardPorts(lpd: boundLPDPort, ipp: boundIPPPort, raw: boundRawPort)
    }

    private let stateLock = NSLock()
    private var running = false
    private var listenFDs: [Int32] = []
    private var activeClientFDs: Set<Int32> = []
    private var acceptQueues: [DispatchQueue] = []

    // IPP job table guarded by ippLock.
    private struct IPPJob {
        let id: Int32
        var jobName: String
        let queue: String
        var state: Int32
        var chunks: [Data]
    }
    private let ippLock = NSLock()
    private var ippJobs: [Int32: IPPJob] = [:]
    private var ippJobOrder: [Int32] = []
    private var nextIPPJobID: Int32 = 0
    private var lastIPPJobID: Int32?

    init(
        targetPrinter: String?,
        pdfOutputDir: URL,
        jobHandler: JobHandler? = nil,
        lpdPort: UInt16 = 0,
        ippPort: UInt16 = 0,
        rawPort: UInt16 = 0
    ) {
        self.targetPrinter = targetPrinter
        self.pdfOutputDir = pdfOutputDir
        self.jobHandler = jobHandler ?? { data, jobName, _, _ in
            try HostPrinterManager.submitJob(
                data: data,
                jobName: jobName,
                targetPrinter: targetPrinter,
                pdfOutputDir: pdfOutputDir
            )
        }
        self.requestedLPDPort = lpdPort
        self.requestedIPPPort = ippPort
        self.requestedRawPort = rawPort
    }

    deinit {
        stop()
    }

    func start() throws {
        stateLock.lock()
        if running {
            stateLock.unlock()
            return
        }
        stateLock.unlock()

        var openedFDs: [Int32] = []
        do {
            let (lpdFD, lpdPort) = try bindLoopback(port: requestedLPDPort)
            openedFDs.append(lpdFD)
            let (ippFD, ippPort) = try bindLoopback(port: requestedIPPPort)
            openedFDs.append(ippFD)
            let (rawFD, rawPort) = try bindLoopback(port: requestedRawPort)
            openedFDs.append(rawFD)

            stateLock.lock()
            running = true
            listenFDs = [lpdFD, ippFD, rawFD]
            boundLPDPort = lpdPort
            boundIPPPort = ippPort
            boundRawPort = rawPort
            let lpdQueue = DispatchQueue(label: "com.classicmac.print.lpd", qos: .userInitiated)
            let ippQueue = DispatchQueue(label: "com.classicmac.print.ipp", qos: .userInitiated)
            let rawQueue = DispatchQueue(label: "com.classicmac.print.raw", qos: .userInitiated)
            acceptQueues = [lpdQueue, ippQueue, rawQueue]
            stateLock.unlock()

            lpdQueue.async { [weak self] in self?.acceptLoop(listenFD: lpdFD, kind: .lpd) }
            ippQueue.async { [weak self] in self?.acceptLoop(listenFD: ippFD, kind: .ipp) }
            rawQueue.async { [weak self] in self?.acceptLoop(listenFD: rawFD, kind: .raw) }
        } catch {
            for fd in openedFDs {
                Darwin.close(fd)
            }
            throw error
        }
    }

    func stop() {
        stateLock.lock()
        running = false
        let fds = listenFDs
        let clients = Array(activeClientFDs)
        listenFDs = []
        activeClientFDs.removeAll()
        acceptQueues = []
        boundLPDPort = 0
        boundIPPPort = 0
        boundRawPort = 0
        stateLock.unlock()

        for fd in fds {
            Darwin.shutdown(fd, SHUT_RDWR)
            Darwin.close(fd)
        }
        for cfd in clients {
            Darwin.shutdown(cfd, SHUT_RDWR)
        }
    }

    // MARK: Socket plumbing

    private enum ProtocolKind { case lpd, ipp, raw }

    private func bindLoopback(port: UInt16) throws -> (Int32, UInt16) {
        let fd = Darwin.socket(AF_INET, SOCK_STREAM, 0)
        guard fd >= 0 else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }
        var closedOnExit = true
        defer { if closedOnExit { Darwin.close(fd) } }

        _ = fcntl(fd, F_SETFD, FD_CLOEXEC)
        var yes: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, socklen_t(MemoryLayout<Int32>.size))
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, socklen_t(MemoryLayout<Int32>.size))

        var addr = sockaddr_in()
        addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = port.bigEndian
        addr.sin_addr = in_addr(s_addr: inet_addr("127.0.0.1"))

        let bindRes = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                Darwin.bind(fd, sa, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bindRes == 0 else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EADDRINUSE)
        }
        guard Darwin.listen(fd, 16) == 0 else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }

        var bound = sockaddr_in()
        var len = socklen_t(MemoryLayout<sockaddr_in>.size)
        let nameRes = withUnsafeMutablePointer(to: &bound) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                Darwin.getsockname(fd, sa, &len)
            }
        }
        guard nameRes == 0 else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }
        closedOnExit = false
        return (fd, UInt16(bigEndian: bound.sin_port))
    }

    private func acceptLoop(listenFD: Int32, kind: ProtocolKind) {
        let clientQueue = DispatchQueue.global(qos: .userInitiated)
        while isServerRunning {
            var addr = sockaddr_in()
            var len = socklen_t(MemoryLayout<sockaddr_in>.size)
            let clientFD = withUnsafeMutablePointer(to: &addr) { ptr in
                ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                    Darwin.accept(listenFD, sa, &len)
                }
            }
            if clientFD < 0 {
                if errno == EINTR { continue }
                break
            }
            _ = fcntl(clientFD, F_SETFD, FD_CLOEXEC)
            var yes: Int32 = 1
            setsockopt(clientFD, SOL_SOCKET, SO_NOSIGPIPE, &yes, socklen_t(MemoryLayout<Int32>.size))

            stateLock.lock()
            let stillRunning = running
            if stillRunning {
                activeClientFDs.insert(clientFD)
            }
            stateLock.unlock()
            guard stillRunning else {
                Darwin.close(clientFD)
                break
            }

            clientQueue.async { [weak self] in
                guard let self else {
                    Darwin.close(clientFD)
                    return
                }
                defer {
                    self.stateLock.lock()
                    self.activeClientFDs.remove(clientFD)
                    self.stateLock.unlock()
                    Darwin.close(clientFD)
                }
                switch kind {
                case .lpd: self.handleLPD(fd: clientFD)
                case .ipp: self.handleIPP(fd: clientFD)
                case .raw: self.handleRaw(fd: clientFD)
                }
            }
        }
    }

    private var isServerRunning: Bool {
        stateLock.lock()
        defer { stateLock.unlock() }
        return running
    }

    // MARK: Job submission

    private func processJob(_ data: Data, jobName: String, source: String, queue: String) throws {
        let cleanData = HostPrinterManager.stripEOT(data)
        guard !cleanData.isEmpty, cleanData.count <= HostPrinterManager.maxJobBytes else {
            throw PrintError.invalidJob("The print job is empty or too large.")
        }
        let result = try jobHandler(cleanData, jobName, source, queue)
        onPrinted?(result)
    }

    private func reportError(_ error: Error) {
        onError?(error)
    }

    // MARK: Static parsers (shared with tests)

    static func extractPSTitle(_ data: Data) -> String {
        let prefix = data.prefix(4096)
        let text = String(data: prefix, encoding: .macOSRoman) ?? ""
        let lines = text.replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\r", with: "\n")
            .components(separatedBy: "\n")
        for line in lines where line.hasPrefix("%%Title:") {
            var raw = String(line.dropFirst("%%Title:".count))
            raw = raw.trimmingCharacters(in: CharacterSet(charactersIn: "() \t\r\n"))
            if !raw.isEmpty {
                return raw
            }
        }
        return "Mac OS Print"
    }

    static func parseLPDControl(_ data: Data) -> String {
        let text = String(data: data, encoding: .macOSRoman) ?? ""
        let lines = text.replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\r", with: "\n")
            .components(separatedBy: "\n")
        for line in lines {
            if (line.hasPrefix("J") || line.hasPrefix("N")) {
                let value = String(line.dropFirst()).trimmingCharacters(in: .whitespacesAndNewlines)
                if !value.isEmpty {
                    return value
                }
            }
        }
        return "Mac OS Print"
    }

    static func extractIPPQueue(_ path: String) -> String {
        let noQuery = path.split(separator: "?", maxSplits: 1).first.map(String.init) ?? path
        let stripped = noQuery.hasSuffix("/") && noQuery.count > 1 ? String(noQuery.dropLast()) : noQuery
        if stripped.hasPrefix("/printers/") {
            let tail = String(stripped.dropFirst("/printers/".count))
            let firstSegment = tail.split(separator: "/", maxSplits: 1).first.map(String.init) ?? ""
            let decoded = (firstSegment.removingPercentEncoding ?? firstSegment)
                .trimmingCharacters(in: .whitespacesAndNewlines)
            if !decoded.isEmpty {
                return decoded
            }
        }
        return "classicmac"
    }

    // MARK: LPD (RFC 1179)

    private func handleLPD(fd: Int32) {
        let reader = BufferedSocketReader(fd: fd)
        do {
            guard let firstLine = try reader.readLine(maxBytes: 8192, timeoutSeconds: 15), !firstLine.isEmpty else {
                return
            }
            let cmd = firstLine[firstLine.startIndex]
            if cmd == 1 || cmd == 3 || cmd == 4 {
                if cmd == 1 {
                    try writeAll(fd: fd, data: Data([0x00]))
                } else {
                    try writeAll(fd: fd, data: Data("no entries\n".utf8))
                }
                return
            }
            guard cmd == 2 else {
                try writeAll(fd: fd, data: Data([0x01]))
                return
            }
            let rawQueue = String(data: firstLine.dropFirst(), encoding: .isoLatin1)?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            let queue = rawQueue.isEmpty ? "classicmac" : rawQueue
            try writeAll(fd: fd, data: Data([0x00]))

            var jobName = "Mac OS Print"
            var payloads: [Data] = []

            while true {
                guard let rawSub = try reader.readLine(maxBytes: 8192, timeoutSeconds: 60) else {
                    break // EOF
                }
                // Classic Mac OS 9 Desktop PrintMonitor can emit leading NUL padding before subcommands.
                let sub = Data(rawSub.drop(while: { $0 == 0x00 }))
                if sub.isEmpty || sub == Data([0x0A]) {
                    continue
                }
                let stype = sub[sub.startIndex]
                if stype == 1 { // abort
                    try writeAll(fd: fd, data: Data([0x00]))
                    return
                }
                guard stype == 2 || stype == 3 else {
                    try writeAll(fd: fd, data: Data([0x01]))
                    return
                }
                let restText = (String(data: sub.dropFirst(), encoding: .isoLatin1) ?? "")
                    .trimmingCharacters(in: .whitespacesAndNewlines)
                let parts = restText.split(maxSplits: 1, whereSeparator: { $0.isWhitespace })
                guard let countToken = parts.first, let count = Int(countToken),
                      count >= 0, count <= HostPrinterManager.maxJobBytes else {
                    try writeAll(fd: fd, data: Data([0x01]))
                    return
                }
                try writeAll(fd: fd, data: Data([0x00]))

                let payloadData: Data
                if count == 0 {
                    var chunks = Data()
                    while true {
                        let chunk = try reader.readUpTo(65_536, timeoutSeconds: 30)
                        if chunk.isEmpty { break }
                        if chunks.count + chunk.count > HostPrinterManager.maxJobBytes {
                            throw PrintError.invalidJob("LPD print job exceeded size limit.")
                        }
                        chunks.append(chunk)
                    }
                    payloadData = chunks
                } else {
                    var buf = Data()
                    buf.reserveCapacity(count)
                    while buf.count < count {
                        let need = count - buf.count
                        let maxRead = buf.isEmpty ? min(65_536, max(need, sub.count)) : min(65_536, need)
                        let ch = try reader.readUpTo(maxRead, timeoutSeconds: 120)
                        if ch.isEmpty {
                            throw SocketIOError.eof
                        }
                        // Classic Mac OS 9 Desktop PrintMonitor can re-send the subcommand header line
                        // right after receiving 0x00 and wait for a second 0x00 ACK before sending data.
                        if buf.isEmpty && ch.starts(with: sub) {
                            try writeAll(fd: fd, data: Data([0x00]))
                            let afterHeader = Data(ch.dropFirst(sub.count))
                            if afterHeader.count > need {
                                buf.append(afterHeader.prefix(need))
                                reader.unread(Data(afterHeader.dropFirst(need)))
                            } else if !afterHeader.isEmpty {
                                buf.append(afterHeader)
                            }
                            continue
                        }
                        if ch.count > need {
                            buf.append(ch.prefix(need))
                            reader.unread(Data(ch.dropFirst(need)))
                        } else {
                            buf.append(ch)
                        }
                    }
                    payloadData = buf
                    let term = try reader.readUpTo(1, timeoutSeconds: 15)
                    if !term.isEmpty && term[term.startIndex] != 0x00 {
                        try writeAll(fd: fd, data: Data([0x01]))
                        return
                    }
                }

                try writeAll(fd: fd, data: Data([0x00]))
                if stype == 2 {
                    jobName = Self.parseLPDControl(payloadData)
                } else {
                    payloads.append(payloadData)
                }
            }

            if !payloads.isEmpty {
                var combined = Data()
                for part in payloads { combined.append(part) }
                do {
                    try processJob(combined, jobName: jobName, source: "lpd", queue: queue)
                } catch {
                    reportError(error)
                }
            }
        } catch {
            // Connection error/timeout; close cleanly.
        }
    }

    // MARK: AppSocket / JetDirect (TCP 9100)

    private func handleRaw(fd: Int32) {
        let reader = BufferedSocketReader(fd: fd)
        do {
            var buf = Data()
            while true {
                let chunk = try reader.readUpTo(65_536, timeoutSeconds: 60)
                if chunk.isEmpty { break }
                if buf.count + chunk.count > HostPrinterManager.maxJobBytes {
                    reportError(PrintError.invalidJob("Raw 9100 print job exceeded size limit."))
                    return
                }
                buf.append(chunk)
            }
            if !buf.isEmpty {
                let title = Self.extractPSTitle(HostPrinterManager.stripEOT(buf))
                do {
                    try processJob(buf, jobName: title, source: "raw", queue: "classicmac")
                } catch {
                    reportError(error)
                }
            }
        } catch {
            // Connection error/timeout.
        }
    }

    // MARK: IPP (RFC 8010/8011 over HTTP/1.1)

    private func handleIPP(fd: Int32) {
        let reader = BufferedSocketReader(fd: fd)
        do {
            while true {
                guard let headerData = try reader.readUntil(
                    delimiter: Data("\r\n\r\n".utf8),
                    maxBytes: Self.maxHeaderBytes,
                    timeoutSeconds: 30
                ) else {
                    return
                }
                let headerText = String(data: headerData, encoding: .isoLatin1) ?? ""
                let lines = headerText.components(separatedBy: "\r\n")
                guard let requestLine = lines.first else { return }
                let reqParts = requestLine.split(whereSeparator: { $0.isWhitespace })
                let method = reqParts.first.map { String($0).uppercased() } ?? "GET"
                let reqPath = reqParts.count > 1 ? String(reqParts[1]) : "/"
                let ippQueue = Self.extractIPPQueue(reqPath)

                var headers: [String: String] = [:]
                for hl in lines.dropFirst() {
                    if let colon = hl.firstIndex(of: ":") {
                        let k = hl[..<colon].trimmingCharacters(in: .whitespaces).lowercased()
                        let v = hl[hl.index(after: colon)...].trimmingCharacters(in: .whitespaces)
                        headers[k] = v
                    }
                }

                if headers["expect"]?.lowercased() == "100-continue" {
                    try writeAll(fd: fd, data: Data("HTTP/1.1 100 Continue\r\n\r\n".utf8))
                }

                let body: Data
                if headers["transfer-encoding"]?.lowercased().contains("chunked") == true {
                    body = try readChunkedBody(reader: reader)
                } else {
                    let clen = Int(headers["content-length"] ?? "0") ?? -1
                    guard clen >= 0, clen <= HostPrinterManager.maxJobBytes else { return }
                    body = clen > 0 ? try reader.readExact(clen, timeoutSeconds: 120) : Data()
                }

                if method == "POST" {
                    let respIPP = dispatchIPP(body, queue: ippQueue)
                    try writeHTTP(fd: fd, status: 200, contentType: "application/ipp", body: respIPP)
                } else {
                    try writeHTTP(
                        fd: fd,
                        status: 200,
                        contentType: "text/plain; charset=utf-8",
                        body: Data("\(Self.printerDisplayName)\n".utf8)
                    )
                }

                if headers["connection"]?.lowercased() == "close" || requestLine.contains("HTTP/1.0") {
                    return
                }
            }
        } catch {
            // Connection closed / timeout / invalid HTTP.
        }
    }

    private func readChunkedBody(reader: BufferedSocketReader) throws -> Data {
        var out = Data()
        while true {
            guard let sizeLineData = try reader.readUntil(
                delimiter: Data("\r\n".utf8),
                maxBytes: 4096,
                timeoutSeconds: 30
            ) else {
                return out
            }
            let sizeStr = (String(data: sizeLineData, encoding: .isoLatin1) ?? "")
                .split(separator: ";", maxSplits: 1)
                .first?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            guard let size = Int(sizeStr, radix: 16), size >= 0 else {
                throw SocketIOError.invalidData
            }
            if size == 0 {
                // Consume trailing headers up to empty line if present.
                while let t = (try? reader.readUntil(delimiter: Data("\r\n".utf8), maxBytes: 4096, timeoutSeconds: 5)) ?? nil,
                      !t.isEmpty {
                    continue
                }
                return out
            }
            guard out.count + size <= HostPrinterManager.maxJobBytes else {
                throw SocketIOError.invalidData
            }
            let chunk = try reader.readExact(size, timeoutSeconds: 60)
            out.append(chunk)
            _ = try reader.readExact(2, timeoutSeconds: 15) // CRLF
        }
    }

    private func writeHTTP(fd: Int32, status: Int, contentType: String, body: Data) throws {
        let header = "HTTP/1.1 \(status) OK\r\nContent-Type: \(contentType)\r\nContent-Length: \(body.count)\r\nConnection: keep-alive\r\n\r\n"
        var payload = Data(header.utf8)
        payload.append(body)
        try writeAll(fd: fd, data: payload)
    }

    // MARK: IPP binary codec & dispatch

    struct ParsedIPPRequest {
        let operationID: UInt16
        let requestID: UInt32
        let jobName: String
        let jobID: Int32?
        let lastDocument: Bool
        let payload: Data
    }

    static func parseIPPRequest(_ body: Data) -> ParsedIPPRequest? {
        guard body.count >= 8 else { return nil }
        let b = [UInt8](body)
        let opID = (UInt16(b[2]) << 8) | UInt16(b[3])
        let reqID = (UInt32(b[4]) << 24) | (UInt32(b[5]) << 16) | (UInt32(b[6]) << 8) | UInt32(b[7])
        var pos = 8
        let n = b.count

        var jobName = "Mac OS Print"
        var jobID: Int32?
        var lastDocument = true

        while pos < n {
            let tag = b[pos]
            pos += 1
            if tag == 0x03 { // end-of-attributes-tag
                break
            }
            if tag <= 0x0F { // delimiter / group tag
                continue
            }
            guard pos + 2 <= n else { break }
            let klen = Int((UInt16(b[pos]) << 8) | UInt16(b[pos + 1]))
            pos += 2
            guard pos + klen + 2 <= n else { break }
            let key = String(bytes: b[pos..<(pos + klen)], encoding: .utf8) ?? ""
            pos += klen
            let vlen = Int((UInt16(b[pos]) << 8) | UInt16(b[pos + 1]))
            pos += 2
            guard pos + vlen <= n else { break }
            let val = Array(b[pos..<(pos + vlen)])
            pos += vlen

            if key == "job-name", !val.isEmpty {
                jobName = String(bytes: val, encoding: .utf8) ?? "Mac OS Print"
            } else if key == "job-id", val.count == 4, (tag == 0x21 || tag == 0x23) {
                let raw = (UInt32(val[0]) << 24) | (UInt32(val[1]) << 16) | (UInt32(val[2]) << 8) | UInt32(val[3])
                jobID = Int32(bitPattern: raw)
            } else if key == "job-uri", !val.isEmpty, jobID == nil {
                let uri = (String(bytes: val, encoding: .utf8) ?? "").trimmingCharacters(in: CharacterSet(charactersIn: "/"))
                if let range = uri.range(of: #"/jobs/(\d+)$"#, options: .regularExpression) {
                    let tail = uri[range].dropFirst("/jobs/".count)
                    if let parsed = Int32(tail) {
                        jobID = parsed
                    }
                }
            } else if key == "last-document", val.count == 1, tag == 0x22 {
                lastDocument = val[0] != 0
            }
        }

        let payload = pos < n ? Data(b[pos..<n]) : Data()
        return ParsedIPPRequest(
            operationID: opID,
            requestID: reqID,
            jobName: jobName,
            jobID: jobID,
            lastDocument: lastDocument,
            payload: payload
        )
    }

    static func ippAttr(_ tag: UInt8, _ name: String, _ value: Data) -> Data {
        let nameBytes = Data(name.utf8)
        var out = Data([tag])
        var nlen = UInt16(nameBytes.count).bigEndian
        var vlen = UInt16(value.count).bigEndian
        out.append(Data(bytes: &nlen, count: 2))
        out.append(nameBytes)
        out.append(Data(bytes: &vlen, count: 2))
        out.append(value)
        return out
    }

    static func ippInt32Data(_ v: Int32) -> Data {
        var be = v.bigEndian
        return Data(bytes: &be, count: 4)
    }

    static func buildIPPResponse(status: UInt16, requestID: UInt32, extra: Data = Data()) -> Data {
        var out = Data([1, 1])
        var stBE = status.bigEndian
        var reqBE = requestID.bigEndian
        out.append(Data(bytes: &stBE, count: 2))
        out.append(Data(bytes: &reqBE, count: 4))
        out.append(0x01) // operation-attributes-tag
        out.append(ippAttr(0x47, "attributes-charset", Data("utf-8".utf8)))
        out.append(ippAttr(0x48, "attributes-natural-language", Data("en".utf8)))
        out.append(extra)
        out.append(0x03) // end-of-attributes-tag
        return out
    }

    private func newIPPJob(jobName: String, queue: String, state: Int32) -> IPPJob {
        ippLock.lock()
        defer { ippLock.unlock() }
        nextIPPJobID += 1
        let jid = nextIPPJobID
        lastIPPJobID = jid
        if ippJobOrder.count >= 50 {
            let oldest = ippJobOrder.removeFirst()
            ippJobs.removeValue(forKey: oldest)
        }
        let job = IPPJob(id: jid, jobName: jobName, queue: queue, state: state, chunks: [])
        ippJobs[jid] = job
        ippJobOrder.append(jid)
        return job
    }

    private func ippJobAttrs(_ job: IPPJob) -> Data {
        let port = boundIPPPort != 0 ? boundIPPPort : 631
        let juri = "ipp://10.0.2.4:\(port)/jobs/\(job.id)"
        var out = Data([0x02]) // job-attributes-tag
        out.append(Self.ippAttr(0x21, "job-id", Self.ippInt32Data(job.id)))
        out.append(Self.ippAttr(0x45, "job-uri", Data(juri.utf8)))
        out.append(Self.ippAttr(0x23, "job-state", Self.ippInt32Data(job.state)))
        out.append(Self.ippAttr(0x42, "job-name", Data(job.jobName.utf8)))
        return out
    }

    private func dispatchIPP(_ body: Data, queue: String) -> Data {
        guard let req = Self.parseIPPRequest(body) else {
            return Self.buildIPPResponse(status: 0x0400, requestID: 1)
        }
        let opID = req.operationID
        let reqID = req.requestID
        let jobName = req.jobName
        let port = boundIPPPort != 0 ? boundIPPPort : 631
        let qLabel = (!queue.isEmpty && queue != "classicmac") ? queue : Self.printerDisplayName
        let printerURI = "ipp://10.0.2.4:\(port)/printers/\(queue.isEmpty ? "classicmac" : queue)"

        if opID == 0x000B { // Get-Printer-Attributes
            let ops: [Int32] = [2, 4, 5, 6, 8, 9, 10, 11]
            var opsBytes = Self.ippAttr(0x23, "operations-supported", Self.ippInt32Data(ops[0]))
            for op in ops.dropFirst() {
                opsBytes.append(Self.ippAttr(0x23, "", Self.ippInt32Data(op)))
            }
            var extra = Data([0x04]) // printer-attributes-tag
            extra.append(Self.ippAttr(0x45, "printer-uri-supported", Data(printerURI.utf8)))
            extra.append(Self.ippAttr(0x44, "uri-security-supported", Data("none".utf8)))
            extra.append(Self.ippAttr(0x44, "uri-authentication-supported", Data("none".utf8)))
            extra.append(Self.ippAttr(0x42, "printer-name", Data(qLabel.utf8)))
            extra.append(Self.ippAttr(0x23, "printer-state", Self.ippInt32Data(3)))
            extra.append(Self.ippAttr(0x44, "printer-state-reasons", Data("none".utf8)))
            extra.append(Self.ippAttr(0x22, "printer-is-accepting-jobs", Data([0x01])))
            extra.append(opsBytes)
            extra.append(Self.ippAttr(0x44, "pdl-override-supported", Data("not-attempted".utf8)))
            extra.append(Self.ippAttr(0x22, "color-supported", Data([0x01])))
            extra.append(Self.ippAttr(0x49, "document-format-default", Data("application/postscript".utf8)))
            extra.append(Self.ippAttr(0x49, "document-format-supported", Data("application/postscript".utf8)))
            extra.append(Self.ippAttr(0x49, "", Data("application/pdf".utf8)))
            extra.append(Self.ippAttr(0x47, "charset-configured", Data("utf-8".utf8)))
            extra.append(Self.ippAttr(0x47, "charset-supported", Data("utf-8".utf8)))
            extra.append(Self.ippAttr(0x48, "natural-language-configured", Data("en".utf8)))
            extra.append(Self.ippAttr(0x48, "generated-natural-language-supported", Data("en".utf8)))
            return Self.buildIPPResponse(status: 0x0000, requestID: reqID, extra: extra)
        }

        if opID == 0x0004 { // Validate-Job
            return Self.buildIPPResponse(status: 0x0000, requestID: reqID)
        }

        if opID == 0x0005 { // Create-Job
            let job = newIPPJob(jobName: jobName, queue: queue, state: 4)
            return Self.buildIPPResponse(status: 0x0000, requestID: reqID, extra: ippJobAttrs(job))
        }

        if opID == 0x0006 { // Send-Document
            enum Action {
                case missing
                case canceled
                case moreChunks(IPPJob)
                case finish(Data, String, String, Int32)
            }
            let action: Action = ippLock.withLock {
                guard let jid = req.jobID ?? lastIPPJobID, var job = ippJobs[jid] else {
                    return .missing
                }
                if job.state == 7 {
                    return .canceled
                }
                if !req.payload.isEmpty {
                    job.chunks.append(req.payload)
                }
                if jobName != "Mac OS Print", job.jobName == "Mac OS Print" {
                    job.jobName = jobName
                }
                if !req.lastDocument {
                    job.state = 3
                    ippJobs[jid] = job
                    return .moreChunks(job)
                }
                var full = Data()
                for c in job.chunks { full.append(c) }
                job.chunks.removeAll()
                ippJobs[jid] = job
                return .finish(full, job.jobName, job.queue, jid)
            }

            switch action {
            case .missing:
                return Self.buildIPPResponse(status: 0x0406, requestID: reqID)
            case .canceled:
                return Self.buildIPPResponse(status: 0x0407, requestID: reqID)
            case .moreChunks(let job):
                return Self.buildIPPResponse(status: 0x0000, requestID: reqID, extra: ippJobAttrs(job))
            case .finish(let fullData, let resolvedName, let resolvedQueue, let jid):
                do {
                    try processJob(fullData, jobName: resolvedName, source: "ipp", queue: resolvedQueue)
                    let updated: IPPJob? = ippLock.withLock {
                        ippJobs[jid]?.state = 9
                        return ippJobs[jid]
                    }
                    return Self.buildIPPResponse(
                        status: 0x0000,
                        requestID: reqID,
                        extra: updated.map { ippJobAttrs($0) } ?? Data()
                    )
                } catch {
                    ippLock.withLock { ippJobs[jid]?.state = 8 }
                    reportError(error)
                    return Self.buildIPPResponse(status: 0x0500, requestID: reqID)
                }
            }
        }

        if opID == 0x0008 { // Cancel-Job
            let found: Bool = ippLock.withLock {
                guard let jid = req.jobID, var job = ippJobs[jid] else { return false }
                job.state = 7
                job.chunks.removeAll()
                ippJobs[jid] = job
                return true
            }
            return Self.buildIPPResponse(status: found ? 0x0000 : 0x0406, requestID: reqID)
        }

        if opID == 0x0009 { // Get-Job-Attributes
            let job: IPPJob? = ippLock.withLock {
                guard let jid = req.jobID else { return nil }
                return ippJobs[jid]
            }
            guard let job else {
                return Self.buildIPPResponse(status: 0x0406, requestID: reqID)
            }
            return Self.buildIPPResponse(status: 0x0000, requestID: reqID, extra: ippJobAttrs(job))
        }

        if opID == 0x000A { // Get-Jobs
            let recent: [IPPJob] = ippLock.withLock {
                ippJobOrder.suffix(10).compactMap { ippJobs[$0] }
            }
            var extra = Data()
            for job in recent {
                extra.append(ippJobAttrs(job))
            }
            return Self.buildIPPResponse(status: 0x0000, requestID: reqID, extra: extra)
        }

        if opID == 0x0002 { // Print-Job
            let job = newIPPJob(jobName: jobName, queue: queue, state: 5)
            do {
                try processJob(req.payload, jobName: jobName, source: "ipp", queue: queue)
                let updated: IPPJob = ippLock.withLock {
                    ippJobs[job.id]?.state = 9
                    return ippJobs[job.id] ?? job
                }
                return Self.buildIPPResponse(status: 0x0000, requestID: reqID, extra: ippJobAttrs(updated))
            } catch {
                ippLock.withLock { ippJobs[job.id]?.state = 8 }
                reportError(error)
                return Self.buildIPPResponse(status: 0x0500, requestID: reqID)
            }
        }

        return Self.buildIPPResponse(status: 0x0501, requestID: reqID)
    }

    // MARK: Socket I/O helpers

    private enum SocketIOError: Error {
        case timeout
        case eof
        case invalidData
    }

    private func writeAll(fd: Int32, data: Data) throws {
        guard !data.isEmpty else { return }
        try data.withUnsafeBytes { rawBuf in
            guard let base = rawBuf.baseAddress else { return }
            var sent = 0
            while sent < data.count {
                let n = Darwin.send(fd, base.advanced(by: sent), data.count - sent, 0)
                if n > 0 {
                    sent += n
                    continue
                }
                if n < 0 && errno == EINTR {
                    continue
                }
                if n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) {
                    var pfd = pollfd(fd: fd, events: Int16(POLLOUT), revents: 0)
                    let pr = Darwin.poll(&pfd, 1, 10_000)
                    if pr > 0 { continue }
                }
                throw SocketIOError.eof
            }
        }
    }

    private final class BufferedSocketReader {
        private let fd: Int32
        private var buffer = Data()

        init(fd: Int32) {
            self.fd = fd
        }

        func unread(_ data: Data) {
            guard !data.isEmpty else { return }
            buffer = data + buffer
        }

        func readLine(maxBytes: Int, timeoutSeconds: TimeInterval) throws -> Data? {
            while true {
                if let nl = buffer.firstIndex(of: 0x0A) {
                    let end = buffer.index(after: nl)
                    let line = Data(buffer[..<end])
                    buffer.removeSubrange(..<end)
                    return line
                }
                if buffer.count >= maxBytes {
                    throw SocketIOError.invalidData
                }
                let got = try fillBuffer(timeoutSeconds: timeoutSeconds)
                if got == 0 {
                    if buffer.isEmpty { return nil }
                    let rest = buffer
                    buffer.removeAll()
                    return rest
                }
            }
        }

        func readUntil(delimiter: Data, maxBytes: Int, timeoutSeconds: TimeInterval) throws -> Data? {
            while true {
                if let range = buffer.range(of: delimiter) {
                    let result = Data(buffer[..<range.lowerBound])
                    buffer.removeSubrange(..<range.upperBound)
                    return result
                }
                if buffer.count >= maxBytes {
                    throw SocketIOError.invalidData
                }
                let got = try fillBuffer(timeoutSeconds: timeoutSeconds)
                if got == 0 {
                    if buffer.isEmpty { return nil }
                    throw SocketIOError.eof
                }
            }
        }

        func readExact(_ count: Int, timeoutSeconds: TimeInterval) throws -> Data {
            while buffer.count < count {
                let got = try fillBuffer(timeoutSeconds: timeoutSeconds)
                if got == 0 {
                    throw SocketIOError.eof
                }
            }
            let out = Data(buffer.prefix(count))
            buffer.removeFirst(count)
            return out
        }

        func readUpTo(_ maxBytes: Int, timeoutSeconds: TimeInterval) throws -> Data {
            if !buffer.isEmpty {
                let take = min(maxBytes, buffer.count)
                let out = Data(buffer.prefix(take))
                buffer.removeFirst(take)
                return out
            }
            let got = try fillBuffer(timeoutSeconds: timeoutSeconds, maxRead: maxBytes)
            if got == 0 { return Data() }
            let take = min(maxBytes, buffer.count)
            let out = Data(buffer.prefix(take))
            buffer.removeFirst(take)
            return out
        }

        private func fillBuffer(timeoutSeconds: TimeInterval, maxRead: Int = 65_536) throws -> Int {
            let timeoutMs = Int32(max(1, Int(timeoutSeconds * 1000)))
            while true {
                var pfd = pollfd(fd: fd, events: Int16(POLLIN), revents: 0)
                let pr = Darwin.poll(&pfd, 1, timeoutMs)
                if pr < 0 {
                    if errno == EINTR { continue }
                    throw SocketIOError.eof
                }
                if pr == 0 {
                    throw SocketIOError.timeout
                }
                var tmp = [UInt8](repeating: 0, count: min(maxRead, 65_536))
                let n = tmp.withUnsafeMutableBytes { ptr in
                    Darwin.recv(fd, ptr.baseAddress, ptr.count, 0)
                }
                if n < 0 {
                    if errno == EINTR { continue }
                    throw SocketIOError.eof
                }
                if n == 0 {
                    return 0
                }
                buffer.append(contentsOf: tmp[..<n])
                return n
            }
        }
    }
}

private extension NSLock {
    func withLock<T>(_ body: () throws -> T) rethrows -> T {
        lock()
        defer { unlock() }
        return try body()
    }
}
