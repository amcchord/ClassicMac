import Foundation
import SwiftUI

// Loads, saves and creates VMs. VMs live in Application Support/ClassicMac/VMs.
@MainActor
final class VMStore: ObservableObject {
    @Published var vms: [VMConfig] = []
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

    func reload() {
        var loaded: [VMConfig] = []
        let fm = FileManager.default
        let entries = (try? fm.contentsOfDirectory(at: AppPaths.vmsDir, includingPropertiesForKeys: nil)) ?? []
        for entry in entries {
            let configURL = entry.appendingPathComponent("config.json")
            guard let data = try? Data(contentsOf: configURL) else { continue }
            guard let config = try? decoder.decode(VMConfig.self, from: data) else { continue }
            loaded.append(config)
        }
        loaded.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        vms = loaded
    }

    func save(_ config: VMConfig) {
        AppPaths.ensureDirectory(config.folder)
        do {
            let data = try encoder.encode(config)
            try data.write(to: config.configURL, options: .atomic)
        } catch {
            lastError = "Could not save VM: \(error.localizedDescription)"
            return
        }
        if let index = vms.firstIndex(where: { $0.id == config.id }) {
            vms[index] = config
        } else {
            vms.append(config)
        }
        vms.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    func delete(_ config: VMConfig) {
        try? FileManager.default.removeItem(at: config.folder)
        vms.removeAll { $0.id == config.id }
    }

    // Creates the disk image and PRAM image for a new VM using bundled qemu-img,
    // then persists the config. Returns the saved config on success.
    @discardableResult
    func createVM(_ config: VMConfig) -> VMConfig? {
        AppPaths.ensureDirectory(config.folder)

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
        return config
    }
}
