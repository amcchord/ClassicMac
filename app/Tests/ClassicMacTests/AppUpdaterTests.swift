import CryptoKit
import XCTest
@testable import ClassicMac

final class AppUpdaterTests: XCTestCase {
    private var directory: URL!
    private var suiteName: String!
    private var defaults: UserDefaults!

    override func setUpWithError() throws {
        directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        suiteName = "com.classicmac.tests.updater.\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)!
    }

    override func tearDownWithError() throws {
        AppUpdaterURLProtocol.handler = nil
        if let suiteName {
            defaults?.removePersistentDomain(forName: suiteName)
        }
        try? FileManager.default.removeItem(at: directory)
    }

    func testParseLatestReleaseFiltersDraftPrereleaseVersionsAndUntrustedAssets() throws {
        let validJSON: [String: Any] = [
            "tag_name": "v4.9.0",
            "name": "ClassicMac 4.9.0",
            "body": "Bug fixes and improvements.",
            "draft": false,
            "prerelease": false,
            "html_url": "https://github.com/amcchord/ClassicMac/releases/tag/v4.9.0",
            "published_at": "2026-11-25T10:00:00Z",
            "assets": [
                [
                    "name": "ClassicMac-4.9.0.zip",
                    "size": 50_000_000,
                    "browser_download_url": "https://github.com/amcchord/ClassicMac/releases/download/v4.9.0/ClassicMac-4.9.0.zip",
                ],
                [
                    "name": "SHA256SUMS",
                    "size": 256,
                    "browser_download_url": "https://github.com/amcchord/ClassicMac/releases/download/v4.9.0/SHA256SUMS",
                ],
            ],
        ]
        let validData = try JSONSerialization.data(withJSONObject: validJSON)
        let release = try XCTUnwrap(AppUpdateRelease.parseLatestRelease(validData, currentVersion: "4.8.1"))
        XCTAssertEqual(release.version, "4.9.0")
        XCTAssertEqual(release.tagName, "v4.9.0")
        XCTAssertEqual(release.zipFilename, "ClassicMac-4.9.0.zip")
        XCTAssertEqual(release.zipSize, 50_000_000)
        XCTAssertEqual(release.body, "Bug fixes and improvements.")

        // Equal or newer currentVersion -> nil.
        XCTAssertNil(AppUpdateRelease.parseLatestRelease(validData, currentVersion: "4.9.0"))
        XCTAssertNil(AppUpdateRelease.parseLatestRelease(validData, currentVersion: "5.0.0"))

        // Draft and prerelease -> nil.
        var draft = validJSON
        draft["draft"] = true
        XCTAssertNil(AppUpdateRelease.parseLatestRelease(try JSONSerialization.data(withJSONObject: draft), currentVersion: "4.8.1"))

        var prerelease = validJSON
        prerelease["prerelease"] = true
        XCTAssertNil(AppUpdateRelease.parseLatestRelease(try JSONSerialization.data(withJSONObject: prerelease), currentVersion: "4.8.1"))

        // Untrusted asset host -> nil.
        var badHost = validJSON
        badHost["assets"] = [
            [
                "name": "ClassicMac-4.9.0.zip",
                "size": 50_000_000,
                "browser_download_url": "https://evil.example.com/ClassicMac-4.9.0.zip",
            ],
            [
                "name": "SHA256SUMS",
                "size": 256,
                "browser_download_url": "https://github.com/amcchord/ClassicMac/releases/download/v4.9.0/SHA256SUMS",
            ],
        ]
        XCTAssertNil(AppUpdateRelease.parseLatestRelease(try JSONSerialization.data(withJSONObject: badHost), currentVersion: "4.8.1"))

        // Missing SHA256SUMS -> nil.
        var missingSums = validJSON
        missingSums["assets"] = [
            [
                "name": "ClassicMac-4.9.0.zip",
                "size": 50_000_000,
                "browser_download_url": "https://github.com/amcchord/ClassicMac/releases/download/v4.9.0/ClassicMac-4.9.0.zip",
            ],
        ]
        XCTAssertNil(AppUpdateRelease.parseLatestRelease(try JSONSerialization.data(withJSONObject: missingSums), currentVersion: "4.8.1"))
    }

    func testParseSHA256Sums() {
        let sum = String(repeating: "a", count: 64)
        let upper = String(repeating: "B", count: 64)
        let text = """
        # release checksums
        \(sum)  ClassicMac-4.9.0.zip
        \(upper) *./ClassicMac-4.9.0.dmg
        not-a-hash  ClassicMac-4.9.0.zip
        """
        XCTAssertEqual(AppUpdater.parseSHA256Sums(text, filename: "ClassicMac-4.9.0.zip"), sum)
        XCTAssertEqual(AppUpdater.parseSHA256Sums(text, filename: "ClassicMac-4.9.0.dmg"), String(repeating: "b", count: 64))
        XCTAssertNil(AppUpdater.parseSHA256Sums(text, filename: "Other.zip"))
    }

    func testAllowedHostsAndCanInstallInPlace() throws {
        XCTAssertTrue(AppUpdater.isAllowedURL(URL(string: "https://api.github.com/repos/amcchord/ClassicMac/releases/latest")!))
        XCTAssertTrue(AppUpdater.isAllowedURL(URL(string: "https://github.com/amcchord/ClassicMac/releases/download/v4.9.0/ClassicMac-4.9.0.zip")!))
        XCTAssertTrue(AppUpdater.isAllowedURL(URL(string: "https://objects.githubusercontent.com/some/path")!))
        XCTAssertTrue(AppUpdater.isAllowedURL(URL(string: "https://release-assets.githubusercontent.com/some/path")!))
        XCTAssertFalse(AppUpdater.isAllowedURL(URL(string: "http://github.com/amcchord/ClassicMac")!))
        XCTAssertFalse(AppUpdater.isAllowedURL(URL(string: "https://user:pass@github.com/amcchord/ClassicMac")!))
        XCTAssertFalse(AppUpdater.isAllowedURL(URL(string: "https://example.com/ClassicMac.zip")!))

        let writableApp = directory.appendingPathComponent("ClassicMac.app", isDirectory: true)
        try FileManager.default.createDirectory(at: writableApp, withIntermediateDirectories: true)
        XCTAssertTrue(AppUpdater.canInstallInPlace(bundleURL: writableApp))

        XCTAssertFalse(AppUpdater.canInstallInPlace(bundleURL: directory.appendingPathComponent("ClassicMac")))
        XCTAssertFalse(AppUpdater.canInstallInPlace(bundleURL: URL(fileURLWithPath: "/Volumes/ClassicMac/ClassicMac.app")))
        XCTAssertFalse(AppUpdater.canInstallInPlace(bundleURL: directory.appendingPathComponent(".build/debug/ClassicMac.app")))
    }

    func testVerifySHA256AndInfoPlist() throws {
        let payload = Data("classicmac-update-zip".utf8)
        let digest = SHA256.hash(data: payload).map { String(format: "%02x", $0) }.joined()
        let zipFile = directory.appendingPathComponent("ClassicMac-4.9.0.zip")
        try payload.write(to: zipFile)

        XCTAssertNoThrow(try AppUpdater.verifySHA256(file: zipFile, expectedHex: digest))
        XCTAssertThrowsError(try AppUpdater.verifySHA256(file: zipFile, expectedHex: String(repeating: "0", count: 64)))
        XCTAssertFalse(FileManager.default.fileExists(atPath: zipFile.path))

        let appDir = directory.appendingPathComponent("ClassicMac.app", isDirectory: true)
        let contents = appDir.appendingPathComponent("Contents", isDirectory: true)
        try FileManager.default.createDirectory(at: contents, withIntermediateDirectories: true)
        let plist: [String: Any] = [
            "CFBundleIdentifier": "com.classicmac.emulator",
            "CFBundleShortVersionString": "4.9.0",
        ]
        let plistData = try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
        try plistData.write(to: contents.appendingPathComponent("Info.plist"))

        XCTAssertNoThrow(try AppUpdater.verifyInfoPlist(in: appDir, expectedVersion: "4.9.0"))
        XCTAssertThrowsError(try AppUpdater.verifyInfoPlist(in: appDir, expectedVersion: "4.9.1"))

        let wrongID: [String: Any] = [
            "CFBundleIdentifier": "com.evil.emulator",
            "CFBundleShortVersionString": "4.9.0",
        ]
        try PropertyListSerialization.data(fromPropertyList: wrongID, format: .xml, options: 0)
            .write(to: contents.appendingPathComponent("Info.plist"))
        XCTAssertThrowsError(try AppUpdater.verifyInfoPlist(in: appDir, expectedVersion: "4.9.0"))
    }

    @MainActor
    func testPreferencesThrottlingSkipAndFeedCheck() async throws {
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [AppUpdaterURLProtocol.self]
        let feedURL = URL(string: "https://api.github.com/repos/amcchord/ClassicMac/releases/latest")!

        let updater = AppUpdater(
            userDefaults: defaults,
            currentVersion: "4.8.1",
            bundleURL: directory.appendingPathComponent("ClassicMac.app"),
            updatesDirectory: directory.appendingPathComponent("Updates"),
            sessionConfiguration: config,
            feedURL: feedURL
        )

        XCTAssertTrue(updater.automaticallyChecksForUpdates)
        XCTAssertTrue(updater.shouldCheckAutomatically(now: Date(), environment: [:]))
        XCTAssertFalse(updater.shouldCheckAutomatically(now: Date(), environment: ["CLASSICMAC_DISABLE_UPDATE_CHECK": "1"]))
        XCTAssertFalse(updater.shouldCheckAutomatically(now: Date(), environment: ["CLASSICMAC_UI_SNAPSHOT": "1"]))
        XCTAssertFalse(updater.shouldCheckAutomatically(now: Date(), environment: ["XCTestConfigurationFilePath": "/tmp/test.xctestconfiguration"]))

        let now = Date()
        updater.lastUpdateCheckDate = now.addingTimeInterval(-3_600)
        XCTAssertFalse(updater.shouldCheckAutomatically(now: now, environment: [:]))
        updater.lastUpdateCheckDate = now.addingTimeInterval(-86_401)
        XCTAssertTrue(updater.shouldCheckAutomatically(now: now, environment: [:]))

        let releaseJSON: [String: Any] = [
            "tag_name": "v4.9.0",
            "name": "ClassicMac 4.9.0",
            "body": "Notes",
            "draft": false,
            "prerelease": false,
            "assets": [
                [
                    "name": "ClassicMac-4.9.0.zip",
                    "size": 1_024,
                    "browser_download_url": "https://github.com/amcchord/ClassicMac/releases/download/v4.9.0/ClassicMac-4.9.0.zip",
                ],
                [
                    "name": "SHA256SUMS",
                    "size": 128,
                    "browser_download_url": "https://github.com/amcchord/ClassicMac/releases/download/v4.9.0/SHA256SUMS",
                ],
            ],
        ]
        let jsonData = try JSONSerialization.data(withJSONObject: releaseJSON)
        AppUpdaterURLProtocol.handler = { request, client, loader in
            XCTAssertEqual(request.value(forHTTPHeaderField: "Accept"), "application/vnd.github+json")
            client.urlProtocol(loader, didReceive: HTTPURLResponse(
                url: request.url!, statusCode: 200, httpVersion: "HTTP/1.1",
                headerFields: ["Content-Length": "\(jsonData.count)"]
            )!, cacheStoragePolicy: .notAllowed)
            client.urlProtocol(loader, didLoad: jsonData)
            client.urlProtocolDidFinishLoading(loader)
        }

        updater.checkForUpdates(interactive: true)
        while updater.isBusy { try await Task.sleep(nanoseconds: 20_000_000) }
        guard case .available(let found) = updater.state else {
            return XCTFail("Expected available release, got \(updater.state)")
        }
        XCTAssertEqual(found.version, "4.9.0")
        XCTAssertNotNil(updater.lastUpdateCheckDate)

        // Skipping suppresses background check notifications, while interactive check still shows it.
        updater.skipVersion(found)
        XCTAssertEqual(updater.state, .idle)
        XCTAssertEqual(updater.skippedUpdateVersion, "4.9.0")

        updater.checkForUpdates(interactive: false)
        while updater.isBusy { try await Task.sleep(nanoseconds: 20_000_000) }
        XCTAssertEqual(updater.state, .idle)

        updater.checkForUpdates(interactive: true)
        while updater.isBusy { try await Task.sleep(nanoseconds: 20_000_000) }
        guard case .available = updater.state else {
            return XCTFail("Interactive check should surface skipped version")
        }
    }
}

private final class AppUpdaterURLProtocol: URLProtocol {
    private final class State: @unchecked Sendable {
        let lock = NSLock()
        var handler: ((URLRequest, URLProtocolClient, URLProtocol) -> Void)?
    }
    private static let state = State()
    static var handler: ((URLRequest, URLProtocolClient, URLProtocol) -> Void)? {
        get { state.lock.lock(); defer { state.lock.unlock() }; return state.handler }
        set { state.lock.lock(); defer { state.lock.unlock() }; state.handler = newValue }
    }
    override class func canInit(with request: URLRequest) -> Bool {
        request.url?.host == "api.github.com"
    }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        guard let client, let handler = Self.handler else { return }
        handler(request, client, self)
    }
    override func stopLoading() {}
}
