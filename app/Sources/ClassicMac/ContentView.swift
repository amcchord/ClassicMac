import SwiftUI

struct ContentView: View {
    @EnvironmentObject var store: VMStore
    @EnvironmentObject var manager: QEMUManager

    var body: some View {
        NavigationSplitView {
            sidebar
        } detail: {
            detail
        }
        .sheet(isPresented: $store.isPresentingNewVM) {
            NewVMSheet { newConfig in
                if let created = store.createVM(newConfig) {
                    store.selectedID = created.id
                }
            }
        }
        .alert("Something went wrong", isPresented: errorBinding) {
            Button("OK", role: .cancel) {
                store.lastError = nil
                manager.lastError = nil
            }
        } message: {
            Text(store.lastError ?? manager.lastError ?? "")
        }
    }

    private var sidebar: some View {
        List(selection: $store.selectedID) {
            Section("Virtual Machines") {
                ForEach(store.vms) { vm in
                    VMRow(vm: vm, running: manager.isRunning(vm.id))
                        .tag(vm.id)
                }
            }
        }
        .listStyle(.sidebar)
        .frame(minWidth: 220)
        .safeAreaInset(edge: .bottom) {
            sidebarFooter
        }
        .navigationTitle("ClassicMac")
        .toolbar {
            ToolbarItem {
                Menu {
                    Button {
                        store.isPresentingNewVM = true
                    } label: {
                        Label("New Machine...", systemImage: "plus")
                    }
                    Button {
                        store.presentOpenPanel()
                    } label: {
                        Label("Open Machine...", systemImage: "folder")
                    }
                } label: {
                    Label("Add Machine", systemImage: "plus")
                } primaryAction: {
                    store.isPresentingNewVM = true
                }
                .help("Create a new classic Macintosh, or open an existing .classic machine")
            }
        }
    }

    private var sidebarFooter: some View {
        VStack(spacing: 6) {
            if !AppPaths.qemuIsAvailable {
                Label("Emulator not bundled", systemImage: "exclamationmark.triangle.fill")
                    .font(.caption)
                    .foregroundStyle(.orange)
                Text("Run scripts/build-qemu.sh then scripts/bundle-qemu.sh")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            } else {
                Label("Quadra 800 & Power Mac G4", systemImage: "cpu")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(8)
        .background(.bar)
    }

    @ViewBuilder
    private var detail: some View {
        if let id = store.selectedID, store.vms.contains(where: { $0.id == id }) {
            VMDetailView(vmID: id)
                .id(id)
        } else {
            EmptyStateView(showingNewVM: $store.isPresentingNewVM)
        }
    }

    private var errorBinding: Binding<Bool> {
        Binding(
            get: { store.lastError != nil || manager.lastError != nil },
            set: { newValue in
                if !newValue {
                    store.lastError = nil
                    manager.lastError = nil
                }
            }
        )
    }
}

struct VMRow: View {
    let vm: VMConfig
    let running: Bool

    var body: some View {
        HStack {
            Image(systemName: "desktopcomputer")
                .foregroundStyle(.secondary)
            VStack(alignment: .leading, spacing: 2) {
                Text(vm.name)
                Text("\(vm.machineFamily.label) - \(vm.ramMB) MB - \(vm.resolutionLabel)")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            if running {
                Circle()
                    .fill(.green)
                    .frame(width: 8, height: 8)
            }
        }
        .padding(.vertical, 2)
    }
}

struct EmptyStateView: View {
    @Binding var showingNewVM: Bool

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "desktopcomputer")
                .font(.system(size: 64))
                .foregroundStyle(.secondary)
            Text("Welcome to ClassicMac")
                .font(.title)
                .bold()
            Text("Emulate a Quadra 800 (System 7 - 8.1) or a Power Mac G4 (Mac OS 8.5 - 9.2).")
                .foregroundStyle(.secondary)
            Button {
                showingNewVM = true
            } label: {
                Label("Create your first machine", systemImage: "plus")
            }
            .controlSize(.large)
            .buttonStyle(.borderedProminent)
        }
        .padding(40)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
