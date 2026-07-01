import Foundation
import SwiftUI
import AppKit
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
    @Published var selectedID: UUID?
    @Published var isPresentingNewVM = false
    @Published var lastError: String?

    private let encoder: JSONEncoder = {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        return e
    }()
    private let decoder = JSONDecoder()

    init() {
        reload()
    }

    // MARK: Loading

    func reload() {
        migrateLegacyVMsIfNeeded()

        var loaded: [VMConfig] = []
        var validPaths: [String] = []
        for url in indexedBundleURLs() {
            guard let config = loadConfig(from: url) else { continue }
            loaded.append(config)
            validPaths.append(url.standardizedFileURL.path)
        }
        loaded.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        vms = loaded
        // Prune entries whose packages have gone missing.
        writeIndex(validPaths)
    }

    private func loadConfig(from bundleURL: URL) -> VMConfig? {
        let configURL = bundleURL.appendingPathComponent("config.json")
        guard let data = try? Data(contentsOf: configURL) else { return nil }
        guard var config = try? decoder.decode(VMConfig.self, from: data) else { return nil }
        config.bundleURL = bundleURL
        return config
    }

    // MARK: Saving

    func save(_ config: VMConfig) {
        guard let bundle = config.bundleURL else {
            lastError = "This machine has no location on disk to save to."
            return
        }
        AppPaths.ensureDirectory(bundle)
        do {
            let data = try encoder.encode(config)
            try data.write(to: config.configURL, options: .atomic)
        } catch {
            lastError = "Could not save VM: \(error.localizedDescription)"
            return
        }
        upsert(config)
        addToIndex(bundle)
    }

    // MARK: Creating

    // Creates the .classic package (config.json, disk image, PRAM image) at the
    // config's bundleURL using bundled qemu-img, records it in the library, and
    // selects it. Returns the saved config on success.
    @discardableResult
    func createVM(_ config: VMConfig) -> VMConfig? {
        guard let bundle = config.bundleURL else {
            lastError = "No save location was chosen for the new machine."
            return nil
        }
        do {
            try FileManager.default.createDirectory(at: bundle, withIntermediateDirectories: true)
        } catch {
            lastError = "Could not create the machine package: \(error.localizedDescription)"
            return nil
        }

        let diskResult = QEMUManager.createRawImage(at: config.diskImageURL, sizeArgument: "\(config.diskSizeGB)G")
        if case let .failure(message) = diskResult {
            lastError = "Could not create disk image: \(message)"
            return nil
        }

        // Seed PRAM from the known-good template (valid signature) so the virtio
        // declaration ROM used for folder sharing does not hang the boot. Fall
        // back to a blank PRAM if the template is unavailable (dev without bundle).
        let fm = FileManager.default
        if fm.fileExists(atPath: AppPaths.pramSeed.path) {
            do {
                if fm.fileExists(atPath: config.pramImageURL.path) {
                    try fm.removeItem(at: config.pramImageURL)
                }
                try fm.copyItem(at: AppPaths.pramSeed, to: config.pramImageURL)
            } catch {
                lastError = "Could not create PRAM image: \(error.localizedDescription)"
                return nil
            }
        } else {
            let pramResult = QEMUManager.createRawImage(at: config.pramImageURL, sizeArgument: "256b")
            if case let .failure(message) = pramResult {
                lastError = "Could not create PRAM image: \(message)"
                return nil
            }
        }

        save(config)
        NSDocumentController.shared.noteNewRecentDocumentURL(bundle)
        selectedID = config.id
        return config
    }

    // MARK: Opening existing packages

    // Loads a .classic package into the library, selects it, records it as a
    // recent document, and optionally boots it. Used by File > Open and by the
    // Finder/dock open handler (double-click).
    func openBundle(at url: URL, autostart: Bool) {
        guard let config = loadConfig(from: url) else {
            lastError = "\u{201C}\(url.lastPathComponent)\u{201D} is not a ClassicMac machine."
            return
        }
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

    // Forgets a VM but leaves its package on disk.
    func removeFromLibrary(_ config: VMConfig) {
        vms.removeAll { $0.id == config.id }
        if selectedID == config.id { selectedID = nil }
        if let bundle = config.bundleURL {
            removeFromIndex(bundle)
        }
    }

    // Forgets a VM and moves its package to the Trash.
    func moveToTrash(_ config: VMConfig) {
        if let bundle = config.bundleURL {
            do {
                try FileManager.default.trashItem(at: bundle, resultingItemURL: nil)
            } catch {
                lastError = "Could not move the machine to the Trash: \(error.localizedDescription)"
                return
            }
        }
        removeFromLibrary(config)
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
        guard let data = try? Data(contentsOf: AppPaths.libraryIndexURL),
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
        try? data.write(to: AppPaths.libraryIndexURL, options: .atomic)
    }

    private func addToIndex(_ bundle: URL) {
        var paths = indexedBundleURLs().map { $0.path }
        paths.append(bundle.standardizedFileURL.path)
        writeIndex(paths)
    }

    private func removeFromIndex(_ bundle: URL) {
        let target = bundle.standardizedFileURL.path
        let paths = indexedBundleURLs().map { $0.path }.filter { $0 != target }
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

    private func sanitizedBundleName(_ name: String) -> String {
        var cleaned = name.trimmingCharacters(in: .whitespacesAndNewlines)
        cleaned = cleaned.replacingOccurrences(of: "/", with: "-")
        cleaned = cleaned.replacingOccurrences(of: ":", with: "-")
        if cleaned.isEmpty {
            return "Machine"
        }
        return cleaned
    }
}
