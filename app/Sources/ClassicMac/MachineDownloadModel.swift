import SwiftUI
import AppKit

@MainActor
final class MachineDownloadModel: ObservableObject {
    enum Phase: Equatable {
        case idle, loadingCatalog, downloading, verifying, installing, paused, complete
    }
    @Published private(set) var machines: [DownloadableMachine] = []
    @Published private(set) var phase: Phase = .idle
    @Published private(set) var completedBytes: Int64 = 0
    @Published private(set) var totalBytes: Int64 = 0
    @Published private(set) var bytesPerSecond: Double?
    @Published private(set) var etaSeconds: Double?
    @Published private(set) var errorMessage: String?
    @Published private(set) var partialCatalogWarning: String?
    @Published private(set) var isCancelling = false

    private let catalogURLs: [URL]
    private let cacheDirectory: URL
    private let transferConfiguration: URLSessionConfiguration
    private var transferMeter = TransferMeter()
    private var operation: Task<Void, Never>?
    private var cancellation = MachineDownloadCancellation()
    private var operationID = UUID()

    init(catalogURL: URL? = nil, catalogURLs: [URL]? = nil, cacheDirectory: URL? = nil,
         transferConfiguration: URLSessionConfiguration = .ephemeral) {
        self.catalogURLs = catalogURLs ?? catalogURL.map { [$0] } ?? [MachineCatalog.defaultURL, MachineCatalog.retroBootURL]
        self.cacheDirectory = cacheDirectory ?? AppPaths.supportDir.appendingPathComponent("Downloads", isDirectory: true)
        self.transferConfiguration = transferConfiguration
    }

    var isBusy: Bool {
        [.loadingCatalog, .downloading, .verifying, .installing].contains(phase)
    }

    var canGoBack: Bool { phase == .idle || phase == .paused }

    var activityLabel: String {
        if isCancelling { return "Stopping…" }
        switch phase {
        case .loadingCatalog: return "Finding ready-to-run Macs…"
        case .downloading: return "Downloading your Mac…"
        case .verifying: return "Checking the download…"
        case .installing: return "Preparing your Mac…"
        case .paused: return "Paused. Your download is saved."
        case .complete: return "Your Mac is ready."
        case .idle: return ""
        }
    }

    func loadCatalog() {
        guard !isBusy else { return }
        phase = .loadingCatalog
        errorMessage = nil
        partialCatalogWarning = nil
        operation = Task {
            do {
                try prepareCache()
                var results: [MachineCatalog] = []
                var firstError: Error?
                var failureCount = 0
                for url in catalogURLs {
                    do {
                        results.append(try await fetchCatalog(url))
                    } catch is CancellationError {
                        throw CancellationError()
                    } catch {
                        failureCount += 1
                        if firstError == nil { firstError = error }
                    }
                }
                try Task.checkCancellation()
                if results.isEmpty, let firstError { throw firstError }
                if failureCount > 0 {
                    partialCatalogWarning = "Some catalog editions could not be loaded. Refresh to try again."
                }
                var ids = Set<String>()
                machines = results.flatMap(\.machines).filter { ids.insert($0.id).inserted }
            } catch is CancellationError {
            } catch {
                errorMessage = error.localizedDescription
            }
            phase = .idle
            isCancelling = false
            operation = nil
        }
    }

    private func fetchCatalog(_ url: URL) async throws -> MachineCatalog {
        let file = cacheDirectory.appendingPathComponent("catalog-\(UUID().uuidString).json")
        defer { try? FileManager.default.removeItem(at: file) }
        let transfer = MachineDownloadTransfer(
            source: url, destination: file, expectedBytes: nil,
            maximumBytes: MachineCatalog.maximumCatalogBytes,
            configuration: transferConfiguration,
            requestTimeout: 15,
            resourceTimeout: 30
        )
        _ = try await transfer.run()
        return try MachineCatalog.decode(Data(contentsOf: file))
    }

    private func savedArchiveBytes(_ machine: DownloadableMachine) -> Int64 {
        let url = partialURL(machine)
        guard let values = try? url.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey]),
              values.isRegularFile == true, values.isSymbolicLink != true,
              let size = values.fileSize else {
            return 0
        }
        return Int64(size)
    }

    func hasPartialDownload(_ machine: DownloadableMachine) -> Bool {
        let saved = savedArchiveBytes(machine)
        return saved > 0 && saved < machine.archiveBytes
    }

    func hasCompletedCache(_ machine: DownloadableMachine) -> Bool {
        let saved = savedArchiveBytes(machine)
        return machine.archiveBytes > 0 && saved == machine.archiveBytes
    }

    func totalRequiredFreeBytes(for machine: DownloadableMachine, saveFolder: URL, supportsSparse: Bool) -> Int64 {
        let saved = savedArchiveBytes(machine)
        let remaining = saved <= machine.archiveBytes ? machine.archiveBytes - saved : machine.archiveBytes
        let storage = MachineTemplateStoragePlan(machine: machine, supportsSparseFiles: supportsSparse)
        let cacheVolume = Self.volumeURL(for: cacheDirectory)
        let targetVolume = Self.volumeURL(for: saveFolder)
        let sameVolume = cacheVolume != nil && cacheVolume == targetVolume
        return storage.requiredBytes + MachineTemplateInstaller.spaceReserve + (sameVolume ? remaining : 0)
    }

    func discardDownload(_ machine: DownloadableMachine) {
        guard !isBusy else { return }
        do {
            try machine.validate()
            try prepareCache()
            let lease = try MachineDownloadLease(directory: cacheDirectory, digest: machine.sha256)
            defer { withExtendedLifetime(lease) {} }
            let file = partialURL(machine)
            if FileManager.default.fileExists(atPath: file.path) { try FileManager.default.removeItem(at: file) }
            errorMessage = nil
            phase = .idle
            completedBytes = 0
        } catch { errorMessage = error.localizedDescription }
    }

    func install(_ machine: DownloadableMachine, name: String, directory: URL,
                 targetFamily: MachineFamily? = nil,
                 onInstall: @escaping (URL) -> Void) {
        guard !isBusy else { return }
        errorMessage = nil
        cancellation = MachineDownloadCancellation()
        let cancellation = cancellation
        let id = UUID()
        operationID = id
        phase = .downloading
        totalBytes = machine.archiveBytes
        completedBytes = 0
        resetTransferMeter(totalBytes: machine.archiveBytes)
        operation = Task {
            do {
                try machine.validate()
                try machine.checkCompatibility()
                _ = try MachineTemplateInstaller.normalizedName(name)
                try prepareCache()
                let lease = try MachineDownloadLease(directory: cacheDirectory, digest: machine.sha256)
                defer { withExtendedLifetime(lease) {} }
                let file = partialURL(machine)
                let savedBytes = savedArchiveBytes(machine)
                let remaining = savedBytes <= machine.archiveBytes ? machine.archiveBytes - savedBytes : machine.archiveBytes
                // The cache and machine may share a volume. Checking the
                // combined requirement in that case prevents starting a large
                // download that leaves no room for extraction.
                let cacheVolume = Self.volumeURL(for: cacheDirectory)
                let targetVolume = Self.volumeURL(for: directory)
                let storage = MachineTemplateInstaller.storagePlan(for: machine, at: directory)
                let cacheNeeded = remaining + MachineTemplateInstaller.spaceReserve +
                    (cacheVolume != nil && cacheVolume == targetVolume ? storage.requiredBytes : 0)
                try MachineTemplateInstaller.checkSpace(at: cacheDirectory, requiredBytes: cacheNeeded)
                try MachineTemplateInstaller.checkSpace(at: directory,
                    requiredBytes: storage.requiredBytes + MachineTemplateInstaller.spaceReserve)
                if savedBytes != machine.archiveBytes {
                    let reporter = reporter(for: id, phase: .downloading)
                    let transfer = MachineDownloadTransfer(
                        source: machine.archiveURL, destination: file,
                        expectedBytes: machine.archiveBytes, maximumBytes: machine.archiveBytes,
                        configuration: transferConfiguration,
                        progress: { reporter.report($0) }
                    )
                    _ = try await transfer.run()
                }
                try Task.checkCancellation()
                phase = .verifying
                completedBytes = 0
                totalBytes = machine.archiveBytes
                resetTransferMeter(totalBytes: machine.archiveBytes)
                let verificationReporter = reporter(for: id, phase: .verifying)
                do {
                    try await Task.detached(priority: .utility) {
                        try MachineDownloadTransfer.verify(file, machine: machine,
                            cancelled: { cancellation.isCancelled },
                            progress: { verificationReporter.report($0) })
                    }.value
                } catch MachineDownloadError.integrity {
                    // A complete but corrupt archive must not be retried as a
                    // cache hit forever. The next attempt starts from zero.
                    try? FileManager.default.removeItem(at: file)
                    throw MachineDownloadError.integrity
                }
                try Task.checkCancellation()
                phase = .installing
                completedBytes = 0
                totalBytes = machine.installedBytes
                resetTransferMeter(totalBytes: machine.installedBytes)
                let installReporter = reporter(for: id, phase: .installing)
                let destination = try await Task.detached(priority: .utility) {
                    try MachineTemplateInstaller.installVerifiedArchive(
                        file, machine: machine, name: name, in: directory,
                        targetFamily: targetFamily,
                        cancelled: { cancellation.isCancelled },
                        progress: { installReporter.report($0) }
                    )
                }.value
                // Once the atomic move succeeds, report the installed Mac even
                // if cancellation arrived at the end of that operation.
                phase = .complete
                resetTransferMeter()
                try? FileManager.default.removeItem(at: file)
                onInstall(destination)
            } catch is CancellationError {
                resetTransferMeter()
                if phase == .downloading, savedArchiveBytes(machine) > 0 {
                    phase = .paused
                } else {
                    phase = .idle
                }
            } catch {
                resetTransferMeter()
                errorMessage = error.localizedDescription
                phase = .idle
            }
            isCancelling = false
            operation = nil
        }
    }

    private static func volumeURL(for url: URL) -> URL? {
        var current = url.standardizedFileURL
        while true {
            if let volume = try? current.resourceValues(forKeys: [.volumeURLKey]).volume {
                return volume
            }
            let parent = current.deletingLastPathComponent()
            if parent.path == current.path { return nil }
            current = parent
        }
    }

    func cancel() {
        guard isBusy else { return }
        isCancelling = true
        cancellation.cancel()
        operation?.cancel()
    }

    private func prepareCache() throws {
        try FileManager.default.createDirectory(at: cacheDirectory, withIntermediateDirectories: true,
                                                 attributes: [.posixPermissions: 0o700])
        let values = try cacheDirectory.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard values.isDirectory == true, values.isSymbolicLink != true else {
            throw MachineDownloadError.network("The download folder is unavailable.")
        }
    }

    private func partialURL(_ machine: DownloadableMachine) -> URL {
        cacheDirectory.appendingPathComponent(machine.sha256 + ".tar.gz.part")
    }

    private func resetTransferMeter(totalBytes: Int64 = 0) {
        transferMeter.reset(totalBytes: totalBytes)
        bytesPerSecond = nil
        etaSeconds = nil
    }

    private func reporter(for id: UUID, phase: Phase) -> MachineDownloadProgress {
        MachineDownloadProgress { [weak self] bytes in
            Task { @MainActor in
                guard let self, self.operationID == id, self.phase == phase else { return }
                self.completedBytes = bytes
                self.transferMeter.record(done: bytes, total: self.totalBytes)
                self.bytesPerSecond = self.transferMeter.bytesPerSecond
                self.etaSeconds = self.transferMeter.etaSeconds
            }
        }
    }
}

// Avoid creating thousands of queued UI updates during a fast download or a
// multi-gigabyte disk extraction. Each phase gets an independent reporter.
private final class MachineDownloadProgress: @unchecked Sendable {
    private let lock = NSLock()
    private var lastUpdate = Date.distantPast
    private let update: (Int64) -> Void
    init(update: @escaping (Int64) -> Void) { self.update = update }
    func report(_ bytes: Int64) {
        lock.lock()
        let now = Date()
        let shouldUpdate = now.timeIntervalSince(lastUpdate) >= 0.1
        if shouldUpdate { lastUpdate = now }
        lock.unlock()
        if shouldUpdate { update(bytes) }
    }
}
