import AppKit
import PlatinumKit

/// Development aid: with CLASSICMAC_UI_SNAPSHOTS=<folder> set, ClassicMac opens
/// the windows named in CLASSICMAC_UI_TOUR (comma separated: library, about,
/// settings, newmac, media, alert, shutdown), renders every visible window to
/// a PNG in that folder at the screen's scale, and quits. Run it with
/// CFFIXED_USER_HOME pointing at a scratch home so no real Macs are touched.
@MainActor
enum DebugSnapshots {
    static func runIfRequested() {
        let env = ProcessInfo.processInfo.environment
        guard let folder = env["CLASSICMAC_UI_SNAPSHOTS"] else { return }
        let tour = (env["CLASSICMAC_UI_TOUR"] ?? "").split(separator: ",").map(String.init)
        let out = URL(fileURLWithPath: folder, isDirectory: true)
        try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: 1_500_000_000)
            for step in tour {
                if step.hasPrefix("wait:"), let seconds = UInt64(step.dropFirst(5)) {
                    try? await Task.sleep(nanoseconds: seconds * 1_000_000_000)
                    continue
                }
                if step.hasPrefix("shot:") {
                    snapshot(into: out, prefix: String(step.dropFirst(5)))
                    continue
                }
                if step == "addicons" {
                    let sources = (env["CLASSICMAC_UI_ICON_SOURCES"] ?? "").split(separator: "|").map { URL(fileURLWithPath: String($0)) }
                    do { try await IconPacks.shared.add(sources) } catch { print("addicons:", error.localizedDescription) }
                    continue
                }
                if step == "stop" {
                    await stopSelected()
                    continue
                }
                open(step)
                try? await Task.sleep(nanoseconds: 1_200_000_000)
            }
            try? await Task.sleep(nanoseconds: 800_000_000)
            snapshot(into: out, prefix: "")
            if env["CLASSICMAC_UI_STAY"] == nil {
                await stopSelected()
                NSApp.terminate(nil)
            }
        }
    }

    private static func snapshot(into out: URL, prefix: String) {
        // Guest screens draw on the display link, which pauses while the
        // display sleeps; draw their latest frames before capturing.
        NotificationCenter.default.post(name: GuestScreenNSView.refreshNotification, object: nil)
        var index = 0
        for window in NSApp.orderedWindows.reversed() where window.isVisible {
            index += 1
            let title = window.title.isEmpty ? "window" : window.title.replacingOccurrences(of: "/", with: "-")
            let name = String(format: "%@%02d-%@.png", prefix.isEmpty ? "" : prefix + "-", index, title)
            write(window, to: out.appendingPathComponent(name))
        }
    }

    /// Shuts the selected Mac down the way the app does, waiting up to 40 s.
    private static func stopSelected() async {
        let manager = QEMUManager.shared
        guard let id = VMStore.shared.selectedID, manager.isRunning(id) else { return }
        manager.requestStop(id)
        manager.confirmStop()
        for _ in 0..<80 where manager.isRunning(id) {
            try? await Task.sleep(nanoseconds: 500_000_000)
        }
    }

    private static func open(_ step: String) {
        let manager = WindowManager.shared
        let store = VMStore.shared
        switch step {
        case "library": manager.showLibrary()
        case "about": manager.showAbout()
        case "newmac": manager.presentNewMac()
        case "icons": manager.presentIconPacks()
        case "guestwindow":
            // The screen window alone, for a stand-in emulator to connect to.
            if let id = store.selectedID, let vm = store.vms.first(where: { $0.id == id }) {
                _ = try? GuestWindows.shared.prepare(vm)
                GuestWindows.shared.show(id)
                print("display-socket:", GuestDisplaySession.socketPath(for: id))
            }
        case "settings": if let id = store.selectedID { manager.presentSettings(for: id) }
        case "media": if let id = store.selectedID { MediaController.shared.present(for: id) }
        case "shutdown": if let id = store.selectedID { QEMUManager.shared.requestStop(id) }
        case "alert":
            manager.alert(.caution, "This is a Platinum alert.", informative: "Buttons, icon and text follow the Mac OS alert layout.",
                          buttons: [PlatinumAlertButton("OK", role: .default), PlatinumAlertButton("Cancel", role: .cancel)])
        case "start": if let id = store.selectedID, let vm = store.vms.first(where: { $0.id == id }) { QEMUManager.shared.start(vm) }
        case "collapse": (NSApp.orderedWindows.first { $0.isVisible } as? PlatinumWindow)?.toggleCollapse()
        default:
            if step.hasPrefix("guest-size:"), let id = store.selectedID,
               let vm = store.vms.first(where: { $0.id == id }),
               let window = NSApp.windows.first(where: { $0.title == vm.name }) as? PlatinumWindow {
                let dimensions = step.dropFirst(11).split(separator: "x").compactMap { Double($0) }
                if dimensions.count == 2 {
                    if window.state.isCollapsed { window.toggleCollapse() }
                    var frame = window.frame
                    let size = PlatinumWindow.frameSize(forContent: NSSize(width: dimensions[0], height: dimensions[1] + GuestWindows.toolbarHeight), kind: .document)
                    frame.origin.y += frame.height - size.height; frame.size = size
                    window.setFrame(frame, display: true)
                    window.makeKeyAndOrderFront(nil)
                }
            }
            if step == "guest-activate", let id = store.selectedID,
               let vm = store.vms.first(where: { $0.id == id }) {
                NSApp.windows.first(where: { $0.title == vm.name })?.makeKeyAndOrderFront(nil)
                NSApp.activate(ignoringOtherApps: true)
            }
            if step.hasPrefix("select:"), let n = Int(step.dropFirst(7)), store.vms.indices.contains(n) {
                store.selectedID = store.vms[n].id
            }
        }
    }

    private static func write(_ window: NSWindow, to url: URL) {
        guard let view = window.contentView else { return }
        let bounds = view.bounds
        guard let rep = view.bitmapImageRepForCachingDisplay(in: bounds) else { return }
        view.cacheDisplay(in: bounds, to: rep)
        try? rep.representation(using: .png, properties: [:])?.write(to: url)
    }
}
