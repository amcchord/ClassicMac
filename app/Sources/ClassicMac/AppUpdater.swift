import AppKit
import CryptoKit
import Darwin
import Foundation

extension Notification.Name {
    static let classicMacCheckForUpdates = Notification.Name("com.classicmac.check-for-updates")
}

struct AppUpdateRelease: Equatable, Sendable {
    let version: String
    let tagName: String
    let name: String
    let body: String
    let htmlURL: URL?
    let zipURL: URL
    let zipSize: Int64
    let sha256SumsURL: URL
    let publishedAt: Date?

    var zipFilename: String { "ClassicMac-\(version).zip" }

    private struct GitHubRelease: Decodable {
        struct Asset: Decodable {
            let name: String
            let size: Int64?
            let browser_download_url: String
        }
        let tag_name: String
        let name: String?
        let body: String?
        let draft: Bool?
        let prerelease: Bool?
        let html_url: String?
        let published_at: String?
        let assets: [Asset]?
    }

    static func parseLatestRelease(_ data: Data, currentVersion: String) -> AppUpdateRelease? {
        guard data.count <= AppUpdater.maximumFeedBytes,
              let current = AppReleaseVersion(currentVersion),
              let decoded = try? JSONDecoder().decode(GitHubRelease.self, from: data),
              decoded.draft != true,
              decoded.prerelease != true else {
            return nil
        }

        let tag = decoded.tag_name.trimmingCharacters(in: .whitespacesAndNewlines)
        let stripped = (tag.hasPrefix("v") || tag.hasPrefix("V")) ? String(tag.dropFirst()) : tag
        guard let latest = AppReleaseVersion(stripped), latest > current else {
            return nil
        }

        let assets = decoded.assets ?? []
        let expectedZipName = "ClassicMac-\(stripped).zip"
        guard let zipAsset = assets.first(where: { $0.name == expectedZipName }),
              let sumsAsset = assets.first(where: { $0.name == "SHA256SUMS" }),
              let zipURL = URL(string: zipAsset.browser_download_url),
              let sumsURL = URL(string: sumsAsset.browser_download_url),
              AppUpdater.isAllowedURL(zipURL),
              AppUpdater.isAllowedURL(sumsURL) else {
            return nil
        }

        let zipSize = zipAsset.size ?? 0
        guard zipSize > 0, zipSize <= AppUpdater.maximumZipBytes else {
            return nil
        }

        let htmlURL: URL?
        if let rawHTML = decoded.html_url, let parsed = URL(string: rawHTML), AppUpdater.isAllowedURL(parsed) {
            htmlURL = parsed
        } else {
            htmlURL = nil
        }

        let publishedAt: Date?
        if let rawDate = decoded.published_at {
            let formatter = ISO8601DateFormatter()
            formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
            if let date = formatter.date(from: rawDate) {
                publishedAt = date
            } else {
                let fallback = ISO8601DateFormatter()
                fallback.formatOptions = [.withInternetDateTime]
                publishedAt = fallback.date(from: rawDate)
            }
        } else {
            publishedAt = nil
        }

        let releaseName = decoded.name?.trimmingCharacters(in: .whitespacesAndNewlines)
        let title = (releaseName?.isEmpty == false) ? releaseName! : "ClassicMac \(stripped)"
        let notes = decoded.body?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""

        return AppUpdateRelease(
            version: stripped,
            tagName: tag,
            name: title,
            body: notes,
            htmlURL: htmlURL,
            zipURL: zipURL,
            zipSize: zipSize,
            sha256SumsURL: sumsURL,
            publishedAt: publishedAt
        )
    }
}

enum AppUpdaterError: LocalizedError {
    case network(String)
    case integrity(String)
    case verification(String)
    case runningMachines
    case notInstallableInPlace
    case installFailed(String)

    var errorDescription: String? {
        switch self {
        case .network(let reason):
            return reason
        case .integrity(let reason):
            return reason
        case .verification(let reason):
            return reason
        case .runningMachines:
            return "Shut down all running Macs before installing the update."
        case .notInstallableInPlace:
            return "ClassicMac cannot update itself from this location. Move ClassicMac.app to Applications or download the update from GitHub."
        case .installFailed(let reason):
            return reason
        }
    }
}

@MainActor
final class AppUpdater: ObservableObject {
    static let shared = AppUpdater()

    nonisolated static let latestReleaseURL = URL(string: "https://api.github.com/repos/amcchord/ClassicMac/releases/latest")!
    nonisolated static let teamIdentifier = "7PTN7E8EDS"
    nonisolated static let expectedBundleIdentifier = "com.classicmac.emulator"
    nonisolated static let maximumFeedBytes: Int64 = 1_048_576
    nonisolated static let maximumSumsBytes: Int64 = 65_536
    nonisolated static let maximumZipBytes: Int64 = 512 * 1_048_576
    nonisolated static let checkInterval: TimeInterval = 24 * 60 * 60
    nonisolated static let allowedHosts: Set<String> = [
        "api.github.com",
        "github.com",
        "objects.githubusercontent.com",
        "release-assets.githubusercontent.com",
    ]

    enum DefaultsKey {
        static let automaticallyChecksForUpdates = "automaticallyChecksForUpdates"
        static let lastUpdateCheckDate = "lastUpdateCheckDate"
        static let skippedUpdateVersion = "skippedUpdateVersion"
    }

    enum State: Equatable {
        case idle
        case checking
        case upToDate(version: String)
        case available(AppUpdateRelease)
        case downloading(AppUpdateRelease, done: Int64, total: Int64, bytesPerSecond: Double?, etaSeconds: Double?)
        case verifying(AppUpdateRelease)
        case readyToInstall(AppUpdateRelease, appURL: URL, stagingURL: URL, zipURL: URL)
        case error(String)
    }

    @Published private(set) var state: State = .idle
    @Published var automaticallyChecksForUpdates: Bool {
        didSet { userDefaults.set(automaticallyChecksForUpdates, forKey: DefaultsKey.automaticallyChecksForUpdates) }
    }

    let currentVersion: String
    private let userDefaults: UserDefaults
    private let bundleURL: URL
    private let updatesDirectory: URL
    private let sessionConfiguration: URLSessionConfiguration
    private let feedURL: URL
    private let fileManager: FileManager
    private var activeTask: Task<Void, Never>?
    private var cancellation = MachineDownloadCancellation()
    private var transferMeter = TransferMeter()

    init(
        userDefaults: UserDefaults = .standard,
        currentVersion: String = MachineCatalog.currentAppVersion,
        bundleURL: URL = Bundle.main.bundleURL,
        updatesDirectory: URL? = nil,
        sessionConfiguration: URLSessionConfiguration = .ephemeral,
        feedURL: URL = AppUpdater.latestReleaseURL,
        fileManager: FileManager = .default
    ) {
        self.userDefaults = userDefaults
        self.currentVersion = currentVersion
        self.bundleURL = bundleURL
        self.updatesDirectory = updatesDirectory ?? AppPaths.supportDir.appendingPathComponent("Updates", isDirectory: true)
        self.sessionConfiguration = sessionConfiguration
        self.feedURL = feedURL
        self.fileManager = fileManager
        if let stored = userDefaults.object(forKey: DefaultsKey.automaticallyChecksForUpdates) as? Bool {
            self.automaticallyChecksForUpdates = stored
        } else {
            self.automaticallyChecksForUpdates = true
        }
    }

    var lastUpdateCheckDate: Date? {
        get { userDefaults.object(forKey: DefaultsKey.lastUpdateCheckDate) as? Date }
        set { userDefaults.set(newValue, forKey: DefaultsKey.lastUpdateCheckDate) }
    }

    var skippedUpdateVersion: String? {
        get { userDefaults.string(forKey: DefaultsKey.skippedUpdateVersion) }
        set { userDefaults.set(newValue, forKey: DefaultsKey.skippedUpdateVersion) }
    }

    var canInstallInPlace: Bool {
        Self.canInstallInPlace(bundleURL: bundleURL, fileManager: fileManager)
    }

    var isBusy: Bool {
        switch state {
        case .checking, .downloading, .verifying:
            return true
        case .idle, .upToDate, .available, .readyToInstall, .error:
            return false
        }
    }

    nonisolated static func canInstallInPlace(bundleURL: URL, fileManager: FileManager = .default) -> Bool {
        guard bundleURL.pathExtension == "app" else { return false }
        let path = bundleURL.standardizedFileURL.path
        if path.hasPrefix("/Volumes/") || path.contains("/.build/") {
            return false
        }
        if (try? bundleURL.resourceValues(forKeys: [.volumeIsReadOnlyKey]).volumeIsReadOnly) == true {
            return false
        }
        let parent = bundleURL.deletingLastPathComponent()
        return fileManager.isWritableFile(atPath: parent.path)
    }

    nonisolated static func isAllowedURL(_ url: URL) -> Bool {
        guard DownloadableMachine.secureURL(url),
              let host = url.host?.lowercased() else {
            return false
        }
        return allowedHosts.contains(host)
    }

    nonisolated static func parseSHA256Sums(_ text: String, filename: String) -> String? {
        for rawLine in text.split(whereSeparator: \.isNewline) {
            let line = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
            if line.isEmpty || line.hasPrefix("#") { continue }
            guard line.count > 65 else { continue }
            let splitIndex = line.index(line.startIndex, offsetBy: 64)
            let hash = String(line[..<splitIndex]).lowercased()
            guard hash.allSatisfy({ "0123456789abcdef".contains($0) }) else { continue }
            let rest = line[splitIndex...]
            guard let first = rest.first, first == " " || first == "\t" else { continue }
            var name = rest.trimmingCharacters(in: .whitespaces)
            if name.hasPrefix("*") {
                name = String(name.dropFirst())
            }
            if name.hasPrefix("./") {
                name = String(name.dropFirst(2))
            }
            if name == filename {
                return hash
            }
        }
        return nil
    }

    nonisolated static func shouldSkipAutomaticCheck(environment: [String: String] = ProcessInfo.processInfo.environment) -> Bool {
        if environment["CLASSICMAC_DISABLE_UPDATE_CHECK"] == "1" { return true }
        if let snap = environment["CLASSICMAC_UI_SNAPSHOT"], !snap.isEmpty { return true }
        if let xc = environment["XCTestConfigurationFilePath"], !xc.isEmpty { return true }
        return false
    }

    func shouldCheckAutomatically(now: Date = Date(),
                                  environment: [String: String] = ProcessInfo.processInfo.environment) -> Bool {
        guard automaticallyChecksForUpdates, !Self.shouldSkipAutomaticCheck(environment: environment) else {
            return false
        }
        guard let last = lastUpdateCheckDate else { return true }
        return now.timeIntervalSince(last) >= Self.checkInterval
    }

    func scheduleAutomaticCheckIfNeeded(now: Date = Date(),
                                        environment: [String: String] = ProcessInfo.processInfo.environment) {
        guard shouldCheckAutomatically(now: now, environment: environment), !isBusy else { return }
        checkForUpdates(interactive: false)
    }

    func checkForUpdates(interactive: Bool = true) {
        if isBusy { return }
        if case .readyToInstall = state, interactive { return }
        activeTask?.cancel()
        cancellation = MachineDownloadCancellation()
        state = .checking
        let feed = feedURL
        let config = sessionConfiguration
        let current = currentVersion
        activeTask = Task { [weak self] in
            guard let self else { return }
            do {
                let data = try await UpdateTransfer.fetchData(
                    url: feed,
                    accept: "application/vnd.github+json",
                    maximumBytes: Self.maximumFeedBytes,
                    requestTimeout: 15,
                    resourceTimeout: 20,
                    configuration: config
                )
                try Task.checkCancellation()
                lastUpdateCheckDate = Date()
                if let release = AppUpdateRelease.parseLatestRelease(data, currentVersion: current) {
                    if !interactive && skippedUpdateVersion == release.version {
                        state = .idle
                    } else {
                        state = .available(release)
                    }
                } else {
                    state = interactive ? .upToDate(version: current) : .idle
                }
            } catch is CancellationError {
                state = .idle
            } catch {
                if interactive {
                    state = .error(error.localizedDescription)
                } else {
                    state = .idle
                }
            }
            activeTask = nil
        }
    }

    func skipVersion(_ release: AppUpdateRelease) {
        skippedUpdateVersion = release.version
        state = .idle
    }

    func cancel() {
        cancellation.cancel()
        activeTask?.cancel()
        activeTask = nil
        transferMeter.reset()
        switch state {
        case .checking, .downloading, .verifying:
            state = .idle
        default:
            break
        }
    }

    func downloadAndVerify(_ release: AppUpdateRelease) {
        guard !isBusy else { return }
        guard canInstallInPlace else {
            state = .error(AppUpdaterError.notInstallableInPlace.localizedDescription)
            return
        }
        activeTask?.cancel()
        cancellation = MachineDownloadCancellation()
        let cancellation = cancellation
        let updatesDir = updatesDirectory
        let config = sessionConfiguration
        transferMeter.reset(totalBytes: release.zipSize)
        state = .downloading(release, done: 0, total: release.zipSize, bytesPerSecond: nil, etaSeconds: nil)

        activeTask = Task { [weak self] in
            guard let self else { return }
            do {
                try prepareUpdatesDirectory()
                let sumsData = try await UpdateTransfer.fetchData(
                    url: release.sha256SumsURL,
                    accept: "application/octet-stream",
                    maximumBytes: Self.maximumSumsBytes,
                    requestTimeout: 15,
                    resourceTimeout: 20,
                    configuration: config
                )
                try Task.checkCancellation()
                guard let sumsText = String(data: sumsData, encoding: .utf8),
                      let expectedSHA = Self.parseSHA256Sums(sumsText, filename: release.zipFilename) else {
                    throw AppUpdaterError.integrity("Could not find checksum for \(release.zipFilename) in SHA256SUMS.")
                }

                let zipURL = updatesDir.appendingPathComponent(release.zipFilename)
                let stagingURL = updatesDir.appendingPathComponent("staging-\(UUID().uuidString)", isDirectory: true)
                let reporter = UpdateProgressThrottle { [weak self] done in
                    Task { @MainActor in
                        guard let self else { return }
                        if case .downloading(let activeRelease, _, _, _, _) = self.state,
                           activeRelease == release {
                            self.transferMeter.record(done: done, total: release.zipSize)
                            self.state = .downloading(
                                release,
                                done: done,
                                total: release.zipSize,
                                bytesPerSecond: self.transferMeter.bytesPerSecond,
                                etaSeconds: self.transferMeter.etaSeconds
                            )
                        }
                    }
                }

                try await UpdateTransfer.downloadFile(
                    url: release.zipURL,
                    destination: zipURL,
                    expectedBytes: release.zipSize,
                    maximumBytes: Self.maximumZipBytes,
                    requestTimeout: 30,
                    resourceTimeout: 60 * 60,
                    configuration: config,
                    progress: { reporter.report($0) }
                )
                try Task.checkCancellation()

                transferMeter.reset()
                state = .verifying(release)

                let verifiedAppURL = try await Task.detached(priority: .utility) {
                    try Self.verifySHA256(file: zipURL, expectedHex: expectedSHA) {
                        cancellation.isCancelled
                    }
                    if cancellation.isCancelled { throw CancellationError() }
                    try FileManager.default.createDirectory(
                        at: stagingURL,
                        withIntermediateDirectories: true,
                        attributes: [.posixPermissions: 0o700]
                    )
                    do {
                        try Self.extractZip(zipURL, to: stagingURL)
                        if cancellation.isCancelled { throw CancellationError() }
                        let appURL = stagingURL.appendingPathComponent("ClassicMac.app", isDirectory: true)
                        try Self.verifyExtractedApp(at: appURL, expectedVersion: release.version)
                        return appURL
                    } catch {
                        try? FileManager.default.removeItem(at: stagingURL)
                        throw error
                    }
                }.value

                try Task.checkCancellation()
                state = .readyToInstall(release, appURL: verifiedAppURL, stagingURL: stagingURL, zipURL: zipURL)
            } catch is CancellationError {
                transferMeter.reset()
                state = .idle
            } catch {
                transferMeter.reset()
                state = .error(error.localizedDescription)
            }
            activeTask = nil
        }
    }

    func installPreparedUpdate() {
        guard case .readyToInstall(_, let appURL, let stagingURL, let zipURL) = state else { return }
        guard canInstallInPlace else {
            state = .error(AppUpdaterError.notInstallableInPlace.localizedDescription)
            return
        }
        guard QEMUManager.shared.runningIDs.isEmpty else {
            state = .error(AppUpdaterError.runningMachines.localizedDescription)
            return
        }
        do {
            try Self.spawnDetachedSwapHelper(
                targetAppURL: bundleURL,
                newAppURL: appURL,
                stagingURL: stagingURL,
                zipURL: zipURL
            )
            NSApp.terminate(nil)
        } catch {
            state = .error(AppUpdaterError.installFailed("Could not start update installer: \(error.localizedDescription)").localizedDescription)
        }
    }

    private func prepareUpdatesDirectory() throws {
        try fileManager.createDirectory(
            at: updatesDirectory,
            withIntermediateDirectories: true,
            attributes: [.posixPermissions: 0o700]
        )
        let values = try updatesDirectory.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard values.isDirectory == true, values.isSymbolicLink != true else {
            throw AppUpdaterError.installFailed("The Updates folder is unavailable.")
        }
    }

    nonisolated static func verifySHA256(file: URL, expectedHex: String, cancelled: () -> Bool = { false }) throws {
        let values = try file.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
        guard values.isRegularFile == true, values.isSymbolicLink != true else {
            throw AppUpdaterError.integrity("The downloaded update is not a regular file.")
        }
        let input = try FileHandle(forReadingFrom: file)
        defer { try? input.close() }
        var hasher = SHA256()
        while let chunk = try input.read(upToCount: 1_048_576), !chunk.isEmpty {
            if cancelled() { throw CancellationError() }
            hasher.update(data: chunk)
        }
        let digest = hasher.finalize().map { String(format: "%02x", $0) }.joined()
        guard digest == expectedHex.lowercased() else {
            try? FileManager.default.removeItem(at: file)
            throw AppUpdaterError.integrity("The update archive failed its SHA-256 verification.")
        }
    }

    nonisolated static func extractZip(_ zipURL: URL, to stagingURL: URL) throws {
        let result = try runProcess(
            executable: "/usr/bin/ditto",
            arguments: ["-x", "-k", zipURL.path, stagingURL.path]
        )
        guard result.status == 0 else {
            throw AppUpdaterError.verification("Could not unpack the update archive.")
        }
    }

    nonisolated static func verifyExtractedApp(at appURL: URL, expectedVersion: String) throws {
        let values = try? appURL.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard values?.isDirectory == true, values?.isSymbolicLink != true else {
            throw AppUpdaterError.verification("The update archive did not contain ClassicMac.app.")
        }

        let verifyResult = try runProcess(
            executable: "/usr/bin/codesign",
            arguments: ["--verify", "--deep", "--strict", appURL.path]
        )
        guard verifyResult.status == 0 else {
            throw AppUpdaterError.verification("The update bundle failed its code signature verification.")
        }

        let detailsResult = try runProcess(
            executable: "/usr/bin/codesign",
            arguments: ["-dvvv", appURL.path]
        )
        let signatureText = detailsResult.stdout + "\n" + detailsResult.stderr
        guard detailsResult.status == 0,
              signatureText.contains("TeamIdentifier=\(teamIdentifier)"),
              signatureText.contains("Authority=Developer ID Application:") else {
            throw AppUpdaterError.verification("The update bundle's Developer ID signature could not be verified.")
        }

        try verifyInfoPlist(in: appURL, expectedVersion: expectedVersion)
    }

    nonisolated static func verifyInfoPlist(in appURL: URL, expectedVersion: String) throws {
        let plistURL = appURL.appendingPathComponent("Contents/Info.plist")
        guard let data = try? Data(contentsOf: plistURL),
              let plist = try? PropertyListSerialization.propertyList(from: data, options: [], format: nil) as? [String: Any],
              let bundleID = plist["CFBundleIdentifier"] as? String,
              let shortVersion = plist["CFBundleShortVersionString"] as? String,
              bundleID == expectedBundleIdentifier,
              shortVersion == expectedVersion else {
            throw AppUpdaterError.verification("The update bundle's Info.plist does not match ClassicMac \(expectedVersion).")
        }
    }

    nonisolated static func spawnDetachedSwapHelper(
        targetAppURL: URL,
        newAppURL: URL,
        stagingURL: URL,
        zipURL: URL,
        pid: Int32 = ProcessInfo.processInfo.processIdentifier
    ) throws {
        let backupURL = targetAppURL.deletingLastPathComponent()
            .appendingPathComponent(".\(targetAppURL.lastPathComponent).previous.\(UUID().uuidString)")
        let script = """
        PID="$1"
        TARGET="$2"
        NEW_APP="$3"
        BACKUP="$4"
        STAGING="$5"
        ZIP_FILE="$6"
        while kill -0 "$PID" 2>/dev/null; do
            sleep 0.2
        done
        rm -rf "$BACKUP"
        if [ -e "$TARGET" ]; then
            mv "$TARGET" "$BACKUP" || exit 1
        fi
        if mv "$NEW_APP" "$TARGET"; then
            rm -rf "$BACKUP" "$STAGING" "$ZIP_FILE"
            open -n "$TARGET"
        else
            if [ -e "$BACKUP" ] && [ ! -e "$TARGET" ]; then
                mv "$BACKUP" "$TARGET"
            fi
            exit 1
        fi
        """
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sh")
        process.arguments = [
            "-c", script, "classicmac-updater",
            String(pid),
            targetAppURL.path,
            newAppURL.path,
            backupURL.path,
            stagingURL.path,
            zipURL.path,
        ]
        process.standardInput = FileHandle.nullDevice
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        try process.run()
    }

    private nonisolated static func runProcess(executable: String, arguments: [String]) throws -> (status: Int32, stdout: String, stderr: String) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.environment = ["PATH": "/usr/bin:/bin"]
        let outPipe = Pipe()
        let errPipe = Pipe()
        process.standardOutput = outPipe
        process.standardError = errPipe
        try process.run()
        let outData = outPipe.fileHandleForReading.readDataToEndOfFile()
        let errData = errPipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        return (
            process.terminationStatus,
            String(decoding: outData, as: UTF8.self),
            String(decoding: errData, as: UTF8.self)
        )
    }
}

private final class UpdateProgressThrottle: @unchecked Sendable {
    private let lock = NSLock()
    private var lastUpdate = Date.distantPast
    private let update: (Int64) -> Void

    init(update: @escaping (Int64) -> Void) {
        self.update = update
    }

    func report(_ bytes: Int64) {
        lock.lock()
        let now = Date()
        let shouldUpdate = now.timeIntervalSince(lastUpdate) >= 0.1
        if shouldUpdate { lastUpdate = now }
        lock.unlock()
        if shouldUpdate { update(bytes) }
    }
}

private final class UpdateTransfer: NSObject, URLSessionDataDelegate, @unchecked Sendable {
    private enum Destination {
        case memory
        case file(URL)
    }

    private let source: URL
    private let destination: Destination
    private let accept: String
    private let expectedBytes: Int64?
    private let maximumBytes: Int64
    private let requestTimeout: TimeInterval
    private let resourceTimeout: TimeInterval
    private let configuration: URLSessionConfiguration
    private let progress: (Int64) -> Void
    private let lock = NSLock()
    private var continuation: CheckedContinuation<Data, Error>?
    private var session: URLSession?
    private var task: URLSessionDataTask?
    private var cancelled = false

    private var buffer = Data()
    private var output: FileHandle?
    private var received: Int64 = 0
    private var acceptedResponse = false
    private var failure: Error?

    static func fetchData(url: URL, accept: String, maximumBytes: Int64,
                          requestTimeout: TimeInterval, resourceTimeout: TimeInterval,
                          configuration: URLSessionConfiguration) async throws -> Data {
        let transfer = UpdateTransfer(
            source: url, destination: .memory, accept: accept,
            expectedBytes: nil, maximumBytes: maximumBytes,
            requestTimeout: requestTimeout, resourceTimeout: resourceTimeout,
            configuration: configuration, progress: { _ in }
        )
        return try await transfer.run()
    }

    static func downloadFile(url: URL, destination: URL, expectedBytes: Int64, maximumBytes: Int64,
                             requestTimeout: TimeInterval, resourceTimeout: TimeInterval,
                             configuration: URLSessionConfiguration,
                             progress: @escaping (Int64) -> Void) async throws {
        let transfer = UpdateTransfer(
            source: url, destination: .file(destination), accept: "application/octet-stream",
            expectedBytes: expectedBytes, maximumBytes: maximumBytes,
            requestTimeout: requestTimeout, resourceTimeout: resourceTimeout,
            configuration: configuration, progress: progress
        )
        _ = try await transfer.run()
    }

    private init(source: URL, destination: Destination, accept: String,
                 expectedBytes: Int64?, maximumBytes: Int64,
                 requestTimeout: TimeInterval, resourceTimeout: TimeInterval,
                 configuration: URLSessionConfiguration,
                 progress: @escaping (Int64) -> Void) {
        self.source = source
        self.destination = destination
        self.accept = accept
        self.expectedBytes = expectedBytes
        self.maximumBytes = maximumBytes
        self.requestTimeout = requestTimeout
        self.resourceTimeout = resourceTimeout
        self.configuration = (configuration.copy() as? URLSessionConfiguration) ?? .ephemeral
        self.progress = progress
    }

    func run() async throws -> Data {
        guard AppUpdater.isAllowedURL(source) else {
            throw AppUpdaterError.network("The update address is not allowed.")
        }
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                lock.lock()
                if cancelled {
                    lock.unlock()
                    continuation.resume(throwing: CancellationError())
                    return
                }
                self.continuation = continuation
                let queue = OperationQueue()
                queue.maxConcurrentOperationCount = 1
                configuration.urlCache = nil
                configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
                configuration.timeoutIntervalForRequest = requestTimeout
                configuration.timeoutIntervalForResource = resourceTimeout
                let session = URLSession(configuration: configuration, delegate: self, delegateQueue: queue)
                self.session = session
                var request = URLRequest(url: source)
                request.setValue(accept, forHTTPHeaderField: "Accept")
                let task = session.dataTask(with: request)
                self.task = task
                lock.unlock()
                task.resume()
            }
        } onCancel: {
            self.cancel()
        }
    }

    func cancel() {
        lock.lock()
        cancelled = true
        let task = task
        lock.unlock()
        task?.cancel()
    }

    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void) {
        guard let url = request.url, AppUpdater.isAllowedURL(url) else {
            failure = AppUpdaterError.network("The update redirected to an untrusted address.")
            completionHandler(nil)
            return
        }
        completionHandler(request)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask,
                    didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        do {
            guard let response = response as? HTTPURLResponse else {
                throw AppUpdaterError.network("The update server returned an unreadable response.")
            }
            guard (200..<300).contains(response.statusCode) else {
                throw AppUpdaterError.network("The update server returned HTTP \(response.statusCode).")
            }
            if response.expectedContentLength >= 0 {
                guard response.expectedContentLength <= maximumBytes else {
                    throw AppUpdaterError.network("The update response exceeded the maximum allowed size.")
                }
                if let expectedBytes, response.expectedContentLength != expectedBytes {
                    throw AppUpdaterError.network("The update file size did not match the release manifest.")
                }
            }
            if case .file(let url) = destination {
                if let values = try? url.resourceValues(forKeys: [.isSymbolicLinkKey]),
                   values.isSymbolicLink == true {
                    throw AppUpdaterError.installFailed("The update file destination is not valid.")
                }
                let fd = open(url.path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0o600)
                guard fd >= 0 else {
                    throw AppUpdaterError.installFailed("The Updates folder is not writable.")
                }
                var info = stat()
                guard fstat(fd, &info) == 0, info.st_mode & S_IFMT == S_IFREG else {
                    close(fd)
                    throw AppUpdaterError.installFailed("The update file destination is not a regular file.")
                }
                output = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
            }
            received = 0
            acceptedResponse = true
            progress(0)
            completionHandler(.allow)
        } catch {
            failure = error
            completionHandler(.cancel)
        }
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        guard failure == nil, acceptedResponse else { return }
        do {
            guard Int64(data.count) <= maximumBytes - received else {
                throw AppUpdaterError.network("The update exceeded the maximum allowed size.")
            }
            switch destination {
            case .memory:
                buffer.append(data)
            case .file:
                try output?.write(contentsOf: data)
            }
            received += Int64(data.count)
            progress(received)
        } catch {
            failure = error
            dataTask.cancel()
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        do { try output?.close() } catch { failure = failure ?? error }
        output = nil
        lock.lock()
        let continuation = continuation
        self.continuation = nil
        let wasCancelled = cancelled
        self.task = nil
        self.session = nil
        lock.unlock()
        session.finishTasksAndInvalidate()
        guard let continuation else { return }
        if wasCancelled {
            continuation.resume(throwing: CancellationError())
        } else if let failure {
            continuation.resume(throwing: failure)
        } else if let error {
            continuation.resume(throwing: error)
        } else if !acceptedResponse || expectedBytes.map({ received != $0 }) == true {
            continuation.resume(throwing: AppUpdaterError.network("The update download ended before finishing."))
        } else {
            continuation.resume(returning: buffer)
        }
    }
}
