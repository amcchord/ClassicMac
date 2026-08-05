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
                    return true
                }
                return false
            }
        }
        .alert(
            currentError?.title ?? "Something Went Wrong",
            isPresented: errorBinding,
            presenting: currentError
        ) { error in
            if let logURL = error.logURL {
                Button("Show Details") {
                    NSWorkspace.shared.activateFileViewerSelecting([logURL])
                    clearErrors()
                }
            }
            Button("OK", role: .cancel) {
                clearErrors()
            }
        } message: { error in
            Text(error.message)
        }
        .confirmationDialog(
            "Shut Down \(pendingStopName)?",
            isPresented: stopConfirmationBinding,
            titleVisibility: .visible
        ) {
            Button("Shut Down Now", role: .destructive) {
                manager.confirmStop()
            }
            Button("Cancel", role: .cancel) {
                manager.cancelStop()
            }
        } message: {
            Text("This turns the machine off immediately and may lose unsaved work. When possible, shut down from inside Mac OS instead.")
        }
    }

    private var currentError: AppError? {
        if let error = store.lastError {
            return error
        }
        return manager.lastError
    }

    private func clearErrors() {
        store.lastError = nil
        manager.lastError = nil
    }

    private var pendingStopName: String {
        guard let id = manager.pendingStopID,
              let vm = store.vms.first(where: { $0.id == id }) else {
            return "this machine"
        }
        return "\u{201C}\(vm.name)\u{201D}"
    }

    private var stopConfirmationBinding: Binding<Bool> {
        Binding(
            get: { manager.pendingStopID != nil },
            set: { isPresented in
                if !isPresented {
                    manager.cancelStop()
                }
            }
        )
    }

    private var sidebar: some View {
        List(selection: $store.selectedID) {
            Section("Machines") {
                ForEach(store.vms) { vm in
                    VMRow(vm: vm, running: manager.isRunning(vm.id), paused: manager.isPaused(vm.id))
                        .tag(vm.id)
                        .contextMenu {
                            rowContextMenu(vm)
                        }
                }
            }
        }
        .listStyle(.sidebar)
        .frame(minWidth: 220)
        .safeAreaInset(edge: .bottom) {
            // Development builds only: unbundled runs get a footer telling the
            // developer how to produce the emulator. Bundled builds show no
            // footer at all.
            if !AppPaths.qemuIsAvailable {
                devFooter
            }
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

    @ViewBuilder
    private func rowContextMenu(_ vm: VMConfig) -> some View {
        let running = manager.isRunning(vm.id)
        if running {
            Button("Shut Down") {
                manager.requestStop(vm.id)
            }
        } else {
            Button("Start") {
                manager.start(vm)
            }
        }
        Divider()
        Button("Reveal in Finder") {
            NSWorkspace.shared.activateFileViewerSelecting([vm.folder])
        }
        Button("Remove from Library") {
            store.removeFromLibrary(vm)
        }
        .disabled(running)
    }

    private var devFooter: some View {
        VStack(spacing: 6) {
            Label("Emulator not bundled", systemImage: "exclamationmark.triangle.fill")
                .font(.caption)
                .foregroundStyle(.orange)
            Text("Run scripts/build-qemu.sh then scripts/bundle-qemu.sh")
                .font(.caption2)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
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
            EmptyStateView(
                hasMachines: !store.vms.isEmpty,
                showingNewVM: $store.isPresentingNewVM,
                openExisting: store.presentOpenPanel
            )
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
    let paused: Bool

    var body: some View {
        HStack(spacing: 10) {
            MachineBadgeView(family: vm.machineFamily, size: 30)
                .overlay(alignment: .bottomTrailing) {
                    if running {
                        statusDot
                    }
                }
            VStack(alignment: .leading, spacing: 2) {
                Text(vm.name)
                Text(vm.machineFamily.label)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
        }
        .padding(.vertical, 3)
    }

    private var statusDot: some View {
        Circle()
            .fill(paused ? Color.orange : Color.green)
            .frame(width: 9, height: 9)
            .overlay(
                Circle().strokeBorder(Color(nsColor: .windowBackgroundColor), lineWidth: 1.5)
            )
            .offset(x: 2, y: 2)
            .accessibilityLabel(paused ? "Paused" : "Running")
    }
}

struct EmptyStateView: View {
    let hasMachines: Bool
    @Binding var showingNewVM: Bool
    let openExisting: () -> Void

    var body: some View {
        VStack(spacing: 12) {
            HStack(spacing: 16) {
                MachineBadgeView(family: .quadra800, size: 76)
                MachineBadgeView(family: .powerMacG4, size: 76)
            }
            .padding(.bottom, 12)
            Text(hasMachines ? "Choose a Mac" : "Welcome to ClassicMac")
                .font(.largeTitle.bold())
            Text(description)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 420)
            HStack(spacing: 10) {
                Button {
                    showingNewVM = true
                } label: {
                    Label(hasMachines ? "New Machine" : "Create Your First Mac", systemImage: "plus")
                        .padding(.horizontal, 4)
                }
                .buttonStyle(.borderedProminent)

                Button(action: openExisting) {
                    Label("Open Machine", systemImage: "folder")
                }
                .buttonStyle(.bordered)
            }
            .controlSize(.large)
            .padding(.top, 8)
        }
        .padding(40)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var description: String {
        if hasMachines {
            return "Select a machine in the sidebar, or create another classic Macintosh."
        }
        return "Run System 7 through Mac OS 9 on a Quadra 800 or Power Mac G4. Each machine is a portable file you can keep anywhere."
    }
}
