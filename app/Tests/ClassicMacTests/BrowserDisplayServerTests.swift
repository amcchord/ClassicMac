import XCTest
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

    func testEndpointUsesPrivateUnixSocketAndLoopbackWebSocket() {
        let id = UUID()
        let endpoint = BrowserDisplayEndpoint(vmID: id, webSocketPort: 61_234)

        XCTAssertEqual(
            endpoint.vncSocketURL.path,
            "/tmp/ClassicMac/\(id.uuidString).vnc.sock"
        )
        XCTAssertEqual(
            endpoint.qemuDisplay,
            "vnc=unix:/tmp/ClassicMac/\(id.uuidString).vnc.sock," +
            "websocket=127.0.0.1:61234,share=force-shared"
        )
    }

    func testLoopbackServerPublishesViewerAndPerMachineConfiguration() async throws {
        let server = try BrowserDisplayServer.start(for: config())
        defer { server.stop() }

        XCTAssertEqual(server.url.host, "127.0.0.1")

        let (pageData, pageResponse) = try await URLSession.shared.data(from: server.url)
        XCTAssertEqual((pageResponse as? HTTPURLResponse)?.statusCode, 200)
        let page = String(decoding: pageData, as: UTF8.self)
        XCTAssertTrue(page.contains("Classic Mac display"))
        XCTAssertTrue(page.contains("Waiting for the Mac"))
        XCTAssertTrue(page.contains("may be shut down or restarting"))
        XCTAssertTrue(page.contains("reconnect automatically"))
        XCTAssertTrue(page.contains("content=\"Browser Test Mac\""))
        XCTAssertTrue(
            page.contains("content=\"ws://127.0.0.1:\(server.endpoint.webSocketPort)\"")
        )

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
        XCTAssertEqual(
            object["webSocketURL"] as? String,
            "ws://127.0.0.1:\(server.endpoint.webSocketPort)"
        )

        let traversalURL = URL(
            string: "%2e%2e/config.json",
            relativeTo: server.url
        )!
        let (_, traversalResponse) = try await URLSession.shared.data(from: traversalURL)
        XCTAssertEqual((traversalResponse as? HTTPURLResponse)?.statusCode, 404)
    }
}
