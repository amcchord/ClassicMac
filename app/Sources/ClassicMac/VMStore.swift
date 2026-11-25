import Foundation
import SwiftUI
import AppKit
import Darwin
import UniformTypeIdentifiers

extension UTType {
    // The VM package type. Declared/exported in the app bundle's Info.plist as
    // com.classicmac.vm; resolved here by extension so it also works when the
    // app is run unbundled during development.
    static var classicVM: UTType {
        UTType(filenameExtension: VMConfig.packageExtension, conformingTo: .package) ?? .package
    }
}

// Manages the library of .classic VM packages: loading, saving, creating,
// opening (double-click / File > Open), and removing them. VMs can live anywhere
// on disk; the library is just an index of their locations kept in Application
// Support. A shared instance is used so the AppKit open handler and the SwiftUI
// scene operate on the same state.
@MainActor
final class VMStore: ObservableObject {
    static let shared = VMStore()

    @Published var vms: [VMConfig] = []
    @Published private(set) var unavailablePaths: [String] = []
    @Published var selectedID: UUID?
    @Published var isPresentingNewVM = false
    @Published var lastError: AppError?

    private let indexURL: URL
    private let mediaDir: URL
    private let migrateLegacy: Bool

    private enum CachedTemplateOS {
        case missing
        case present(String)
    }
    private var templateOSVersionCache: [URL: CachedTemplateOS] = [:]

    private let encoder: JSONEncoder = {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        return e
    }()
    private let decoder = JSONDecoder()

    init(
        indexURL: URL = AppPaths.libraryIndexURL,
        mediaDir: URL = AppPaths.mediaDir,
        migrateLegacy: Bool = true
    ) {
        self.indexURL = indexURL
        self.mediaDir = mediaDir
        self.migrateLegacy = migrateLegacy
        reload()
    }

    // MARK: Loading & template cache

    func reload() {
        if migrateLegacy {
            migrateLegacyVMsIfNeeded()
        }
        templateOSVersionCache.removeAll()

        let previousSelection = selectedID
        var loaded: [VMConfig] = []
        var retainedPaths: [String] = []
        var missingPaths: [String] = []
        let fm = FileManager.default

        for url in indexedBundleURLs() {
            let path = url.standardizedFileURL.path
            if let config = loadConfig(from: url) {
                loaded.append(config)
                retainedPaths.append(path)
            } else if !fm.fileExists(atPath: path) {
                // Retain missing/unmounted paths (e.g. external drives) across
                // reloads instead of dropping them from library.json.
                missingPaths.append(path)
                retainedPaths.append(path)
            }
        }
        loaded.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        vms = loaded
        unavailablePaths = missingPaths
        if let previousSelection, loaded.contains(where: { $0.id == previousSelection }) {
            selectedID = previousSelection
        } else {
            selectedID = loaded.first?.id
        }
        writeIndex(retainedPaths)
    }

    func templateOSVersion(for vm: VMConfig) -> String? {
        let key = vm.folder.standardizedFileURL
        if let cached = templateOSVersionCache[key] {
            switch cached {
            case .missing: return nil
            case .present(let version): return version
            }
        }
        if let version = VMTemplateMetadata.load(from: key)?.osVersion {
            templateOSVersionCache[key] = .present(version)
            return version
        }
        templateOSVersionCache[key] = .missing
        return nil
    }

    private func loadConfig(from bundleURL: URL) -> VMConfig? {
        let configURL = bundleURL.appendingPathComponent("config.json")
        guard let data = try? Data(contentsOf: configURL) else { return nil }
        guard var config = try? decoder.decode(VMConfig.self, from: data) else { return nil }
        config.bundleURL = bundleURL
        return config
    }

    // MARK: Saving

    @discardableResult
    func save(_ config: VMConfig) -> Bool {
        guard let bundle = config.bundleURL else {
            lastError = AppError(
                "Couldn't Save Changes",
                "This machine has no location on disk to save to."
            )
            return false
        }
        AppPaths.ensureDirectory(bundle)
        do {
            let data = try encoder.encode(config)
            try data.write(to: config.configURL, options: .atomic)
        } catch {
            lastError = AppError(
                "Couldn't Save Changes",
                "The machine's settings could not be saved. \(error.localizedDescription)"
            )
            return false
        }
        templateOSVersionCache.removeValue(forKey: bundle.standardizedFileURL)
        upsert(config)
        addToIndex(bundle)
        return true
    }

    // MARK: Creating

    // Creates the .classic package (config.json, disk image, and on the Quadra
    // a PRAM image) at the config's bundleURL using bundled qemu-img, records
    // it in the library, and selects it. Returns the saved config on success.
    // On failure the partially built package is removed so no broken .classic
    // file is left behind.
    @discardableResult
    func createVM(_ config: VMConfig) -> VMConfig? {
        guard let setup = prepareCreateVM(config) else { return nil }
        if let failure = Self.createImages(for: config) {
            return setup.fail(failure)
        }
        return finalizeCreateVM(config, fail: setup.fail)
    }

    @discardableResult
    func createVMAsync(_ config: VMConfig) async -> VMConfig? {
        guard let setup = prepareCreateVM(config) else { return nil }
        let failure: String? = await Task.detached(priority: .userInitiated) {
            Self.createImages(for: config)
        }.value
        if let failure {
            return setup.fail(failure)
        }
        return finalizeCreateVM(config, fail: setup.fail)
    }

    private func prepareCreateVM(_ config: VMConfig) -> (fail: (String) -> VMConfig?, ())? {
        let errorTitle = "Couldn't Create \u{201C}\(config.name)\u{201D}"
        guard let bundle = config.bundleURL else {
            lastError = AppError(errorTitle, "No save location was chosen for the new machine.")
            return nil
        }

        let fm = FileManager.default
        let bundleExisted = fm.fileExists(atPath: bundle.path)
        do {
            try fm.createDirectory(at: bundle, withIntermediateDirectories: true)
        } catch {
            lastError = AppError(errorTitle, "The machine file could not be created. \(error.localizedDescription)")
            return nil
        }

        let fail: (String) -> VMConfig? = { [weak self] message in
            self?.lastError = AppError(errorTitle, message)
            if !bundleExisted {
                try? fm.removeItem(at: bundle)
            }
            return nil
        }
        return (fail, ())
    }

    private func finalizeCreateVM(_ config: VMConfig, fail: (String) -> VMConfig?) -> VMConfig? {
        guard let bundle = config.bundleURL else { return nil }
        if !save(config) {
            return fail(lastError?.message ?? "The machine's settings could not be saved.")
        }
        NSDocumentController.shared.noteNewRecentDocumentURL(bundle)
        selectedID = config.id
        return config
    }

    nonisolated private static func createImages(for config: VMConfig) -> String? {
        let fm = FileManager.default
        let diskResult = QEMUManager.createRawImage(at: config.diskImageURL, sizeArgument: "\(config.diskSizeGB)G")
        if case let .failure(message) = diskResult {
            return "The machine's hard disk could not be created. \(message)"
        }

        // The Power Mac (mac99) keeps its NVRAM inside the machine; only the
        // Quadra boots with an external PRAM image. Seed it from the known-good
        // template (valid signature) so the virtio declaration ROM used for
        // folder sharing does not hang the boot; fall back to a blank PRAM if
        // the template is unavailable (dev without bundle).
        if config.machineFamily.usesPRAMImage {
            if fm.fileExists(atPath: AppPaths.pramSeed.path) {
                do {
                    if fm.fileExists(atPath: config.pramImageURL.path) {
                        try fm.removeItem(at: config.pramImageURL)
                    }
                    try fm.copyItem(at: AppPaths.pramSeed, to: config.pramImageURL)
                } catch {
                    return "The machine's startup memory could not be created. \(error.localizedDescription)"
                }
            } else {
                let pramResult = QEMUManager.createRawImage(at: config.pramImageURL, sizeArgument: "256b")
                if case let .failure(message) = pramResult {
                    return "The machine's startup memory could not be created. \(message)"
                }
            }
        }
        return nil
    }

    // MARK: Opening existing packages

    // Loads a .classic package into the library, selects it, records it as a
    // recent document, and optionally boots it. Used by File > Open and by the
    // Finder/dock open handler (double-click).
    func openBundle(at url: URL, autostart: Bool) {
        templateOSVersionCache.removeValue(forKey: url.standardizedFileURL)
        guard var config = loadConfig(from: url) else {
            lastError = AppError(
                "Couldn't Open \u{201C}\(url.lastPathComponent)\u{201D}",
                "It doesn't appear to be a ClassicMac machine, or its settings file is damaged."
            )
            return
        }
        let targetResolved = url.standardizedFileURL.resolvingSymlinksInPath().path
        if let existing = vms.first(where: { $0.id == config.id }),
           existing.folder.standardizedFileURL.resolvingSymlinksInPath().path != targetResolved {
            // Finder duplicate of an existing .classic bundle: assign a fresh
            // UUID so both machines have independent identities and sockets.
            config.id = UUID()
            do {
                let data = try encoder.encode(config)
                try data.write(to: config.configURL, options: .atomic)
            } catch {
                lastError = AppError(
                    "Couldn't Open \u{201C}\(url.lastPathComponent)\u{201D}",
                    "The duplicated machine's settings could not be updated. \(error.localizedDescription)"
                )
                return
            }
        }
        unavailablePaths.removeAll { $0 == url.standardizedFileURL.path }
        upsert(config)
        addToIndex(url)
        NSDocumentController.shared.noteNewRecentDocumentURL(url)
        selectedID = config.id
        if autostart {
            QEMUManager.shared.start(config)
        }
    }

    func presentOpenPanel() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.allowedContentTypes = [.classicVM]
        panel.message = "Open a ClassicMac machine (.classic)"
        panel.prompt = "Open"
        if panel.runModal() == .OK, let url = panel.url {
            openBundle(at: url, autostart: false)
        }
    }

    // MARK: Removing

    nonisolated static func bundleDiskUsageBytes(for bundle: URL) -> Int64 {
        let keys: Set<URLResourceKey> = [
            .isRegularFileKey,
            .totalFileAllocatedSizeKey,
            .fileAllocatedSizeKey,
            .fileSizeKey,
        ]
        guard let enumerator = FileManager.default.enumerator(
            at: bundle,
            includingPropertiesForKeys: Array(keys),
            options: []
        ) else {
            return 0
        }
        var total: Int64 = 0
        for case let fileURL as URL in enumerator {
            guard let values = try? fileURL.resourceValues(forKeys: keys),
                  values.isRegularFile == true else { continue }
            let size = values.totalFileAllocatedSize ?? values.fileAllocatedSize ?? values.fileSize ?? 0
            total += Int64(max(size, 0))
        }
        return total
    }

    // Forgets a VM but leaves its package on disk.
    func removeFromLibrary(_ config: VMConfig) {
        let removedIndex = vms.firstIndex { $0.id == config.id }
        vms.removeAll { $0.id == config.id }
        if selectedID == config.id {
            let nextIndex = min(removedIndex ?? 0, max(vms.count - 1, 0))
            selectedID = vms.isEmpty ? nil : vms[nextIndex].id
        }
        if let bundle = config.bundleURL {
            templateOSVersionCache.removeValue(forKey: bundle.standardizedFileURL)
            removeFromIndex(bundle)
        }
    }

    // Forgets a VM and moves its package to the Trash.
    func moveToTrash(_ config: VMConfig) {
        if let bundle = config.bundleURL {
            var st = stat()
            if lstat(bundle.path, &st) != 0 && errno == ENOENT {
                // Already absent from disk; remove from library and clean up orphaned media.
            } else {
                guard isSafePackageToTrash(bundle, matching: config.id) else {
                    lastError = AppError(
                        "Couldn't Move \u{201C}\(config.name)\u{201D} to the Trash",
                        "This folder could not be verified as a ClassicMac package."
                    )
                    return
                }
                do {
                    try FileManager.default.trashItem(at: bundle, resultingItemURL: nil)
                } catch {
                    lastError = AppError(
                        "Couldn't Move \u{201C}\(config.name)\u{201D} to the Trash",
                        error.localizedDescription
                    )
                    return
                }
            }
        }
        let candidateMedia = [config.cdImagePath, config.floppyImagePath]
            .compactMap { $0 }
            .filter { !$0.isEmpty }
        removeFromLibrary(config)
        cleanUpUnreferencedMedia(candidateMedia)
    }

    private func isSafePackageToTrash(_ bundle: URL, matching vmID: UUID) -> Bool {
        guard bundle.pathExtension.lowercased() == VMConfig.packageExtension else {
            return false
        }
        var st = stat()
        guard lstat(bundle.path, &st) == 0,
              (st.st_mode & S_IFMT) == S_IFDIR else {
            return false
        }
        let configURL = bundle.appendingPathComponent("config.json")
        var cfgStat = stat()
        guard lstat(configURL.path, &cfgStat) == 0,
              (cfgStat.st_mode & S_IFMT) == S_IFREG,
              let data = try? Data(contentsOf: configURL),
              let decoded = try? decoder.decode(VMConfig.self, from: data),
              decoded.id == vmID else {
            return false
        }
        let bundleResolved = bundle.standardizedFileURL.resolvingSymlinksInPath().path
        let prefix = bundleResolved.hasSuffix("/") ? bundleResolved : bundleResolved + "/"
        for other in vms where other.id != vmID {
            let otherResolved = other.folder.standardizedFileURL.resolvingSymlinksInPath().path
            if otherResolved == bundleResolved || otherResolved.hasPrefix(prefix) {
                return false
            }
        }
        for unavailable in unavailablePaths {
            let otherResolved = URL(fileURLWithPath: unavailable).standardizedFileURL.resolvingSymlinksInPath().path
            if otherResolved == bundleResolved || otherResolved.hasPrefix(prefix) {
                return false
            }
        }
        return true
    }

    private func cleanUpUnreferencedMedia(_ candidatePaths: [String]) {
        guard !candidatePaths.isEmpty else { return }
        let fm = FileManager.default
        let resolvedMediaRoot = mediaDir.standardizedFileURL.resolvingSymlinksInPath().path
        let mediaPrefix = resolvedMediaRoot.hasSuffix("/") ? resolvedMediaRoot : resolvedMediaRoot + "/"

        let referenced = Set(vms.flatMap { vm in
            [vm.cdImagePath, vm.floppyImagePath]
                .compactMap { $0 }
                .filter { !$0.isEmpty }
                .map { URL(fileURLWithPath: $0).standardizedFileURL.resolvingSymlinksInPath().path }
        })

        for path in candidatePaths {
            let resolved = URL(fileURLWithPath: path).standardizedFileURL.resolvingSymlinksInPath().path
            guard resolved.hasPrefix(mediaPrefix), !referenced.contains(resolved) else { continue }
            var st = stat()
            guard lstat(resolved, &st) == 0, (st.st_mode & S_IFMT) == S_IFREG else { continue }
            try? fm.removeItem(atPath: resolved)
        }
    }

    // MARK: In-memory list helpers

    private func upsert(_ config: VMConfig) {
        if let index = vms.firstIndex(where: { $0.id == config.id }) {
            vms[index] = config
        } else {
            vms.append(config)
        }
        vms.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    // MARK: Library index (list of package paths)

    private func indexedBundleURLs() -> [URL] {
        guard let data = try? Data(contentsOf: indexURL),
              let paths = try? decoder.decode([String].self, from: data) else {
            return []
        }
        var seen = Set<String>()
        var urls: [URL] = []
        for path in paths {
            let standardized = URL(fileURLWithPath: path).standardizedFileURL
            if seen.insert(standardized.path).inserted {
                urls.append(standardized)
            }
        }
        return urls
    }

    private func writeIndex(_ paths: [String]) {
        var seen = Set<String>()
        var unique: [String] = []
        for path in paths {
            if seen.insert(path).inserted {
                unique.append(path)
            }
        }
        guard let data = try? encoder.encode(unique) else { return }
        try? data.write(to: indexURL, options: .atomic)
    }

    private func addToIndex(_ bundle: URL) {
        var paths = indexedBundleURLs().map { $0.path }
        paths.append(bundle.standardizedFileURL.path)
        writeIndex(paths)
    }

    private func removeFromIndex(_ bundle: URL) {
        let target = bundle.standardizedFileURL.path
        let paths = indexedBundleURLs().map { $0.path }.filter { $0 != target }
        unavailablePaths.removeAll { $0 == target }
        writeIndex(paths)
    }

    // MARK: One-time migration from the old Application Support/VMs layout

    private func migrateLegacyVMsIfNeeded() {
        let fm = FileManager.default
        guard fm.fileExists(atPath: AppPaths.legacyVMsDir.path) else { return }
        let entries = (try? fm.contentsOfDirectory(at: AppPaths.legacyVMsDir,
                                                   includingPropertiesForKeys: nil)) ?? []
        for entry in entries {
            let legacyConfig = entry.appendingPathComponent("config.json")
            guard fm.fileExists(atPath: legacyConfig.path) else { continue }
            let name = (try? decoder.decode(VMConfig.self, from: Data(contentsOf: legacyConfig)))?.name
                ?? entry.lastPathComponent
            let destination = uniqueBundleURL(in: AppPaths.defaultLibraryDir, name: name)
            do {
                try fm.moveItem(at: entry, to: destination)
                addToIndex(destination)
            } catch {
                // Leave the legacy VM in place; it can be migrated on a later run.
                continue
            }
        }
        // Remove the now-empty legacy directory so we don't scan it again.
        if let remaining = try? fm.contentsOfDirectory(at: AppPaths.legacyVMsDir, includingPropertiesForKeys: nil),
           remaining.isEmpty {
            try? fm.removeItem(at: AppPaths.legacyVMsDir)
        }
    }

    // MARK: Naming

    // A non-colliding <dir>/<name>.classic URL.
    func uniqueBundleURL(in directory: URL, name: String) -> URL {
        let base = sanitizedBundleName(name)
        let fm = FileManager.default
        var candidate = directory.appendingPathComponent("\(base).\(VMConfig.packageExtension)")
        var counter = 2
        while fm.fileExists(atPath: candidate.path) {
            candidate = directory.appendingPathComponent("\(base) \(counter).\(VMConfig.packageExtension)")
            counter += 1
        }
        return candidate
    }

    // A non-colliding URL for copied installation media. Keeping this logic
    // beside package naming prevents a same-named disc from silently reusing
    // an unrelated older image in the shared media directory.
    nonisolated static func uniqueFileURL(in directory: URL, suggestedName: String) -> URL {
        let source = URL(fileURLWithPath: suggestedName)
        let ext = source.pathExtension
        let rawBase = source.deletingPathExtension().lastPathComponent
        var base = rawBase.trimmingCharacters(in: .whitespacesAndNewlines)
        base = base.replacingOccurrences(of: "/", with: "-")
        base = base.replacingOccurrences(of: ":", with: "-")
        if base.isEmpty {
            base = "Disc"
        }
        let fm = FileManager.default

        func fileName(counter: Int?) -> String {
            let suffix = counter.map { " \($0)" } ?? ""
            return ext.isEmpty ? "\(base)\(suffix)" : "\(base)\(suffix).\(ext)"
        }

        var candidate = directory.appendingPathComponent(fileName(counter: nil))
        var counter = 2
        while fm.fileExists(atPath: candidate.path) {
            candidate = directory.appendingPathComponent(fileName(counter: counter))
            counter += 1
        }
        return candidate
    }

    func sanitizedBundleName(_ name: String) -> String {
        var cleaned = name.trimmingCharacters(in: .whitespacesAndNewlines)
        cleaned = cleaned.replacingOccurrences(of: "/", with: "-")
        cleaned = cleaned.replacingOccurrences(of: ":", with: "-")
        while let first = cleaned.first, first == "." || first.isWhitespace || first.isNewline {
            cleaned.removeFirst()
        }
        cleaned = cleaned.trimmingCharacters(in: .whitespacesAndNewlines)
        if cleaned.isEmpty {
            return "Machine"
        }
        return cleaned
    }
}
