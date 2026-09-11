import XCTest
@testable import ClassicMac

@MainActor
final class MediaBrowserTests: XCTestCase {
    func testMediaWindowRequestRequiresTheDisplayTokenAndExactOrigin() async throws {
        let server = try BrowserDisplayServer.start(for: VMConfig(name: "Media test", machineFamily: .powerMacG4, ramMB: 512))
        defer { server.stop() }
        let (htmlData, _) = try await URLSession.shared.data(from: server.url)
        let html = String(decoding: htmlData, as: UTF8.self)
        XCTAssertTrue(html.contains("id=\"media\""))
        let expression = try NSRegularExpression(pattern: "name=\"classicmac-action-token\" content=\"([^\"]+)\"")
        let match = try XCTUnwrap(expression.firstMatch(in: html, range: NSRange(html.startIndex..., in: html)))
        let token = String(html[try XCTUnwrap(Range(match.range(at: 1), in: html))])
        let origin = "http://127.0.0.1:\(server.url.port!)"
        for (candidateToken, candidateOrigin, expected) in [
            (nil, origin, 403), (token, nil, 403), ("wrong", origin, 403),
            (token, "https://example.com", 403), (token, "http://localhost:\(server.url.port!)", 403),
            (token, origin, 409)
        ] as [(String?, String?, Int)] {
            var request = URLRequest(url: server.url.appendingPathComponent("actions/media"))
            request.httpMethod = "POST"
            if let candidateToken { request.setValue(candidateToken, forHTTPHeaderField: "X-ClassicMac-Action") }
            if let candidateOrigin { request.setValue(candidateOrigin, forHTTPHeaderField: "Origin") }
            let (data, response) = try await URLSession.shared.data(for: request)
            XCTAssertEqual((response as? HTTPURLResponse)?.statusCode, expected)
            XCTAssertFalse(String(decoding: data, as: UTF8.self).contains("/Users/"))
        }
    }
}
