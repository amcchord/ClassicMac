import SwiftUI

struct ContentView: View {
    // Supplied by the download flow when its catalog is available.
    var downloadMachine: (() -> Void)? = nil
    @EnvironmentObject var store: VMStore
    @EnvironmentObject var manager: QEMUManager

    var body: some View {
        NavigationSplitView {
            sidebar
        } detail: {
            detail
        }
        .sheet(isPresented: $store.isPresentingNewVM) {
            NewMachineWizard(onCreate: { newConfig in
                if let created = store.createVM(newConfig) {
                    store.selectedID = created.id
                    return true
                }
                return false
            }, onInstall: { url in
                store.openBundle(at: url, autostart: false)
            })
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
            Button("Shut Down", role: .destructive) {
                manager.confirmStop()
            }
            Button("Cancel", role: .cancel) {
                manager.cancelStop()
            }
        } message: {
            Text("ClassicMac will ask Mac OS to shut down safely so its disk stays healthy and the next startup is faster. If Mac OS does not respond, the machine will turn off after 15 seconds and unsaved work may be lost.")
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
        .navigationSplitViewColumnWidth(min: 190, ideal: 220, max: 300)
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
                    if let downloadMachine {
                        Button(action: downloadMachine) {
                            Label("Download Mac OS 9…", systemImage: "arrow.down.circle")
                        }
                        Divider()
                    }
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
            Button("Show Mac") {
                manager.activate(vm.id)
            }
            if manager.isPaused(vm.id) {
                Button("Resume") { manager.resume(vm.id) }
            } else {
                Button("Pause") { manager.pause(vm.id) }
            }
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
            VMDetailView(
                vmID: id,
                templateOSVersion: store.vms.first { $0.id == id }
                    .flatMap { VMTemplateMetadata.load(from: $0.folder)?.osVersion }
            )
                .id(id)
        } else {
            EmptyStateView(
                hasMachines: !store.vms.isEmpty,
                showingNewVM: $store.isPresentingNewVM,
                openExisting: store.presentOpenPanel,
                downloadMachine: downloadMachine
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
                    .lineLimit(1)
                    .fontWeight(running ? .medium : .regular)
                Text(paused ? "Paused · \(vm.machineFamily.label)" : vm.machineFamily.label)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
        }
        .padding(.vertical, 5)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(vm.name), \(vm.machineFamily.label), \(paused ? "paused" : running ? "running" : "shut down")")
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
    var downloadMachine: (() -> Void)? = nil

    var body: some View {
        ScrollView {
            VStack(spacing: 18) {
                HStack(spacing: 20) {
                    MachineBadgeView(family: .quadra800, size: 68)
                        .rotationEffect(.degrees(-5))
                    MachineBadgeView(family: .powerMacG4, size: 68)
                        .rotationEffect(.degrees(5))
                }
                .accessibilityHidden(true)
                .padding(.bottom, 10)

                VStack(spacing: 10) {
                    Text(hasMachines ? "Choose a Mac" : "Welcome to ClassicMac")
                        .font(.largeTitle.weight(.semibold))
                    Text(description)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                        .frame(maxWidth: 400)
                        .fixedSize(horizontal: false, vertical: true)
                }

                VStack(spacing: 12) {
                    if let downloadMachine {
                        Button(action: downloadMachine) {
                            Label("Download Mac OS 9", systemImage: "arrow.down.circle")
                                .padding(.horizontal, 8)
                        }
                        .buttonStyle(.borderedProminent)
                    }
                    HStack(spacing: 10) {
                        if downloadMachine == nil {
                            newMachineButton.buttonStyle(.borderedProminent)
                        } else {
                            newMachineButton.buttonStyle(.bordered)
                        }
                        Button(action: openExisting) {
                            Label("Open Machine", systemImage: "folder")
                        }
                        .buttonStyle(.bordered)
                    }
                }
                .controlSize(.large)
                .padding(.top, 6)
            }
            .padding(32)
            .frame(maxWidth: .infinity)
        }
        .defaultScrollAnchor(.center)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private var newMachineButton: some View {
        Button {
            showingNewVM = true
        } label: {
            Label("New Machine", systemImage: "plus")
        }
    }

    private var description: String {
        if hasMachines {
            return "Select a machine in the sidebar to pick up where you left off, or add another classic Macintosh."
        }
        if downloadMachine != nil {
            return "Start with a ready-to-run Mac OS 9 system, create a Mac of your own, or open an existing machine."
        }
        return "Run System 7 through Mac OS 9. Create a classic Macintosh, or open a portable .classic machine from anywhere on your Mac."
    }
}
