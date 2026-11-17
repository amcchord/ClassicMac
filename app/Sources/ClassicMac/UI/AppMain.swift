import AppKit
import SwiftUI
import PlatinumKit

@main
enum ClassicMacMain {
    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.setActivationPolicy(.regular)
        withExtendedLifetime(delegate) { app.run() }
    }
}

/// The application: menus, Finder open events and window routing. Every
/// window is a Platinum window managed by `WindowManager`.
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuItemValidation {
    private var pendingOpenURLs: [URL] = []
    private var launched = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        PlatinumFonts.registerIfNeeded()
        IconPacks.shared.load()
        NSApp.mainMenu = MainMenu.build()
        PasteTextController.shared.startListening()
        MediaController.shared.startListening()
        WindowManager.shared.start()
        launched = true
        for url in pendingOpenURLs { open(url) }
        pendingOpenURLs.removeAll()
        NSApp.activate(ignoringOtherApps: true)
        DebugSnapshots.runIfRequested()
    }

    // Double-clicking a .classic package in the Finder.
    func application(_ application: NSApplication, open urls: [URL]) {
        for url in urls where url.pathExtension == VMConfig.packageExtension {
            if launched { open(url) } else { pendingOpenURLs.append(url) }
        }
    }

    private func open(_ url: URL) {
        VMStore.shared.openBundle(at: url, autostart: true)
        WindowManager.shared.showMain()
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag { WindowManager.shared.showMain() }
        return true
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    /// Running Macs live in ClassicMac's windows, so they shut down with it.
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        let manager = QEMUManager.shared
        let running = manager.runningIDs.count
        guard running > 0 else { return .terminateNow }
        if let osx = VMStore.shared.vms.first(where: {
            $0.machineFamily == .powerMacOSX && manager.isRunning($0.id)
        }) {
            WindowManager.shared.alert(.caution, "Shut down Mac OS X before quitting.",
                informative: "Choose Shut Down from the Apple menu inside your Mac.",
                buttons: [PlatinumAlertButton("Show Mac", role: .default) {
                    if manager.isPaused(osx.id) { manager.resume(osx.id) }
                    manager.activate(osx.id)
                }])
            return .terminateCancel
        }
        let names = VMStore.shared.vms.filter { manager.isRunning($0.id) }.map { "\u{201C}\($0.name)\u{201D}" }
        let message = running == 1 ? "Shut down \(names.first ?? "the running Mac") and quit ClassicMac?"
                                   : "Shut down \(running) running Macs and quit ClassicMac?"
        WindowManager.shared.alert(.caution, message,
            informative: "Each Mac shuts down safely first, as if you chose Shut Down from the Special menu. A Mac that does not respond within 15 seconds is turned off.",
            buttons: [
                PlatinumAlertButton("Shut Down and Quit", role: .default) {
                    manager.shutDownAll { NSApp.reply(toApplicationShouldTerminate: true) }
                },
                PlatinumAlertButton("Cancel", role: .cancel) { NSApp.reply(toApplicationShouldTerminate: false) },
            ])
        return .terminateLater
    }

    // MARK: Menu actions

    private var store: VMStore { .shared }
    private var manager: QEMUManager { .shared }

    private var selected: VMConfig? {
        guard let id = store.selectedID else { return nil }
        return store.vms.first { $0.id == id }
    }

    @objc func showAbout(_ sender: Any?) { WindowManager.shared.showAbout() }
    @objc func showIconPacks(_ sender: Any?) { WindowManager.shared.presentIconPacks() }
    @objc func newMac(_ sender: Any?) { WindowManager.shared.presentNewMac() }
    @objc func openMac(_ sender: Any?) { store.presentOpenPanel(); WindowManager.shared.showMain() }
    @objc func showLibrary(_ sender: Any?) { WindowManager.shared.showLibrary() }
    @objc func showMainWindow(_ sender: Any?) { WindowManager.shared.showMain() }

    @objc func openRecent(_ sender: NSMenuItem) {
        guard let url = sender.representedObject as? URL else { return }
        store.openBundle(at: url, autostart: false)
        WindowManager.shared.showMain()
    }

    @objc func clearRecents(_ sender: Any?) { NSDocumentController.shared.clearRecentDocuments(nil) }

    @objc func startOrShow(_ sender: Any?) {
        guard let vm = selected else { return }
        if manager.isRunning(vm.id) {
            if manager.isPaused(vm.id) { manager.resume(vm.id) }
            manager.activate(vm.id)
        } else {
            manager.start(vm)
        }
    }

    @objc func pauseOrResume(_ sender: Any?) {
        guard let vm = selected, manager.isRunning(vm.id) else { return }
        if manager.isPaused(vm.id) { manager.resume(vm.id) } else { manager.pause(vm.id) }
    }

    @objc func restart(_ sender: Any?) { if let vm = selected { manager.reboot(vm.id) } }
    @objc func shutDown(_ sender: Any?) { if let vm = selected { manager.requestStop(vm.id) } }
    @objc func showMedia(_ sender: Any?) { if let vm = selected { MediaController.shared.present(for: vm.id) } }
    @objc func pasteText(_ sender: Any?) { if let vm = selected { PasteTextController.shared.present(for: vm.id) } }
    @objc func forceQuitApp(_ sender: Any?) { if let vm = selected { manager.forceQuitFrontmostApp(vm.id) } }
    @objc func showSettings(_ sender: Any?) { if let vm = selected { WindowManager.shared.presentSettings(for: vm.id) } }

    @objc func revealInFinder(_ sender: Any?) {
        if let vm = selected { NSWorkspace.shared.activateFileViewerSelecting([vm.folder]) }
    }

    @objc func removeMac(_ sender: Any?) { if let vm = selected { WindowManager.shared.confirmRemove(vm) } }

    @objc func collapseWindow(_ sender: Any?) {
        (NSApp.keyWindow as? PlatinumWindow)?.toggleCollapse()
    }

    @objc func openHelp(_ sender: Any?) {
        NSWorkspace.shared.open(URL(string: "https://github.com/amcchord/ClassicMac#readme")!)
    }

    func validateMenuItem(_ item: NSMenuItem) -> Bool {
        let vm = selected
        let running = vm.map { manager.isRunning($0.id) } ?? false
        let paused = vm.map { manager.isPaused($0.id) } ?? false
        let copland = vm?.machineFamily == .powerMac7500
        let blocked = WindowManager.shared.mainIsBlocked
        switch item.action {
        case #selector(startOrShow(_:)):
            item.title = running ? (paused ? "Resume and Show Mac" : "Show Mac") : "Start Up"
            return vm != nil && !blocked && (running || AppPaths.qemuIsAvailable(for: vm!.machineFamily))
        case #selector(pauseOrResume(_:)):
            item.title = paused ? "Resume" : "Pause"
            return running
        case #selector(restart(_:)), #selector(shutDown(_:)):
            return running
        case #selector(showMedia(_:)):
            return vm != nil && !copland
        case #selector(pasteText(_:)), #selector(forceQuitApp(_:)):
            return running && !paused && !copland
        case #selector(showSettings(_:)), #selector(revealInFinder(_:)):
            return vm != nil && !blocked
        case #selector(removeMac(_:)):
            return vm != nil && !running && !blocked
        case #selector(newMac(_:)), #selector(openMac(_:)), #selector(showIconPacks(_:)):
            return !blocked
        case #selector(collapseWindow(_:)):
            if let window = NSApp.keyWindow as? PlatinumWindow, window.boxes.collapse {
                item.title = window.state.isCollapsed ? "Expand" : "Collapse"
                return true
            }
            return false
        default:
            return true
        }
    }
}

/// The menu bar. ClassicMac keeps the system menu bar (as every Mac app
/// must) with Mac OS-style names: Start Up, Get Info, Shut Down.
@MainActor
enum MainMenu {
    static func build() -> NSMenu {
        let bar = NSMenu()

        let app = submenu(bar, "ClassicMac")
        app.addItem(item("About ClassicMac", #selector(AppDelegate.showAbout(_:))))
        app.addItem(.separator())
        app.addItem(item("Iconfactory Icons…", #selector(AppDelegate.showIconPacks(_:))))
        app.addItem(.separator())
        let services = NSMenuItem(title: "Services", action: nil, keyEquivalent: "")
        services.submenu = NSMenu(title: "Services")
        NSApp.servicesMenu = services.submenu
        app.addItem(services)
        app.addItem(.separator())
        app.addItem(item("Hide ClassicMac", #selector(NSApplication.hide(_:)), "h"))
        app.addItem(item("Hide Others", #selector(NSApplication.hideOtherApplications(_:)), "h", [.command, .option]))
        app.addItem(item("Show All", #selector(NSApplication.unhideAllApplications(_:))))
        app.addItem(.separator())
        app.addItem(item("Quit ClassicMac", #selector(NSApplication.terminate(_:)), "q"))

        let file = submenu(bar, "File")
        file.addItem(item("New Mac…", #selector(AppDelegate.newMac(_:)), "n"))
        file.addItem(item("Open Mac…", #selector(AppDelegate.openMac(_:)), "o"))
        let recent = NSMenuItem(title: "Open Recent", action: nil, keyEquivalent: "")
        let recentMenu = NSMenu(title: "Open Recent")
        recentMenu.delegate = RecentMenuDelegate.shared
        recent.submenu = recentMenu
        file.addItem(recent)
        file.addItem(.separator())
        file.addItem(item("Mac OS Library…", #selector(AppDelegate.showLibrary(_:)), "l", [.command, .shift]))
        file.addItem(.separator())
        file.addItem(item("Close Window", #selector(NSWindow.performClose(_:)), "w"))

        let edit = submenu(bar, "Edit")
        edit.addItem(item("Undo", Selector(("undo:")), "z"))
        edit.addItem(item("Redo", Selector(("redo:")), "z", [.command, .shift]))
        edit.addItem(.separator())
        edit.addItem(item("Cut", #selector(NSText.cut(_:)), "x"))
        edit.addItem(item("Copy", #selector(NSText.copy(_:)), "c"))
        edit.addItem(item("Paste", #selector(NSText.paste(_:)), "v"))
        edit.addItem(item("Clear", #selector(NSText.delete(_:))))
        edit.addItem(item("Select All", #selector(NSText.selectAll(_:)), "a"))

        let mac = submenu(bar, "Mac")
        mac.addItem(item("Start Up", #selector(AppDelegate.startOrShow(_:)), "r"))
        mac.addItem(item("Pause", #selector(AppDelegate.pauseOrResume(_:))))
        mac.addItem(item("Restart", #selector(AppDelegate.restart(_:)), "r", [.command, .shift]))
        mac.addItem(item("Shut Down…", #selector(AppDelegate.shutDown(_:))))
        mac.addItem(.separator())
        mac.addItem(item("Get Info…", #selector(AppDelegate.showSettings(_:)), "i"))
        mac.addItem(item("Media…", #selector(AppDelegate.showMedia(_:)), "e", [.command, .shift]))
        mac.addItem(item("Paste Text into Mac…", #selector(AppDelegate.pasteText(_:)), "v", [.command, .shift]))
        mac.addItem(item("Force Quit Frontmost App…", #selector(AppDelegate.forceQuitApp(_:))))
        mac.addItem(.separator())
        mac.addItem(item("Show in Finder", #selector(AppDelegate.revealInFinder(_:))))
        mac.addItem(item("Remove Mac…", #selector(AppDelegate.removeMac(_:))))

        let window = submenu(bar, "Window")
        window.addItem(item("Minimize", #selector(NSWindow.performMiniaturize(_:)), "m"))
        window.addItem(item("Zoom", #selector(NSWindow.performZoom(_:))))
        window.addItem(item("Collapse", #selector(AppDelegate.collapseWindow(_:)), "m", [.command, .option]))
        window.addItem(.separator())
        window.addItem(item("ClassicMac", #selector(AppDelegate.showMainWindow(_:)), "1"))
        window.addItem(item("Mac OS Library", #selector(AppDelegate.showLibrary(_:)), "2"))
        window.addItem(.separator())
        window.addItem(item("Bring All to Front", #selector(NSApplication.arrangeInFront(_:))))
        NSApp.windowsMenu = window

        let help = submenu(bar, "Help")
        help.addItem(item("ClassicMac Help", #selector(AppDelegate.openHelp(_:)), "?"))
        NSApp.helpMenu = help
        return bar
    }

    private static func submenu(_ bar: NSMenu, _ title: String) -> NSMenu {
        let holder = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        let menu = NSMenu(title: title)
        holder.submenu = menu
        bar.addItem(holder)
        return menu
    }

    private static func item(_ title: String, _ action: Selector, _ key: String = "",
                             _ modifiers: NSEvent.ModifierFlags = .command) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
        item.keyEquivalentModifierMask = modifiers
        return item
    }
}

/// Fills File → Open Recent from the document controller's recent packages.
@MainActor
final class RecentMenuDelegate: NSObject, NSMenuDelegate {
    static let shared = RecentMenuDelegate()

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        let recents = NSDocumentController.shared.recentDocumentURLs
        if recents.isEmpty {
            let empty = NSMenuItem(title: "No Recent Macs", action: nil, keyEquivalent: "")
            empty.isEnabled = false
            menu.addItem(empty)
            return
        }
        for url in recents {
            let item = NSMenuItem(title: url.deletingPathExtension().lastPathComponent,
                                  action: #selector(AppDelegate.openRecent(_:)), keyEquivalent: "")
            item.representedObject = url
            menu.addItem(item)
        }
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "Clear Menu", action: #selector(AppDelegate.clearRecents(_:)), keyEquivalent: ""))
    }
}
