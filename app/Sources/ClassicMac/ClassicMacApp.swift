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
                Button("New Machine\u{2026}") {
                    store.isPresentingNewVM = true
                }
                .keyboardShortcut("n", modifiers: .command)

                Button("Open\u{2026}") {
                    store.presentOpenPanel()
                }
                .keyboardShortcut("o", modifiers: .command)

                OpenRecentMenu()
            }

            // App-level commands for the selected machine, so every lifecycle
            // action is discoverable in the menu bar with a shortcut.
            CommandMenu("Machine") {
                MachineCommands()
            }
        }
    }
}

// Menu-bar commands acting on the machine selected in the sidebar. Commands
// live outside the window's environment, so this observes the shared objects
// directly.
private struct MachineCommands: View {
    @ObservedObject private var store = VMStore.shared
    @ObservedObject private var manager = QEMUManager.shared

    private var selected: VMConfig? {
        guard let id = store.selectedID else { return nil }
        return store.vms.first { $0.id == id }
    }

    var body: some View {
        let vm = selected
        let running = vm.map { manager.isRunning($0.id) } ?? false
        let paused = vm.map { manager.isPaused($0.id) } ?? false

        Button("Start") {
            if let vm = vm {
                manager.start(vm)
            }
        }
        .keyboardShortcut("r", modifiers: .command)
        .disabled(vm == nil || running)

        if paused {
            Button("Resume") {
                if let vm = vm {
                    manager.resume(vm.id)
                }
            }
            .disabled(!running)
        } else {
            Button("Pause") {
                if let vm = vm {
                    manager.pause(vm.id)
                }
            }
            .disabled(!running)
        }

        Button("Restart") {
            if let vm = vm {
                manager.reboot(vm.id)
            }
        }
        .keyboardShortcut("r", modifiers: [.command, .shift])
        .disabled(!running)

        Button("Shut Down") {
            if let vm = vm {
                manager.stop(vm.id)
            }
        }
        .disabled(!running)

        Divider()

        Button("Reveal in Finder") {
            if let folder = vm?.folder {
                NSWorkspace.shared.activateFileViewerSelecting([folder])
            }
        }
        .disabled(vm == nil)
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
