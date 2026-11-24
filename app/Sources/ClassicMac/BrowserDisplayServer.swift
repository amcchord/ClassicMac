import Foundation
import CryptoKit
import Network
import Darwin

struct BrowserDisplayEndpoint: Equatable {
    let vmID: UUID

    static func vncSocketURL(for vmID: UUID) -> URL {
        AppPaths.runtimeSocketDir.appendingPathComponent("\(vmID.uuidString).vnc.sock")
    }

    var vncSocketURL: URL { Self.vncSocketURL(for: vmID) }

    var qemuDisplay: String {
        "vnc=unix:\(QEMUManager.qemuEscape(vncSocketURL.path)),share=force-shared"
    }
}

enum BrowserDisplayError: LocalizedError {
    case assetsMissing
    case portAllocationFailed(String)
    case serverFailed(String)
    case serverTimedOut

    var errorDescription: String? {
        switch self {
        case .assetsMissing:
            return "The browser display files are missing from ClassicMac."
        case .portAllocationFailed(let detail):
            return "ClassicMac could not reserve a local browser port. \(detail)"
        case .serverFailed(let detail):
            return "ClassicMac could not start its local browser server. \(detail)"
        case .serverTimedOut:
            return "ClassicMac's local browser server did not start in time."
        }
    }
}

private final class ListenerStartBox: @unchecked Sendable {
    private let lock = NSLock()
    private var result: Result<UInt16, Error>?

    func resolve(_ value: Result<UInt16, Error>) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        guard result == nil else { return false }
        result = value
        return true
    }

    func value() -> Result<UInt16, Error>? {
        lock.lock()
        defer { lock.unlock() }
        return result
    }
}

// A tiny, loopback-only HTTP server and authenticated WebSocket-to-Unix-socket
// proxy for the browser display. Publishes the bundled HTML, CSS, and noVNC
// modules and proxies `/websockify?token=<actionToken>` to QEMU's private
// Unix VNC socket after validating both Origin and the per-run token.
final class BrowserDisplayServer: @unchecked Sendable {
    let endpoint: BrowserDisplayEndpoint
    private(set) var url: URL

    private let listener: NWListener
    private let queue: DispatchQueue
    private let assetRoot: URL
    private let machineName: String
    private let classicInputHelpers: Bool
    private let indexTemplate: String
    fileprivate let idleReadTimeout: TimeInterval
    private let actionToken = UUID().uuidString

    private var listeningPort: UInt16 = 0
    private var configurationData = Data()
    private var indexData = Data()

    private let proxiesLock = NSLock()
    private var activeProxies: [ObjectIdentifier: WebSocketProxy] = [:]

    static func start(
        for config: VMConfig,
        idleReadTimeout: TimeInterval = 10
    ) throws -> BrowserDisplayServer {
        let endpoint = BrowserDisplayEndpoint(vmID: config.id)
        return try BrowserDisplayServer(
            endpoint: endpoint,
            machineName: config.name,
            classicInputHelpers: config.classicInputHelpers,
            idleReadTimeout: idleReadTimeout
        )
    }

    private init(
        endpoint: BrowserDisplayEndpoint,
        machineName: String,
        classicInputHelpers: Bool,
        idleReadTimeout: TimeInterval
    ) throws {
        let root = AppPaths.browserAssetsDir
        let templateURL = root.appendingPathComponent("index.html")
        guard FileManager.default.fileExists(atPath: templateURL.path),
              let template = try? String(contentsOf: templateURL, encoding: .utf8) else {
            throw BrowserDisplayError.assetsMissing
        }

        self.endpoint = endpoint
        self.assetRoot = root.standardizedFileURL
        self.machineName = machineName
        self.classicInputHelpers = classicInputHelpers
        self.indexTemplate = template
        self.idleReadTimeout = idleReadTimeout
        self.queue = DispatchQueue(
            label: "com.classicmac.browser-display.\(endpoint.vmID.uuidString)"
        )

        let parameters = NWParameters.tcp
        parameters.requiredLocalEndpoint = .hostPort(
            host: "127.0.0.1",
            port: .any
        )
        self.listener = try NWListener(using: parameters)
        self.url = URL(string: "http://127.0.0.1/")!

        let started = ListenerStartBox()
        let semaphore = DispatchSemaphore(value: 0)
        listener.stateUpdateHandler = { [weak self, weak listener] state in
            switch state {
            case .ready:
                guard let port = listener?.port?.rawValue,
                      let self else {
                    if started.resolve(.failure(
                        BrowserDisplayError.serverFailed("No listening port was assigned.")
                    )) {
                        semaphore.signal()
                    }
                    return
                }
                do {
                    try self.configure(port: port)
                    if started.resolve(.success(port)) {
                        semaphore.signal()
                    }
                } catch {
                    if started.resolve(.failure(error)) {
                        semaphore.signal()
                    }
                }
            case .failed(let error):
                if started.resolve(.failure(
                    BrowserDisplayError.serverFailed(error.localizedDescription)
                )) {
                    semaphore.signal()
                }
            default:
                break
            }
        }

        listener.newConnectionHandler = { [weak self] connection in
            self?.accept(connection)
        }
        listener.start(queue: queue)

        guard semaphore.wait(timeout: .now() + 3) == .success else {
            listener.cancel()
            throw BrowserDisplayError.serverTimedOut
        }

        switch started.value() {
        case .success:
            break
        case .failure(let error):
            listener.cancel()
            throw error
        case .none:
            listener.cancel()
            throw BrowserDisplayError.serverTimedOut
        }
    }

    private func configure(port: UInt16) throws {
        guard let publicURL = URL(string: "http://127.0.0.1:\(port)/") else {
            throw BrowserDisplayError.serverFailed("The local URL was invalid.")
        }
        let wsURL = "ws://127.0.0.1:\(port)/websockify?token=\(actionToken)"
        self.configurationData = try JSONSerialization.data(
            withJSONObject: [
                "machineName": machineName,
                "webSocketURL": wsURL,
                "classicInputHelpers": classicInputHelpers
            ],
            options: []
        )
        let renderedIndex = indexTemplate
            .replacingOccurrences(
                of: "__CLASSICMAC_MACHINE_NAME__",
                with: Self.htmlAttributeEscape(machineName)
            )
            .replacingOccurrences(
                of: "__CLASSICMAC_WEBSOCKET_URL__",
                with: wsURL
            )
            .replacingOccurrences(
                of: "__CLASSICMAC_INPUT_HELPERS__",
                with: classicInputHelpers ? "true" : "false"
            )
            .replacingOccurrences(of: "__CLASSICMAC_ACTION_TOKEN__", with: actionToken)
        self.indexData = Data(renderedIndex.utf8)
        self.listeningPort = port
        self.url = publicURL
    }

    func stop() {
        listener.cancel()
        proxiesLock.lock()
        let proxies = Array(activeProxies.values)
        activeProxies.removeAll()
        proxiesLock.unlock()
        for proxy in proxies {
            proxy.stop()
        }
    }

    private func accept(_ connection: NWConnection) {
        guard listeningPort > 0 else {
            connection.cancel()
            return
        }
        BrowserHTTPRequest(connection: connection, server: self).start(on: queue)
    }

    fileprivate func registerProxy(_ proxy: WebSocketProxy) {
        proxiesLock.lock()
        activeProxies[ObjectIdentifier(proxy)] = proxy
        proxiesLock.unlock()
    }

    fileprivate func unregisterProxy(_ proxy: WebSocketProxy) {
        proxiesLock.lock()
        activeProxies.removeValue(forKey: ObjectIdentifier(proxy))
        proxiesLock.unlock()
    }

    private static func constantTimeEquals(_ lhs: String, _ rhs: String) -> Bool {
        let a = Array(lhs.utf8)
        let b = Array(rhs.utf8)
        guard a.count == b.count else { return false }
        var diff: UInt8 = 0
        for i in 0..<a.count {
            diff |= a[i] ^ b[i]
        }
        return diff == 0
    }

    private var expectedOrigin: String? {
        guard listeningPort > 0 else { return nil }
        return "http://127.0.0.1:\(listeningPort)"
    }

    /// This capability opens a local review window only. It never accepts text
    /// or sends guest input. Require both the per-run token and exact origin;
    /// a web page elsewhere must not be able to inspect the host clipboard.
    fileprivate func pasteTextResponse(headers: [String: String], openWindow: Bool) async -> BrowserHTTPResponse {
        guard let origin = expectedOrigin,
              headers["origin"] == origin,
              let providedToken = headers["x-classicmac-action"],
              Self.constantTimeEquals(providedToken, actionToken) else {
            return textResponse(status: 403, reason: "Forbidden", text: "Reopen the display from ClassicMac.")
        }
        let opened = await MainActor.run {
            guard QEMUManager.shared.isRunning(endpoint.vmID),
                  !QEMUManager.shared.isPaused(endpoint.vmID) else { return false }
            if openWindow { PasteTextController.shared.present(for: endpoint.vmID) }
            return true
        }
        if !openWindow {
            return BrowserHTTPResponse(status: 200, reason: "OK", contentType: "application/json",
                                       body: Data("{\"canPaste\":\(opened)}".utf8),
                                       contentSecurityPolicy: contentSecurityPolicy)
        }
        return opened
            ? textResponse(status: 202, reason: "Accepted", text: "Review the text in ClassicMac, then choose Paste Text.")
            : textResponse(status: 409, reason: "Conflict", text: "Resume or start the Mac in ClassicMac before pasting.")
    }

    // This endpoint only opens the app's drawer. File selection and media
    // changes remain local, and source paths are never returned to the page.
    fileprivate func mediaResponse(headers: [String: String]) async -> BrowserHTTPResponse {
        guard let origin = expectedOrigin,
              headers["origin"] == origin,
              let providedToken = headers["x-classicmac-action"],
              Self.constantTimeEquals(providedToken, actionToken) else {
            return textResponse(status: 403, reason: "Forbidden", text: "Reopen the display from ClassicMac.")
        }
        let opened = await MainActor.run {
            guard VMStore.shared.vms.contains(where: { $0.id == endpoint.vmID }) else { return false }
            MediaController.shared.present(for: endpoint.vmID)
            return true
        }
        return opened
            ? textResponse(status: 202, reason: "Accepted", text: "Manage this Mac's media in ClassicMac.")
            : textResponse(status: 409, reason: "Conflict", text: "Open this machine in ClassicMac again.")
    }

    fileprivate func upgradeWebSocket(
        connection: NWConnection,
        method: String,
        components: URLComponents,
        headers: [String: String],
        trailingData: Data
    ) -> BrowserHTTPResponse? {
        guard method == "GET" else {
            return textResponse(status: 405, reason: "Method Not Allowed", text: "Method not allowed")
        }
        let tokens = components.queryItems?.filter { $0.name == "token" } ?? []
        guard let origin = expectedOrigin,
              headers["origin"] == origin,
              tokens.count == 1,
              let queryToken = tokens[0].value,
              Self.constantTimeEquals(queryToken, actionToken) else {
            return textResponse(status: 403, reason: "Forbidden", text: "Reopen the display from ClassicMac.")
        }

        let connectionTokens = (headers["connection"] ?? "")
            .lowercased()
            .split(separator: ",")
            .map { $0.trimmingCharacters(in: .whitespaces) }
        guard headers["upgrade"]?.lowercased() == "websocket",
              connectionTokens.contains("upgrade"),
              headers["sec-websocket-version"] == "13",
              let wsKey = headers["sec-websocket-key"],
              !wsKey.isEmpty else {
            return textResponse(status: 400, reason: "Bad Request", text: "Bad WebSocket upgrade request")
        }

        guard let unixFD = Self.connectUnixSocket(path: endpoint.vncSocketURL.path) else {
            return textResponse(status: 503, reason: "Service Unavailable", text: "The Mac display is not ready yet.")
        }

        let acceptKey = Self.webSocketAcceptKey(for: wsKey)
        let protocols = (headers["sec-websocket-protocol"] ?? "")
            .split(separator: ",")
            .map { $0.trimmingCharacters(in: .whitespaces) }
        var handshake = "HTTP/1.1 101 Switching Protocols\r\n" +
            "Upgrade: websocket\r\n" +
            "Connection: Upgrade\r\n" +
            "Sec-WebSocket-Accept: \(acceptKey)\r\n"
        if protocols.contains("binary") {
            handshake += "Sec-WebSocket-Protocol: binary\r\n"
        }
        handshake += "\r\n"

        let proxy = WebSocketProxy(
            connection: connection,
            unixFD: unixFD,
            initialClientBytes: trailingData,
            server: self
        )
        registerProxy(proxy)
        proxy.start(handshakeResponse: Data(handshake.utf8))
        return nil
    }

    private static func webSocketAcceptKey(for clientKey: String) -> String {
        let magic = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
        let digest = Insecure.SHA1.hash(data: Data(magic.utf8))
        return Data(digest).base64EncodedString()
    }

    private static func connectUnixSocket(path: String) -> Int32? {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { return nil }
        var noSignal: Int32 = 1
        _ = setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                       &noSignal, socklen_t(MemoryLayout.size(ofValue: noSignal)))

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let maxLen = MemoryLayout.size(ofValue: addr.sun_path) - 1
        guard path.utf8.count <= maxLen else {
            close(fd)
            return nil
        }
        withUnsafeMutablePointer(to: &addr.sun_path) { rawPtr in
            rawPtr.withMemoryRebound(to: CChar.self, capacity: maxLen + 1) { dest in
                _ = path.withCString { src in
                    strncpy(dest, src, maxLen)
                }
            }
        }
        let length = socklen_t(MemoryLayout<sockaddr_un>.size)
        let rc = withUnsafePointer(to: &addr) { rawPtr in
            rawPtr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                connect(fd, sa, length)
            }
        }
        guard rc == 0 else {
            close(fd)
            return nil
        }
        let flags = fcntl(fd, F_GETFL, 0)
        if flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 {
            close(fd)
            return nil
        }
        return fd
    }

    fileprivate func response(for method: String, target: String) -> BrowserHTTPResponse {
        guard method == "GET" || method == "HEAD" else {
            return textResponse(status: 405, reason: "Method Not Allowed", text: "Method not allowed")
        }

        guard let components = URLComponents(string: "http://127.0.0.1\(target)"),
              let decodedPath = components.percentEncodedPath.removingPercentEncoding else {
            return textResponse(status: 400, reason: "Bad Request", text: "Bad request")
        }

        guard decodedPath.hasPrefix("/") else {
            return textResponse(status: 404, reason: "Not Found", text: "Not found")
        }

        let relative = decodedPath
            .trimmingCharacters(in: CharacterSet(charactersIn: "/"))

        if relative.isEmpty {
            return BrowserHTTPResponse(
                status: 200,
                reason: "OK",
                contentType: "text/html; charset=utf-8",
                body: indexData,
                contentSecurityPolicy: contentSecurityPolicy
            )
        }
        if relative == "config.json" {
            return BrowserHTTPResponse(
                status: 200,
                reason: "OK",
                contentType: "application/json; charset=utf-8",
                body: configurationData,
                contentSecurityPolicy: contentSecurityPolicy
            )
        }

        guard !relative.split(separator: "/").contains("..") else {
            return textResponse(status: 404, reason: "Not Found", text: "Not found")
        }
        let candidate = assetRoot.appendingPathComponent(relative).standardizedFileURL
        guard candidate.path.hasPrefix(assetRoot.path + "/") else {
            return textResponse(status: 404, reason: "Not Found", text: "Not found")
        }
        return fileResponse(at: candidate)
    }

    private var contentSecurityPolicy: String {
        let port = listeningPort
        return "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; " +
            "connect-src 'self' ws://127.0.0.1:\(port); " +
            "img-src 'self' data: blob:; worker-src 'self' blob:; object-src 'none'; " +
            "base-uri 'none'; frame-ancestors 'none'"
    }

    private func fileResponse(at url: URL) -> BrowserHTTPResponse {
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory),
              !isDirectory.boolValue,
              let data = try? Data(contentsOf: url) else {
            return textResponse(status: 404, reason: "Not Found", text: "Not found")
        }

        return BrowserHTTPResponse(
            status: 200,
            reason: "OK",
            contentType: Self.contentType(for: url.pathExtension),
            body: data,
            contentSecurityPolicy: contentSecurityPolicy
        )
    }

    private func textResponse(
        status: Int,
        reason: String,
        text: String
    ) -> BrowserHTTPResponse {
        BrowserHTTPResponse(
            status: status,
            reason: reason,
            contentType: "text/plain; charset=utf-8",
            body: Data(text.utf8),
            contentSecurityPolicy: contentSecurityPolicy
        )
    }

    private static func contentType(for pathExtension: String) -> String {
        switch pathExtension.lowercased() {
        case "html": return "text/html; charset=utf-8"
        case "css": return "text/css; charset=utf-8"
        case "js", "mjs": return "text/javascript; charset=utf-8"
        case "json": return "application/json; charset=utf-8"
        case "png": return "image/png"
        case "svg": return "image/svg+xml"
        case "wasm": return "application/wasm"
        default: return "application/octet-stream"
        }
    }

    private static func htmlAttributeEscape(_ value: String) -> String {
        value
            .replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "\"", with: "&quot;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
    }
}

fileprivate struct BrowserHTTPResponse {
    let status: Int
    let reason: String
    let contentType: String
    let body: Data
    let contentSecurityPolicy: String

    func serialized(headOnly: Bool) -> Data {
        let header = "HTTP/1.1 \(status) \(reason)\r\n" +
            "Content-Type: \(contentType)\r\n" +
            "Content-Length: \(body.count)\r\n" +
            "Cache-Control: no-store\r\n" +
            "Connection: close\r\n" +
            "Content-Security-Policy: \(contentSecurityPolicy)\r\n" +
            "Referrer-Policy: no-referrer\r\n" +
            "X-Content-Type-Options: nosniff\r\n" +
            "Cross-Origin-Resource-Policy: same-origin\r\n\r\n"
        var data = Data(header.utf8)
        if !headOnly {
            data.append(body)
        }
        return data
    }
}

private final class BrowserHTTPRequest: @unchecked Sendable {
    private let connection: NWConnection
    private weak var server: BrowserDisplayServer?
    private var data = Data()
    private var queue: DispatchQueue?
    private var idleTimer: DispatchWorkItem?
    private var finished = false

    init(connection: NWConnection, server: BrowserDisplayServer) {
        self.connection = connection
        self.server = server
    }

    func start(on queue: DispatchQueue) {
        self.queue = queue
        rescheduleIdleTimeout()
        connection.start(queue: queue)
        receive()
    }

    private func rescheduleIdleTimeout() {
        idleTimer?.cancel()
        guard !finished, let queue else { return }
        let timeout = server?.idleReadTimeout ?? 10
        let item = DispatchWorkItem { [weak self] in
            guard let self, !self.finished else { return }
            self.finished = true
            self.connection.cancel()
        }
        idleTimer = item
        queue.asyncAfter(deadline: .now() + timeout, execute: item)
    }

    private func cancelIdleTimeout() {
        idleTimer?.cancel()
        idleTimer = nil
    }

    private func receive() {
        connection.receive(
            minimumIncompleteLength: 1,
            maximumLength: 8_192
        ) { [self] chunk, _, complete, error in
            guard !finished else { return }
            if let chunk, !chunk.isEmpty {
                data.append(chunk)
                rescheduleIdleTimeout()
            }

            if let headerEnd = data.range(of: Data("\r\n\r\n".utf8)) {
                cancelIdleTimeout()
                finished = true
                respond(headerEnd: headerEnd)
                return
            }

            if data.count > 64 * 1_024 {
                cancelIdleTimeout()
                finished = true
                sendBadRequest()
                return
            }

            if complete || error != nil {
                cancelIdleTimeout()
                finished = true
                connection.cancel()
                return
            }
            receive()
        }
    }

    private func respond(headerEnd: Range<Data.Index>) {
        let headerData = data[data.startIndex..<headerEnd.lowerBound]
        let trailingData = Data(data[headerEnd.upperBound..<data.endIndex])
        guard let request = String(data: headerData, encoding: .utf8) else {
            sendBadRequest()
            return
        }
        let lines = request.components(separatedBy: "\r\n")
        guard let firstLine = lines.first else {
            sendBadRequest()
            return
        }
        let parts = firstLine.split(separator: " ", maxSplits: 2).map(String.init)
        guard parts.count == 3,
              parts[2].hasPrefix("HTTP/1.") else {
            sendBadRequest()
            return
        }

        let singleValueHeaders: Set<String> = [
            "origin", "x-classicmac-action", "sec-websocket-key",
            "sec-websocket-version", "upgrade", "connection", "host"
        ]
        var headers: [String: String] = [:]
        for line in lines.dropFirst() {
            if line.isEmpty { break }
            let pair = line.split(separator: ":", maxSplits: 1, omittingEmptySubsequences: false)
            guard pair.count == 2 else { sendBadRequest(); return }
            let key = pair[0].trimmingCharacters(in: .whitespaces).lowercased()
            let value = pair[1].trimmingCharacters(in: .whitespaces)
            if singleValueHeaders.contains(key), headers[key] != nil {
                sendBadRequest()
                return
            }
            if headers[key] == nil {
                headers[key] = value
            }
        }

        guard let server else {
            connection.cancel()
            return
        }

        if parts[0] == "POST", ["/actions/paste-text", "/actions/paste-text-state", "/actions/media"].contains(parts[1]) {
            Task {
                let response = parts[1] == "/actions/media"
                    ? await server.mediaResponse(headers: headers)
                    : await server.pasteTextResponse(headers: headers, openWindow: parts[1] == "/actions/paste-text")
                send(response.serialized(headOnly: false))
            }
            return
        }

        if let components = URLComponents(string: "http://127.0.0.1\(parts[1])"),
           components.path == "/websockify" {
            if let errorResponse = server.upgradeWebSocket(
                connection: connection,
                method: parts[0],
                components: components,
                headers: headers,
                trailingData: trailingData
            ) {
                send(errorResponse.serialized(headOnly: parts[0] == "HEAD"))
            }
            return
        }

        let response = server.response(for: parts[0], target: parts[1])
        send(response.serialized(headOnly: parts[0] == "HEAD"))
    }

    private func sendBadRequest() {
        let response = BrowserHTTPResponse(
            status: 400,
            reason: "Bad Request",
            contentType: "text/plain; charset=utf-8",
            body: Data("Bad request".utf8),
            contentSecurityPolicy: "default-src 'none'"
        )
        send(response.serialized(headOnly: false))
    }

    private func send(_ data: Data) {
        connection.send(
            content: data,
            contentContext: .finalMessage,
            isComplete: true,
            completion: .contentProcessed { [connection] _ in
                connection.cancel()
            }
        )
    }
}

/// Bridges an authenticated RFC 6455 WebSocket connection to QEMU's private
/// Unix-domain VNC socket, forwarding raw RFB byte streams bidirectionally.
fileprivate final class WebSocketProxy: @unchecked Sendable {
    private static let maxWebSocketFramePayload = 16 * 1024 * 1024

    private let connection: NWConnection
    private let unixFD: Int32
    private weak var server: BrowserDisplayServer?
    private let queue = DispatchQueue(label: "com.classicmac.browser-ws-proxy")

    private var readSource: DispatchSourceRead?
    private var readSourceSuspended = false
    private var writeSource: DispatchSourceWrite?
    private var pendingUnixWrite = Data()
    private var wsBuffer: Data
    private var pendingWSSends = 0
    private var closed = false

    init(connection: NWConnection, unixFD: Int32, initialClientBytes: Data, server: BrowserDisplayServer) {
        self.connection = connection
        self.unixFD = unixFD
        self.wsBuffer = initialClientBytes
        self.server = server
    }

    func start(handshakeResponse: Data) {
        queue.async { [self] in
            guard !closed else {
                Darwin.close(unixFD)
                return
            }
            let fd = unixFD
            let rs = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
            rs.setEventHandler { [weak self] in
                self?.readFromUnix()
            }
            rs.setCancelHandler {
                Darwin.close(fd)
            }
            readSource = rs

            connection.send(
                content: handshakeResponse,
                completion: .contentProcessed { [weak self] error in
                    guard let self else { return }
                    self.queue.async {
                        guard !self.closed else { return }
                        if error != nil {
                            self.closeNow()
                            return
                        }
                        self.readSource?.resume()
                        if !self.wsBuffer.isEmpty {
                            self.processWebSocketBuffer()
                        }
                        self.receiveFromWebSocket()
                    }
                }
            )
        }
    }

    func stop() {
        queue.async { [self] in
            closeNow()
        }
    }

    private func closeNow() {
        guard !closed else { return }
        closed = true
        writeSource?.cancel()
        writeSource = nil
        if let rs = readSource {
            if readSourceSuspended {
                readSourceSuspended = false
                rs.resume()
            }
            rs.cancel()
            readSource = nil
        }
        connection.cancel()
        server?.unregisterProxy(self)
    }

    private func receiveFromWebSocket() {
        guard !closed else { return }
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65_536) { [weak self] chunk, _, complete, error in
            guard let self else { return }
            self.queue.async {
                guard !self.closed else { return }
                if let chunk, !chunk.isEmpty {
                    self.wsBuffer.append(chunk)
                    self.processWebSocketBuffer()
                }
                if self.closed { return }
                if complete || error != nil {
                    self.closeNow()
                    return
                }
                self.receiveFromWebSocket()
            }
        }
    }

    private func processWebSocketBuffer() {
        var offset = 0
        let start = wsBuffer.startIndex
        let count = wsBuffer.count

        while !closed && count - offset >= 2 {
            let b0 = wsBuffer[start + offset]
            let b1 = wsBuffer[start + offset + 1]
            let rsv = b0 & 0x70
            let opcode = b0 & 0x0F
            let masked = (b1 & 0x80) != 0
            let len7 = Int(b1 & 0x7F)

            if rsv != 0 || !masked {
                sendCloseFrame(code: 1002)
                closeNow()
                return
            }

            let headerLen: Int
            let payloadLen: Int
            if len7 < 126 {
                headerLen = 2
                payloadLen = len7
            } else if len7 == 126 {
                guard count - offset >= 4 else { break }
                headerLen = 4
                payloadLen = (Int(wsBuffer[start + offset + 2]) << 8) | Int(wsBuffer[start + offset + 3])
            } else {
                guard count - offset >= 10 else { break }
                headerLen = 10
                var len64: UInt64 = 0
                for i in 0..<8 {
                    len64 = (len64 << 8) | UInt64(wsBuffer[start + offset + 2 + i])
                }
                guard (len64 & (1 << 63)) == 0,
                      len64 <= UInt64(Self.maxWebSocketFramePayload) else {
                    sendCloseFrame(code: 1009)
                    closeNow()
                    return
                }
                payloadLen = Int(len64)
            }

            let totalFrameBytes = headerLen + 4 + payloadLen
            guard count - offset >= totalFrameBytes else { break }

            let maskStart = start + offset + headerLen
            let payloadStart = maskStart + 4
            var payload = [UInt8](repeating: 0, count: payloadLen)
            for i in 0..<payloadLen {
                payload[i] = wsBuffer[payloadStart + i] ^ wsBuffer[maskStart + (i & 3)]
            }
            offset += totalFrameBytes

            switch opcode {
            case 0x0, 0x1, 0x2:
                if !payload.isEmpty {
                    enqueueUnixWrite(Data(payload))
                }
            case 0x8:
                let closePayload = payload.count >= 2 ? Array(payload.prefix(2)) : []
                sendWebSocketFrame(opcode: 0x8, payload: closePayload) { [weak self] in
                    self?.queue.async { self?.closeNow() }
                }
                return
            case 0x9:
                guard payload.count <= 125 else {
                    sendCloseFrame(code: 1002)
                    closeNow()
                    return
                }
                sendWebSocketFrame(opcode: 0xA, payload: payload)
            case 0xA:
                break
            default:
                sendCloseFrame(code: 1002)
                closeNow()
                return
            }
        }

        if offset > 0 {
            wsBuffer.removeSubrange(start..<(start + offset))
        }
    }

    private func readFromUnix() {
        guard !closed else { return }
        var buffer = [UInt8](repeating: 0, count: 65_536)
        while !closed {
            let n = recv(unixFD, &buffer, buffer.count, 0)
            if n > 0 {
                sendWebSocketFrame(opcode: 0x2, payload: Array(buffer[0..<n]))
                if pendingWSSends >= 32, !readSourceSuspended, let rs = readSource {
                    readSourceSuspended = true
                    rs.suspend()
                    return
                }
            } else if n == 0 {
                sendCloseFrame(code: 1000) { [weak self] in
                    self?.queue.async { self?.closeNow() }
                }
                return
            } else {
                if errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR {
                    return
                }
                closeNow()
                return
            }
        }
    }

    private func enqueueUnixWrite(_ data: Data) {
        pendingUnixWrite.append(data)
        flushUnixWrite()
    }

    private func flushUnixWrite() {
        guard !closed else { return }
        while !pendingUnixWrite.isEmpty {
            let written: Int = pendingUnixWrite.withUnsafeBytes { raw in
                guard let base = raw.baseAddress else { return 0 }
                return send(unixFD, base, pendingUnixWrite.count, 0)
            }
            if written > 0 {
                pendingUnixWrite.removeFirst(written)
            } else if written < 0 && errno == EINTR {
                continue
            } else if written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) {
                ensureWriteSource()
                return
            } else {
                closeNow()
                return
            }
        }
        if let ws = writeSource {
            ws.cancel()
            writeSource = nil
        }
    }

    private func ensureWriteSource() {
        guard writeSource == nil, !closed else { return }
        let ws = DispatchSource.makeWriteSource(fileDescriptor: unixFD, queue: queue)
        ws.setEventHandler { [weak self] in
            self?.flushUnixWrite()
        }
        writeSource = ws
        ws.resume()
    }

    private func sendCloseFrame(code: UInt16, completion: (() -> Void)? = nil) {
        let payload = [UInt8(code >> 8), UInt8(code & 0xFF)]
        sendWebSocketFrame(opcode: 0x8, payload: payload, completion: completion)
    }

    private func sendWebSocketFrame(opcode: UInt8, payload: [UInt8], completion: (() -> Void)? = nil) {
        guard !closed else { return }
        var frame = Data()
        let len = payload.count
        frame.append(0x80 | (opcode & 0x0F))
        if len < 126 {
            frame.append(UInt8(len))
        } else if len <= 65_535 {
            frame.append(126)
            frame.append(UInt8((len >> 8) & 0xFF))
            frame.append(UInt8(len & 0xFF))
        } else {
            frame.append(127)
            let len64 = UInt64(len)
            for shift in stride(from: 56, through: 0, by: -8) {
                frame.append(UInt8((len64 >> shift) & 0xFF))
            }
        }
        frame.append(contentsOf: payload)

        pendingWSSends += 1
        connection.send(
            content: frame,
            completion: .contentProcessed { [weak self] error in
                guard let self else { return }
                self.queue.async {
                    self.pendingWSSends = max(0, self.pendingWSSends - 1)
                    if self.readSourceSuspended, self.pendingWSSends < 8, !self.closed, let rs = self.readSource {
                        self.readSourceSuspended = false
                        rs.resume()
                    }
                    if error != nil {
                        self.closeNow()
                    }
                    completion?()
                }
            }
        )
    }
}
