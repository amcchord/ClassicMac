import SwiftUI
import AppKit

// Handles Finder/dock open events (double-clicking a .classic package). SwiftUI's
// WindowGroup does not deliver these on its own, so we bridge through an AppKit
// delegate that forwards to the shared store.
final class AppDelegate: NSObject, NSApplicationDelegate {
    func application(_ application: NSApplication, open urls: [URL]) {
        for url in urls where url.pathExtension == VMConfig.packageExtension {
            VMStore.shared.openBundle(at: url, autostart: true)
        }
    }
}

@main
struct ClassicMacApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var store = VMStore.shared
    @StateObject private var manager = QEMUManager.shared

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(store)
                .environmentObject(manager)
                .frame(minWidth: 820, minHeight: 520)
        }
        .windowStyle(.titleBar)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("New Machine...") {
                    store.isPresentingNewVM = true
                }
                .keyboardShortcut("n", modifiers: .command)

                Button("Open...") {
                    store.presentOpenPanel()
                }
                .keyboardShortcut("o", modifiers: .command)

                OpenRecentMenu()
            }
        }
    }
}

// A basic "Open Recent" submenu backed by the shared document controller's
// recent .classic packages. Commands live outside the window's environment, so
// this talks to the shared store directly.
private struct OpenRecentMenu: View {
    var body: some View {
        Menu("Open Recent") {
            let recents = NSDocumentController.shared.recentDocumentURLs
            if recents.isEmpty {
                Button("No Recent Machines") {}
                    .disabled(true)
            } else {
                ForEach(recents, id: \.self) { url in
                    Button(url.deletingPathExtension().lastPathComponent) {
                        VMStore.shared.openBundle(at: url, autostart: false)
                    }
                }
                Divider()
                Button("Clear Menu") {
                    NSDocumentController.shared.clearRecentDocuments(nil)
                }
            }
        }
    }
}
