import XCTest
import CryptoKit
import Network
import Darwin
@testable import ClassicMac

@MainActor
final class BrowserDisplayServerTests: XCTestCase {
    private func config() -> VMConfig {
        VMConfig(
            name: "Browser Test Mac",
            machineFamily: .powerMacG4,
            ramMB: MachineFamily.powerMacG4.defaultRAMMB,
            sound: false,
            classicInputHelpers: true,
            bundleURL: URL(fileURLWithPath: "/tmp/browser-test.classic")
        )
    }

    func testEndpointUsesPrivateUnixSocketWithoutDirectWebSocketPort() {
        let id = UUID()
        let endpoint = BrowserDisplayEndpoint(vmID: id)

        XCTAssertEqual(
            endpoint.vncSocketURL.path,
            AppPaths.runtimeSocketDir.appendingPathComponent("\(id.uuidString).vnc.sock").path
        )
        XCTAssertEqual(
            endpoint.qemuDisplay,
            "vnc=unix:\(endpoint.vncSocketURL.path),share=force-shared"
        )
        XCTAssertFalse(endpoint.qemuDisplay.contains("websocket="))
    }

    func testLoopbackServerPublishesViewerAndPerMachineConfiguration() async throws {
        let server = try BrowserDisplayServer.start(for: config())
        defer { server.stop() }

        XCTAssertEqual(server.url.host, "127.0.0.1")
        let port = try XCTUnwrap(server.url.port)

        let (pageData, pageResponse) = try await URLSession.shared.data(from: server.url)
        XCTAssertEqual((pageResponse as? HTTPURLResponse)?.statusCode, 200)
        let page = String(decoding: pageData, as: UTF8.self)
        XCTAssertTrue(page.contains("Classic Mac display"))
        XCTAssertTrue(page.contains("Waiting for the Mac"))
        XCTAssertTrue(page.contains("may be shut down or restarting"))
        XCTAssertTrue(page.contains("reconnect automatically"))
        XCTAssertTrue(page.contains("content=\"Browser Test Mac\""))

        let token = try extractActionToken(from: page)
        let expectedWSURL = "ws://127.0.0.1:\(port)/websockify?token=\(token)"
        XCTAssertTrue(page.contains("content=\"\(expectedWSURL)\""))

        let configurationURL = server.url.appendingPathComponent("config.json")
        let (configurationData, configurationResponse) = try await URLSession.shared.data(
            from: configurationURL
        )
        XCTAssertEqual((configurationResponse as? HTTPURLResponse)?.statusCode, 200)

        let object = try XCTUnwrap(
            JSONSerialization.jsonObject(with: configurationData) as? [String: Any]
        )
        XCTAssertEqual(object["machineName"] as? String, "Browser Test Mac")
        XCTAssertEqual(object["classicInputHelpers"] as? Bool, true)
        XCTAssertEqual(object["webSocketURL"] as? String, expectedWSURL)

        let traversalURL = URL(
            string: "%2e%2e/config.json",
            relativeTo: server.url
        )!
        let (_, traversalResponse) = try await URLSession.shared.data(from: traversalURL)
        XCTAssertEqual((traversalResponse as? HTTPURLResponse)?.statusCode, 404)
    }

    func testWebSocketUpgradeRequiresValidOriginAndToken() async throws {
        let server = try BrowserDisplayServer.start(for: config())
        defer { server.stop() }
        let port = try XCTUnwrap(server.url.port)
        let (pageData, _) = try await URLSession.shared.data(from: server.url)
        let token = try extractActionToken(from: pageData)
        let origin = "http://127.0.0.1:\(port)"
        let key = Data("0123456789abcdef".utf8).base64EncodedString()

        let rejectedCases: [(path: String, origin: String?)] = [
            ("/websockify", origin),
            ("/websockify?token=wrong", origin),
            ("/websockify?token=wrong&token=\(token)", origin),
            ("/websockify?token=\(token)", nil),
            ("/websockify?token=\(token)", "http://localhost:\(port)"),
            ("/websockify?token=\(token)", "https://example.com"),
        ]
        for testCase in rejectedCases {
            let reply = try await rawHTTPExchange(
                port: UInt16(port),
                request: "GET \(testCase.path) HTTP/1.1\r\n" +
                    "Host: 127.0.0.1:\(port)\r\n" +
                    (testCase.origin.map { "Origin: \($0)\r\n" } ?? "") +
                    "Upgrade: websocket\r\n" +
                    "Connection: keep-alive, Upgrade\r\n" +
                    "Sec-WebSocket-Key: \(key)\r\n" +
                    "Sec-WebSocket-Version: 13\r\n\r\n"
            )
            XCTAssertTrue(reply.hasPrefix("HTTP/1.1 403 "), "Expected 403 for \(testCase), got: \(reply)")
        }

        // Valid auth without WebSocket upgrade headers returns 400.
        let badUpgrade = try await rawHTTPExchange(
            port: UInt16(port),
            request: "GET /websockify?token=\(token) HTTP/1.1\r\n" +
                "Host: 127.0.0.1:\(port)\r\n" +
                "Origin: \(origin)\r\n\r\n"
        )
        XCTAssertTrue(badUpgrade.hasPrefix("HTTP/1.1 400 "), "Expected 400, got: \(badUpgrade)")

        // Valid auth + upgrade when QEMU VNC Unix socket does not exist returns 503.
        try? FileManager.default.removeItem(at: server.endpoint.vncSocketURL)
        let notReady = try await rawHTTPExchange(
            port: UInt16(port),
            request: "GET /websockify?token=\(token) HTTP/1.1\r\n" +
                "Host: 127.0.0.1:\(port)\r\n" +
                "Origin: \(origin)\r\n" +
                "Upgrade: websocket\r\n" +
                "Connection: Upgrade\r\n" +
                "Sec-WebSocket-Key: \(key)\r\n" +
                "Sec-WebSocket-Version: 13\r\n\r\n"
        )
        XCTAssertTrue(notReady.hasPrefix("HTTP/1.1 503 "), "Expected 503, got: \(notReady)")
    }

    func testAuthenticatedWebSocketProxiesRFBFramesToUnixSocket() async throws {
        let server = try BrowserDisplayServer.start(for: config())
        defer { server.stop() }
        let port = try XCTUnwrap(server.url.port)
        let (pageData, _) = try await URLSession.shared.data(from: server.url)
        let token = try extractActionToken(from: pageData)

        let listenFD = try bindUnixListener(at: server.endpoint.vncSocketURL.path)
        defer {
            close(listenFD)
            try? FileManager.default.removeItem(at: server.endpoint.vncSocketURL)
        }

        // Background fake QEMU VNC server: sends RFB banner, echoes client bytes back.
        let serverTask = Task.detached { () -> Data in
            var pfd = pollfd(fd: listenFD, events: Int16(POLLIN), revents: 0)
            guard poll(&pfd, 1, 2000) > 0 else { return Data() }
            let clientFD = accept(listenFD, nil, nil)
            guard clientFD >= 0 else { return Data() }
            defer { close(clientFD) }
            let banner = Data("RFB 003.008\n".utf8)
            _ = banner.withUnsafeBytes { write(clientFD, $0.baseAddress, banner.count) }
            var buf = [UInt8](repeating: 0, count: 64)
            var rxPfd = pollfd(fd: clientFD, events: Int16(POLLIN), revents: 0)
            guard poll(&rxPfd, 1, 2000) > 0 else { return Data() }
            let n = recv(clientFD, &buf, buf.count, 0)
            guard n > 0 else { return Data() }
            let received = Data(buf[0..<n])
            _ = received.withUnsafeBytes { write(clientFD, $0.baseAddress, received.count) }
            return received
        }

        let wsFD = try Self.connectLoopbackTCP(port: UInt16(port))
        defer { close(wsFD) }
        let key = Data("0123456789abcdef".utf8).base64EncodedString()
        let upgrade = "GET /websockify?token=\(token) HTTP/1.1\r\n" +
            "Host: 127.0.0.1:\(port)\r\n" +
            "Origin: http://127.0.0.1:\(port)\r\n" +
            "Upgrade: websocket\r\n" +
            "Connection: Upgrade\r\n" +
            "Sec-WebSocket-Key: \(key)\r\n" +
            "Sec-WebSocket-Version: 13\r\n\r\n"
        _ = Array(upgrade.utf8).withUnsafeBytes { write(wsFD, $0.baseAddress, $0.count) }

        var rxBuffer = Data()
        let headerEnd = try readUntil(Data("\r\n\r\n".utf8), from: wsFD, into: &rxBuffer)
        let responseHeaders = String(decoding: rxBuffer[..<headerEnd.lowerBound], as: UTF8.self)
        rxBuffer.removeSubrange(..<headerEnd.upperBound)
        XCTAssertTrue(responseHeaders.hasPrefix("HTTP/1.1 101 Switching Protocols"))
        let expectedAccept = Data(Insecure.SHA1.hash(data: Data((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").utf8))).base64EncodedString()
        XCTAssertTrue(responseHeaders.contains("Sec-WebSocket-Accept: \(expectedAccept)"))

        // Read RFB banner wrapped in a server binary WebSocket frame.
        let bannerPayload = try readWebSocketFrame(from: wsFD, buffer: &rxBuffer)
        XCTAssertEqual(String(decoding: bannerPayload, as: UTF8.self), "RFB 003.008\n")

        // Send masked client WebSocket binary frame and verify QEMU unix socket receives it & echoes back.
        let clientMessage = Data("RFB 003.008\n".utf8)
        let maskedFrame = makeMaskedBinaryFrame(clientMessage)
        _ = maskedFrame.withUnsafeBytes { write(wsFD, $0.baseAddress, maskedFrame.count) }

        let receivedOnUnix = await serverTask.value
        XCTAssertEqual(receivedOnUnix, clientMessage)
        let echoedPayload = try readWebSocketFrame(from: wsFD, buffer: &rxBuffer)
        XCTAssertEqual(echoedPayload, clientMessage)
    }

    func testIdleHTTPConnectionIsClosedAfterReadTimeout() async throws {
        let server = try BrowserDisplayServer.start(for: config(), idleReadTimeout: 0.25)
        defer { server.stop() }
        let port = try XCTUnwrap(server.url.port)

        let fd = try Self.connectLoopbackTCP(port: UInt16(port))
        defer { close(fd) }
        let partial = Data("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n".utf8)
        _ = partial.withUnsafeBytes { write(fd, $0.baseAddress, partial.count) }

        var pfd = pollfd(fd: fd, events: Int16(POLLIN), revents: 0)
        let ready = poll(&pfd, 1, 2000)
        XCTAssertGreaterThan(ready, 0, "Expected server to close idle connection")
        var byte: UInt8 = 0
        XCTAssertEqual(recv(fd, &byte, 1, 0), 0)
    }

    private func extractActionToken(from data: Data) throws -> String {
        let html = String(decoding: data, as: UTF8.self)
        return try extractActionToken(from: html)
    }

    private func extractActionToken(from html: String) throws -> String {
        let expression = try NSRegularExpression(pattern: "name=\"classicmac-action-token\" content=\"([^\"]+)\"")
        let match = try XCTUnwrap(expression.firstMatch(in: html, range: NSRange(html.startIndex..., in: html)))
        return String(html[try XCTUnwrap(Range(match.range(at: 1), in: html))])
    }

    nonisolated private static func connectLoopbackTCP(port: UInt16) throws -> Int32 {
        let fd = socket(AF_INET, SOCK_STREAM, 0)
        guard fd >= 0 else { throw POSIXError(.EIO) }
        var addr = sockaddr_in()
        addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = port.bigEndian
        addr.sin_addr = in_addr(s_addr: inet_addr("127.0.0.1"))
        let rc = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                connect(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard rc == 0 else {
            close(fd)
            throw POSIXError(.ECONNREFUSED)
        }
        return fd
    }

    private func bindUnixListener(at path: String) throws -> Int32 {
        unlink(path)
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { throw POSIXError(.EIO) }
        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let maxLen = MemoryLayout.size(ofValue: addr.sun_path) - 1
        withUnsafeMutablePointer(to: &addr.sun_path) { rawPtr in
            rawPtr.withMemoryRebound(to: CChar.self, capacity: maxLen + 1) { dest in
                _ = path.withCString { src in strncpy(dest, src, maxLen) }
            }
        }
        let rc = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.bind(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        guard rc == 0, listen(fd, 2) == 0 else {
            close(fd)
            throw POSIXError(.EIO)
        }
        return fd
    }

    private func rawHTTPExchange(port: UInt16, request: String) async throws -> String {
        try await Task.detached {
            let fd = try Self.connectLoopbackTCP(port: port)
            defer { close(fd) }
            let reqBytes = Array(request.utf8)
            _ = reqBytes.withUnsafeBytes { write(fd, $0.baseAddress, $0.count) }
            var out = Data()
            var buf = [UInt8](repeating: 0, count: 4096)
            while true {
                var pfd = pollfd(fd: fd, events: Int16(POLLIN), revents: 0)
                guard poll(&pfd, 1, 2000) > 0 else { break }
                let n = recv(fd, &buf, buf.count, 0)
                if n <= 0 { break }
                out.append(buf, count: n)
            }
            return String(decoding: out, as: UTF8.self)
        }.value
    }

    private func readUntil(_ marker: Data, from fd: Int32, into buffer: inout Data) throws -> Range<Data.Index> {
        var tmp = [UInt8](repeating: 0, count: 4096)
        while true {
            if let range = buffer.range(of: marker) { return range }
            var pfd = pollfd(fd: fd, events: Int16(POLLIN), revents: 0)
            guard poll(&pfd, 1, 2000) > 0 else { throw POSIXError(.ETIMEDOUT) }
            let n = recv(fd, &tmp, tmp.count, 0)
            guard n > 0 else { throw POSIXError(.ECONNRESET) }
            buffer.append(tmp, count: n)
        }
    }

    private func readWebSocketFrame(from fd: Int32, buffer: inout Data) throws -> Data {
        var tmp = [UInt8](repeating: 0, count: 4096)
        func ensure(_ count: Int) throws {
            while buffer.count < count {
                var pfd = pollfd(fd: fd, events: Int16(POLLIN), revents: 0)
                guard poll(&pfd, 1, 2000) > 0 else { throw POSIXError(.ETIMEDOUT) }
                let n = recv(fd, &tmp, tmp.count, 0)
                guard n > 0 else { throw POSIXError(.ECONNRESET) }
                buffer.append(tmp, count: n)
            }
        }
        try ensure(2)
        let b1 = buffer[buffer.startIndex + 1]
        let len = Int(b1 & 0x7F)
        precondition(len < 126)
        try ensure(2 + len)
        let start = buffer.startIndex + 2
        let payload = Data(buffer[start..<(start + len)])
        buffer.removeFirst(2 + len)
        return payload
    }

    private func makeMaskedBinaryFrame(_ payload: Data) -> Data {
        var frame = Data([0x82, UInt8(0x80 | payload.count)])
        let mask: [UInt8] = [0x12, 0x34, 0x56, 0x78]
        frame.append(contentsOf: mask)
        for (i, byte) in payload.enumerated() {
            frame.append(byte ^ mask[i & 3])
        }
        return frame
    }
}
