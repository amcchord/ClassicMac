import SwiftUI

@main
struct ClassicMacApp: App {
    @StateObject private var store = VMStore()
    @StateObject private var manager = QEMUManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(store)
                .environmentObject(manager)
                .frame(minWidth: 820, minHeight: 520)
        }
        .windowStyle(.titleBar)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
