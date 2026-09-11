import AppKit
import SwiftUI

// The same three drive identities are used by the launcher, native menu, and
// HMP bridge. Never expose arbitrary monitor commands through the media UI.
enum MediaDevice: String, Codable, CaseIterable {
    case disc = "cd0", tools = "tools0", floppy = "fd0"

    var label: String {
        switch self {
        case .disc: return "Disc"
        case .tools: return "ClassicMac Tools"
        case .floppy: return "Floppy disk"
        }
    }

    func path(in config: VMConfig) -> String? {
        switch self {
        case .disc: return config.cdImagePath?.nilIfEmpty
        case .floppy: return config.floppyImagePath?.nilIfEmpty
        case .tools: return config.toolsCDInserted ? AppPaths.toolsCD?.path : nil
        }
    }

    func setting(path: String?, in config: VMConfig) -> VMConfig {
        var updated = config
        switch self {
        case .disc:
            updated.cdImagePath = path
            if path == nil { updated.bootFromCD = false }
        case .floppy: updated.floppyImagePath = path
        case .tools: updated.toolsCDInserted = path != nil
        }
        return updated
    }
}

private extension String {
    var nilIfEmpty: String? { isEmpty ? nil : self }
}

struct MediaDriveState: Decodable, Equatable {
    let device: MediaDevice
    let path: String?
    let inserted: Bool
}

struct MediaSnapshot: Decodable, Equatable {
    let drives: [MediaDriveState]
    let liveChanges: Bool
    let error: String?

    subscript(_ device: MediaDevice) -> MediaDriveState? {
        drives.first { $0.device == device }
    }

    static func parse(_ response: String?) throws -> Self {
        guard let response,
              let line = response.components(separatedBy: .newlines).last(where: { $0.hasPrefix("CLASSICMAC_MEDIA ") }),
              let data = line.dropFirst("CLASSICMAC_MEDIA ".count).data(using: .utf8),
              let snapshot = try? JSONDecoder().decode(Self.self, from: data) else {
            throw MediaError("The Mac's media controls aren't responding. Wait for startup to finish, or shut down the Mac to change its media.")
        }
        if let error = snapshot.error { throw MediaError(error) }
        return snapshot
    }
}

struct MediaError: LocalizedError {
    let message: String
    init(_ message: String) { self.message = message }
    var errorDescription: String? { message }
}

enum MediaCommand {
    // HMP string syntax supports escaped quotes/backslashes. Reject control
    // characters rather than permitting a filename to become another command.
    static func quotedPath(_ path: String) throws -> String {
        guard path.hasPrefix("/"), !path.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }) else {
            throw MediaError("Choose an image with a filename that has no line breaks or control characters.")
        }
        return "\"" + path.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"") + "\""
    }

    static func change(_ device: MediaDevice, path: String?) throws -> String {
        if let path { return "classicmac-media insert \(device.rawValue) \(try quotedPath(path))" }
        return "classicmac-media eject \(device.rawValue)"
    }

    static func validateImage(_ path: String, device: MediaDevice) throws {
        _ = try quotedPath(path)
        let url = URL(fileURLWithPath: path)
        let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey, .isReadableKey, .isWritableKey])
        guard values?.isRegularFile == true, values?.isReadable == true, (values?.fileSize ?? 0) > 0 else {
            throw MediaError("“\(url.lastPathComponent)” is unavailable or empty. Locate the image again, or choose another file.")
        }
        if device == .floppy && values?.isWritable != true {
            throw MediaError("The floppy image is read-only. Choose a writable raw floppy image.")
        }
    }
}

struct RecentMedia: Codable, Equatable, Identifiable {
    var path: String
    let device: MediaDevice
    var id: String { "\(device.rawValue):\(path)" }
    var name: String { URL(fileURLWithPath: path).lastPathComponent }
    var available: Bool { FileManager.default.isReadableFile(atPath: path) }
}

@MainActor
final class MediaRecents: ObservableObject {
    static let shared = MediaRecents()
    @Published private(set) var items: [RecentMedia]
    private let defaults: UserDefaults
    private let key = "ClassicMac.RecentMedia.v1"

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        items = defaults.data(forKey: key).flatMap { try? JSONDecoder().decode([RecentMedia].self, from: $0) } ?? []
    }

    func remember(_ path: String, device: MediaDevice) {
        guard device != .tools else { return }
        let item = RecentMedia(path: URL(fileURLWithPath: path).standardizedFileURL.path, device: device)
        items.removeAll { $0.id == item.id }
        items.insert(item, at: 0)
        items = Array(items.prefix(12))
        persist()
    }

    func forget(_ item: RecentMedia) { items.removeAll { $0.id == item.id }; persist() }
    func clear() { items.removeAll(); persist() }
    private func persist() { if let data = try? JSONEncoder().encode(items) { defaults.set(data, forKey: key) } }
}

@MainActor
final class MediaController: NSObject, ObservableObject, NSWindowDelegate {
    static let shared = MediaController()
    @Published private(set) var snapshots: [UUID: MediaSnapshot] = [:]
    @Published private(set) var busyIDs: Set<UUID> = []
    @Published private(set) var messages: [UUID: String] = [:]
    @Published private(set) var errors: [UUID: String] = [:]
    private var refreshingIDs: Set<UUID> = []
    private var windows: [UUID: NSWindow] = [:]
    private var listening = false
    private let recentMedia: MediaRecents

    init(recents: MediaRecents? = nil) {
        recentMedia = recents ?? .shared
        super.init()
    }

    func startListening() {
        guard !listening else { return }
        listening = true
        DistributedNotificationCenter.default().addObserver(self, selector: #selector(openFromNative(_:)), name: .init("com.classicmac.media"), object: nil)
    }

    @objc private func openFromNative(_ notification: Notification) {
        guard let raw = notification.object as? String, let id = UUID(uuidString: raw),
              QEMUManager.shared.isRunning(id) else { return }
        present(for: id)
    }

    func present(for id: UUID) {
        guard let config = VMStore.shared.vms.first(where: { $0.id == id }) else { return }
        if let window = windows[id] {
            window.makeKeyAndOrderFront(nil)
        } else {
            let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 660, height: 640), styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
            window.title = "Media — \(config.name)"
            window.contentView = NSHostingView(rootView: MediaWindowContent(id: id))
            window.minSize = NSSize(width: 580, height: 480)
            window.isReleasedWhenClosed = false
            window.delegate = self
            window.center()
            windows[id] = window
            window.makeKeyAndOrderFront(nil)
        }
        NSApp.activate(ignoringOtherApps: true)
    }

    func windowWillClose(_ notification: Notification) {
        guard let window = notification.object as? NSWindow else { return }
        windows = windows.filter { $0.value !== window }
    }

    func refresh(_ id: UUID, running: Bool) async {
        guard running else { snapshots[id] = nil; return }
        guard !refreshingIDs.contains(id), !busyIDs.contains(id) else { return }
        refreshingIDs.insert(id)
        defer { refreshingIDs.remove(id) }
        do { snapshots[id] = try await request("classicmac-media status", id: id) }
        catch { snapshots[id] = nil }
    }

    private func request(_ command: String, id: UUID) async throws -> MediaSnapshot {
        let socketPath = QEMUManager.monitorSocketURL(for: id).path
        return try await Task.detached(priority: .userInitiated) {
            try MediaSnapshot.parse(HMPClient.command(command, socketPath: socketPath))
        }.value
    }

    // Configuration writes happen only after a live operation is confirmed.
    // Power Macs stage settings only: OS 9 does not reliably notice IDE swaps.
    func change(_ device: MediaDevice, path: String?, config: VMConfig, running: Bool, paused: Bool) async -> Bool {
        let id = config.id
        guard !busyIDs.contains(id) else { return false }
        busyIDs.insert(id)
        errors[id] = nil
        messages[id] = nil
        defer { busyIDs.remove(id) }
        do {
            if let path { try MediaCommand.validateImage(path, device: device) }
            if running && config.machineFamily == .quadra800 {
                guard !paused else { throw MediaError("Resume the Mac before changing its media so it can safely finish disk activity.") }
                let current = try await request("classicmac-media status", id: id)
                guard current.liveChanges, current[device] != nil else { throw MediaError("This drive does not support changes while the Mac is running. Shut down the Mac first.") }
                // The writable floppy must finish its guest-side eject before
                // insertion. Never force-replace a mounted writable image.
                if device == .floppy, current[device]?.inserted == true, path != nil {
                    throw MediaError("Eject the floppy disk first, then insert the replacement after the Mac finishes using it.")
                }
                var result = try await request(MediaCommand.change(device, path: path), id: id)
                if device == .floppy && path == nil {
                    messages[id] = "Waiting for the Mac to finish using the floppy disk…"
                    let deadline = Date().addingTimeInterval(12)
                    while result[device]?.inserted == true && Date() < deadline {
                        guard QEMUManager.shared.isRunning(id), !QEMUManager.shared.isPaused(id) else {
                            throw MediaError("The Mac stopped or paused before the floppy eject finished. Check the disk after resuming.")
                        }
                        try await Task.sleep(nanoseconds: 250_000_000)
                        result = try await request("classicmac-media status", id: id)
                    }
                }
                snapshots[id] = result
                guard let drive = result[device], drive.inserted == (path != nil), path == nil || drive.path == path else {
                    throw MediaError(device == .floppy ? "The Mac is still using the floppy. Eject it from the Mac desktop, then try again. Nothing was forced out." : "The media change could not be confirmed. Wait a moment, then try again.")
                }
                messages[id] = path == nil ? "\(device.label) ejected." : "\(device.label) inserted."
            } else {
                messages[id] = running ? "Saved for the next startup. Shut down and start the Mac to apply this change." : "Media saved."
            }
            if let path { recentMedia.remember(path, device: device) }
            return true
        } catch {
            messages[id] = nil
            errors[id] = error.localizedDescription
            return false
        }
    }
}

private struct MediaWindowContent: View {
    let id: UUID
    @ObservedObject private var store = VMStore.shared
    @ObservedObject private var manager = QEMUManager.shared

    var body: some View {
        if let config = store.vms.first(where: { $0.id == id }) {
            MediaDrawerView(config: Binding(get: { store.vms.first(where: { $0.id == id }) ?? config }, set: { _ = store.save($0) }), isRunning: manager.isRunning(id), isPaused: manager.isPaused(id))
        } else {
            ContentUnavailableView("Machine Unavailable", systemImage: "desktopcomputer", description: Text("Open the machine in ClassicMac again to manage its media."))
        }
    }
}
