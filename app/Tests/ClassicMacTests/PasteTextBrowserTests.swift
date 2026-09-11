import XCTest
@testable import ClassicMac

@MainActor
final class PasteTextBrowserTests: XCTestCase {
    private func server() throws -> BrowserDisplayServer {
        try BrowserDisplayServer.start(for: VMConfig(
            name: "Paste Test Mac", machineFamily: .powerMacG4,
            ramMB: MachineFamily.powerMacG4.defaultRAMMB, sound: false,
            bundleURL: URL(fileURLWithPath: "/tmp/paste-browser-test.classic")
        ))
    }

    private func token(for server: BrowserDisplayServer) async throws -> String {
        let (data, response) = try await URLSession.shared.data(from: server.url)
        let html = String(decoding: data, as: UTF8.self)
        let pattern = "name=\"classicmac-action-token\" content=\"([^\"]+)\""
        let expression = try NSRegularExpression(pattern: pattern)
        let match = try XCTUnwrap(expression.firstMatch(in: html, range: NSRange(html.startIndex..., in: html)))
        let range = try XCTUnwrap(Range(match.range(at: 1), in: html))
        XCTAssertTrue((response as? HTTPURLResponse)?.value(forHTTPHeaderField: "Content-Security-Policy")?.contains("connect-src 'self'") == true)
        return String(html[range])
    }

    private func request(_ server: BrowserDisplayServer, token: String?, origin: String?, state: Bool = false) async throws -> (Data, Int?) {
        var request = URLRequest(url: server.url.appendingPathComponent(state ? "actions/paste-text-state" : "actions/paste-text"))
        request.httpMethod = "POST"
        if let token { request.setValue(token, forHTTPHeaderField: "X-ClassicMac-Action") }
        if let origin { request.setValue(origin, forHTTPHeaderField: "Origin") }
        let (data, response) = try await URLSession.shared.data(for: request)
        return (data, (response as? HTTPURLResponse)?.statusCode)
    }

    func testEachDisplayGetsItsOwnUnpredictableActionToken() async throws {
        let first = try server()
        let second = try server()
        defer { first.stop(); second.stop() }
        let firstToken = try await token(for: first)
        let secondToken = try await token(for: second)
        XCTAssertNotEqual(firstToken, secondToken)
        XCTAssertNotNil(UUID(uuidString: firstToken))
    }

    func testPasteActionRequiresTokenAndExactOrigin() async throws {
        let server = try server()
        defer { server.stop() }
        let token = try await token(for: server)
        let origin = "http://127.0.0.1:\(server.url.port!)"
        for (candidateToken, candidateOrigin) in [
            (nil, origin), (token, nil), ("wrong-token", origin),
            (token, "https://example.com"), (token, "http://localhost:\(server.url.port!)")
        ] as [(String?, String?)] {
            let (_, status) = try await request(server, token: candidateToken, origin: candidateOrigin)
            XCTAssertEqual(status, 403)
        }
        let (_, stateStatus) = try await request(server, token: token, origin: "https://example.com", state: true)
        XCTAssertEqual(stateStatus, 403)
    }

    func testAuthenticatedPasteIsRejectedWhileMachineIsStopped() async throws {
        let server = try server()
        defer { server.stop() }
        let token = try await token(for: server)
        let origin = "http://127.0.0.1:\(server.url.port!)"
        let (body, status) = try await request(server, token: token, origin: origin)
        XCTAssertEqual(status, 409)
        XCTAssertTrue(String(decoding: body, as: UTF8.self).contains("Resume or start"))
        let (stateBody, stateStatus) = try await request(server, token: token, origin: origin, state: true)
        XCTAssertEqual(stateStatus, 200)
        let state = try JSONSerialization.jsonObject(with: stateBody) as? [String: Bool]
        XCTAssertEqual(state?["canPaste"], false)
    }
}
