import Foundation
import Network
import Darwin

struct BrowserDisplayEndpoint: Equatable {
    let vmID: UUID
    let webSocketPort: UInt16

    static func vncSocketURL(for vmID: UUID) -> URL {
        let directory = URL(fileURLWithPath: "/tmp/ClassicMac", isDirectory: true)
        AppPaths.ensureDirectory(directory)
        return directory.appendingPathComponent("\(vmID.uuidString).vnc.sock")
    }

    var vncSocketURL: URL { Self.vncSocketURL(for: vmID) }

    var qemuDisplay: String {
        "vnc=unix:\(vncSocketURL.path),websocket=127.0.0.1:\(webSocketPort),share=force-shared"
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

// A tiny, loopback-only HTTP server for the browser display. QEMU owns the
// WebSocket endpoint; this server only publishes the bundled HTML, CSS, and
// noVNC modules plus per-run configuration embedded in the index page.
final class BrowserDisplayServer: @unchecked Sendable {
    let endpoint: BrowserDisplayEndpoint
    private(set) var url: URL

    private let listener: NWListener
    private let queue: DispatchQueue
    private let assetRoot: URL
    private let configurationData: Data
    private let indexData: Data

    static func start(for config: VMConfig) throws -> BrowserDisplayServer {
        let webSocketPort = try availableTCPPort()
        let endpoint = BrowserDisplayEndpoint(
            vmID: config.id,
            webSocketPort: webSocketPort
        )
        return try BrowserDisplayServer(
            endpoint: endpoint,
            machineName: config.name,
            classicInputHelpers: config.classicInputHelpers
        )
    }

    private init(
        endpoint: BrowserDisplayEndpoint,
        machineName: String,
        classicInputHelpers: Bool
    ) throws {
        let root = AppPaths.browserAssetsDir
        guard FileManager.default.fileExists(
            atPath: root.appendingPathComponent("index.html").path
        ) else {
            throw BrowserDisplayError.assetsMissing
        }

        self.endpoint = endpoint
        self.assetRoot = root.standardizedFileURL
        self.queue = DispatchQueue(
            label: "com.classicmac.browser-display.\(endpoint.vmID.uuidString)"
        )

        self.configurationData = try JSONSerialization.data(
            withJSONObject: [
                "machineName": machineName,
                "webSocketURL": "ws://127.0.0.1:\(endpoint.webSocketPort)",
                "classicInputHelpers": classicInputHelpers
            ],
            options: []
        )
        let templateURL = root.appendingPathComponent("index.html")
        guard let template = try? String(contentsOf: templateURL, encoding: .utf8) else {
            throw BrowserDisplayError.assetsMissing
        }
        let renderedIndex = template
            .replacingOccurrences(
                of: "__CLASSICMAC_MACHINE_NAME__",
                with: Self.htmlAttributeEscape(machineName)
            )
            .replacingOccurrences(
                of: "__CLASSICMAC_WEBSOCKET_URL__",
                with: "ws://127.0.0.1:\(endpoint.webSocketPort)"
            )
            .replacingOccurrences(
                of: "__CLASSICMAC_INPUT_HELPERS__",
                with: classicInputHelpers ? "true" : "false"
            )
        self.indexData = Data(renderedIndex.utf8)

        let parameters = NWParameters.tcp
        parameters.requiredLocalEndpoint = .hostPort(
            host: "127.0.0.1",
            port: .any
        )
        self.listener = try NWListener(using: parameters)

        let started = ListenerStartBox()
        let semaphore = DispatchSemaphore(value: 0)
        listener.stateUpdateHandler = { [weak listener] state in
            switch state {
            case .ready:
                guard let port = listener?.port?.rawValue else {
                    if started.resolve(.failure(
                        BrowserDisplayError.serverFailed("No listening port was assigned.")
                    )) {
                        semaphore.signal()
                    }
                    return
                }
                if started.resolve(.success(port)) {
                    semaphore.signal()
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

        // url is initialized after the listener reports its kernel-assigned
        // port. Use a temporary value to satisfy Swift's initialization rules.
        self.url = URL(string: "http://127.0.0.1/")!
        listener.newConnectionHandler = { [weak self] connection in
            self?.accept(connection)
        }
        listener.start(queue: queue)

        guard semaphore.wait(timeout: .now() + 3) == .success else {
            listener.cancel()
            throw BrowserDisplayError.serverTimedOut
        }

        let port: UInt16
        switch started.value() {
        case .success(let value):
            port = value
        case .failure(let error):
            listener.cancel()
            throw error
        case .none:
            listener.cancel()
            throw BrowserDisplayError.serverTimedOut
        }

        guard let publicURL = URL(
            string: "http://127.0.0.1:\(port)/"
        ) else {
            listener.cancel()
            throw BrowserDisplayError.serverFailed("The local URL was invalid.")
        }
        self.url = publicURL
    }

    func stop() {
        listener.cancel()
    }

    private func accept(_ connection: NWConnection) {
        BrowserHTTPRequest(connection: connection, server: self).start(on: queue)
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
        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; " +
        "connect-src ws://127.0.0.1:\(endpoint.webSocketPort); " +
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

    private static func availableTCPPort() throws -> UInt16 {
        let descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
        guard descriptor >= 0 else {
            throw BrowserDisplayError.portAllocationFailed(
                String(cString: strerror(errno))
            )
        }
        defer { close(descriptor) }

        var address = sockaddr_in()
        address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        address.sin_family = sa_family_t(AF_INET)
        address.sin_port = 0
        address.sin_addr = in_addr(s_addr: inet_addr("127.0.0.1"))

        let bindResult = withUnsafePointer(to: &address) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(descriptor, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bindResult == 0 else {
            throw BrowserDisplayError.portAllocationFailed(
                String(cString: strerror(errno))
            )
        }

        var length = socklen_t(MemoryLayout<sockaddr_in>.size)
        let nameResult = withUnsafeMutablePointer(to: &address) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                getsockname(descriptor, $0, &length)
            }
        }
        guard nameResult == 0 else {
            throw BrowserDisplayError.portAllocationFailed(
                String(cString: strerror(errno))
            )
        }
        return UInt16(bigEndian: address.sin_port)
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

    init(connection: NWConnection, server: BrowserDisplayServer) {
        self.connection = connection
        self.server = server
    }

    func start(on queue: DispatchQueue) {
        connection.start(queue: queue)
        receive()
    }

    private func receive() {
        connection.receive(
            minimumIncompleteLength: 1,
            maximumLength: 8_192
        ) { [self] chunk, _, complete, error in
            if let chunk {
                data.append(chunk)
            }

            if data.count > 64 * 1_024 {
                sendBadRequest()
                return
            }

            if data.range(of: Data("\r\n\r\n".utf8)) != nil {
                respond()
                return
            }

            if complete || error != nil {
                connection.cancel()
                return
            }
            receive()
        }
    }

    private func respond() {
        guard let request = String(data: data, encoding: .utf8),
              let firstLine = request.components(separatedBy: "\r\n").first else {
            sendBadRequest()
            return
        }
        let parts = firstLine.split(separator: " ", maxSplits: 2).map(String.init)
        guard parts.count == 3,
              parts[2].hasPrefix("HTTP/1.") else {
            sendBadRequest()
            return
        }

        guard let server else {
            connection.cancel()
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
