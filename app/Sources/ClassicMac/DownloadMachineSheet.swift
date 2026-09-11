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
    @Published private(set) var errorMessage: String?
    @Published private(set) var isCancelling = false

    private let catalogURL: URL
    private let cacheDirectory: URL
    private var operation: Task<Void, Never>?
    private var cancellation = MachineDownloadCancellation()
    private var operationID = UUID()

    init(catalogURL: URL = MachineCatalog.defaultURL, cacheDirectory: URL? = nil) {
        self.catalogURL = catalogURL
        self.cacheDirectory = cacheDirectory ?? AppPaths.supportDir.appendingPathComponent("Downloads", isDirectory: true)
    }

    var isBusy: Bool {
        [.loadingCatalog, .downloading, .verifying, .installing].contains(phase)
    }

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
        operation = Task {
            let file = cacheDirectory.appendingPathComponent("catalog-\(UUID().uuidString).json")
            defer { try? FileManager.default.removeItem(at: file) }
            do {
                try prepareCache()
                let transfer = MachineDownloadTransfer(
                    source: catalogURL, destination: file, expectedBytes: nil,
                    maximumBytes: MachineCatalog.maximumCatalogBytes
                )
                _ = try await transfer.run()
                machines = try MachineCatalog.decode(Data(contentsOf: file)).machines
                phase = .idle
            } catch is CancellationError {
                phase = .idle
            } catch {
                errorMessage = error.localizedDescription
                phase = .idle
            }
            isCancelling = false
            operation = nil
        }
    }

    func hasPartialDownload(_ machine: DownloadableMachine) -> Bool {
        FileManager.default.fileExists(atPath: partialURL(machine).path)
    }

    func discardDownload(_ machine: DownloadableMachine) {
        guard !isBusy else { return }
        do {
            try machine.validate()
            try prepareCache()
            let lease = try MachineDownloadLease(directory: cacheDirectory, digest: machine.sha256)
            defer { withExtendedLifetime(lease) {} }
            if hasPartialDownload(machine) { try FileManager.default.removeItem(at: partialURL(machine)) }
            errorMessage = nil
            phase = .idle
            completedBytes = 0
        } catch { errorMessage = error.localizedDescription }
    }

    func install(_ machine: DownloadableMachine, name: String, directory: URL,
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
        operation = Task {
            do {
                try machine.validate()
                try machine.checkCompatibility()
                _ = try MachineTemplateInstaller.normalizedName(name)
                try prepareCache()
                let lease = try MachineDownloadLease(directory: cacheDirectory, digest: machine.sha256)
                defer { withExtendedLifetime(lease) {} }
                let file = partialURL(machine)
                let savedBytes = Int64((try? file.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0)
                let remaining = max(0, machine.archiveBytes - savedBytes)
                // The cache and machine may share a volume. Checking the
                // combined requirement in that case prevents starting a large
                // download that leaves no room for extraction.
                let cacheVolume = try cacheDirectory.resourceValues(forKeys: [.volumeURLKey]).volume
                let targetVolume = try directory.resourceValues(forKeys: [.volumeURLKey]).volume
                let cacheNeeded = remaining + MachineTemplateInstaller.spaceReserve +
                    (cacheVolume == targetVolume ? machine.installedBytes : 0)
                try MachineTemplateInstaller.checkSpace(at: cacheDirectory, requiredBytes: cacheNeeded)
                try MachineTemplateInstaller.checkSpace(at: directory,
                    requiredBytes: machine.installedBytes + MachineTemplateInstaller.spaceReserve)
                if savedBytes != machine.archiveBytes {
                    let reporter = reporter(for: id, phase: .downloading)
                    let transfer = MachineDownloadTransfer(
                        source: machine.archiveURL, destination: file,
                        expectedBytes: machine.archiveBytes, maximumBytes: machine.archiveBytes,
                        progress: { reporter.report($0) }
                    )
                    _ = try await transfer.run()
                }
                try Task.checkCancellation()
                phase = .verifying
                completedBytes = 0
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
                let installReporter = reporter(for: id, phase: .installing)
                let destination = try await Task.detached(priority: .utility) {
                    try MachineTemplateInstaller.installVerifiedArchive(
                        file, machine: machine, name: name, in: directory,
                        cancelled: { cancellation.isCancelled },
                        progress: { installReporter.report($0) }
                    )
                }.value
                // Once the atomic move succeeds, report the installed Mac even
                // if cancellation arrived at the end of that operation.
                phase = .complete
                try? FileManager.default.removeItem(at: file)
                onInstall(destination)
            } catch is CancellationError {
                phase = .paused
            } catch {
                errorMessage = error.localizedDescription
                phase = .idle
            }
            isCancelling = false
            operation = nil
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

    private func reporter(for id: UUID, phase: Phase) -> MachineDownloadProgress {
        MachineDownloadProgress { [weak self] bytes in
            Task { @MainActor in
                guard let self, self.operationID == id, self.phase == phase else { return }
                self.completedBytes = bytes
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

/// Integrate with `.sheet`: after installation, register the returned package
/// using `store.openBundle(at: url, autostart: false)`. The sheet dismisses itself.
struct DownloadMachineSheet: View {
    var onInstall: (URL) -> Void
    @Environment(\.dismiss) private var dismiss
    @StateObject private var model: MachineDownloadModel
    @State private var selectedID: String?
    @State private var name = "Mac OS 9"
    @State private var saveFolder = AppPaths.defaultLibraryDir

    init(catalogURL: URL = MachineCatalog.defaultURL, onInstall: @escaping (URL) -> Void) {
        self.onInstall = onInstall
        _model = StateObject(wrappedValue: MachineDownloadModel(catalogURL: catalogURL))
    }

    private var selected: DownloadableMachine? {
        model.machines.first { $0.id == selectedID } ?? model.machines.first
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 14) {
                Image(systemName: "arrow.down.circle.fill")
                    .font(.system(size: 36)).foregroundStyle(.tint)
                VStack(alignment: .leading, spacing: 4) {
                    Text("Download a Mac").font(.title2.bold())
                    Text("A ready-to-run Mac with GXMetal already installed.")
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }
            .padding(24)
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    if model.phase == .loadingCatalog {
                        ProgressView(model.activityLabel).frame(maxWidth: .infinity).padding(30)
                    } else if let machine = selected {
                        templateForm(machine)
                    } else if model.errorMessage == nil {
                        ContentUnavailableView("No Macs Available Yet", systemImage: "desktopcomputer",
                            description: Text("Check again soon, or create a Mac using your own installation disc."))
                    }
                    if model.isBusy && model.phase != .loadingCatalog || model.phase == .paused {
                        VStack(alignment: .leading, spacing: 8) {
                            Text(model.activityLabel).font(.headline)
                            if model.isBusy {
                                ProgressView(value: Double(model.completedBytes), total: Double(max(1, model.totalBytes)))
                                Text("\(formatted(model.completedBytes)) of \(formatted(model.totalBytes))")
                                    .font(.caption).foregroundStyle(.secondary).monospacedDigit()
                            }
                        }
                        .padding(16)
                        .background(.quaternary, in: RoundedRectangle(cornerRadius: 12))
                    }
                    if let error = model.errorMessage {
                        Label(error, systemImage: "exclamationmark.triangle")
                            .foregroundStyle(.red).fixedSize(horizontal: false, vertical: true)
                    }
                }
                .padding(24)
            }
            Divider()
            HStack {
                if model.isBusy {
                    Button(model.phase == .downloading ? "Pause" : "Cancel") { model.cancel() }
                        .disabled(model.isCancelling)
                } else {
                    Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                    if let machine = selected, model.hasPartialDownload(machine) {
                        Button("Discard Download") { model.discardDownload(machine) }
                    }
                    if model.errorMessage != nil && selected != nil {
                        Button("Reload Catalog") { model.loadCatalog() }
                    }
                }
                Spacer()
                if let machine = selected {
                    Button(model.hasPartialDownload(machine) ? "Resume" : "Download Mac") {
                        model.install(machine, name: name, directory: saveFolder) { url in
                            onInstall(url)
                            dismiss()
                        }
                    }
                    .keyboardShortcut(.defaultAction)
                    .disabled(model.isBusy || (try? MachineTemplateInstaller.normalizedName(name)) == nil ||
                              (try? machine.checkCompatibility()) == nil)
                } else {
                    Button("Try Again") { model.loadCatalog() }.disabled(model.isBusy)
                }
            }
            .padding(20)
        }
        .frame(width: 600, height: 610)
        .task { model.loadCatalog() }
        .onChange(of: selected?.id) { _, _ in
            if let selected {
                selectedID = selected.id
                name = selected.name
            }
        }
        .interactiveDismissDisabled(model.isBusy)
        .onDisappear { model.cancel() }
    }

    @ViewBuilder
    private func templateForm(_ machine: DownloadableMachine) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            if model.machines.count > 1 {
                Picker("Mac", selection: $selectedID) {
                    ForEach(model.machines) { item in Text(item.name).tag(Optional(item.id)) }
                }
                .disabled(model.isBusy)
            }
            Text(machine.name).font(.title3.bold())
            Text(machine.summary).foregroundStyle(.secondary)
            HStack {
                Label(machine.osVersion, systemImage: "desktopcomputer")
                Spacer()
                Text("GXMetal \(machine.gxMetalVersion)")
            }
            .font(.subheadline)
            Text("\(formatted(machine.archiveBytes)) download · \(formatted(machine.installedBytes + MachineTemplateInstaller.spaceReserve)) free space to install")
                .font(.caption).foregroundStyle(.secondary)
            if (try? machine.checkCompatibility()) == nil {
                Text("Requires ClassicMac \(machine.minimumAppVersion) or later.")
                    .font(.callout).foregroundStyle(.orange)
            }
        }
        Divider()
        VStack(alignment: .leading, spacing: 12) {
            TextField("Name your Mac", text: $name)
            LabeledContent("Save in") {
                Text(saveFolder.path).foregroundStyle(.secondary)
                    .lineLimit(1).truncationMode(.middle).help(saveFolder.path)
                Button("Choose…") {
                    let panel = NSOpenPanel()
                    panel.canChooseDirectories = true
                    panel.canChooseFiles = false
                    panel.canCreateDirectories = true
                    panel.allowsMultipleSelection = false
                    panel.directoryURL = saveFolder
                    panel.prompt = "Choose"
                    if panel.runModal() == .OK, let url = panel.url { saveFolder = url }
                }
            }
            Text("Your new Mac is saved as its own .classic machine. Downloads can be paused and continued later.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .disabled(model.isBusy)
    }

    private func formatted(_ bytes: Int64) -> String {
        ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file)
    }
}
