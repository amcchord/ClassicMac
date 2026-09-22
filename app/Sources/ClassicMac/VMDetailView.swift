import SwiftUI

struct VMDetailView: View {
    let vmID: UUID
    // The importer can supply the template OS without guessing from a name.
    var templateOSVersion: String? = nil
    @EnvironmentObject var store: VMStore
    @EnvironmentObject var manager: QEMUManager

    @State private var config: VMConfig?
    @State private var showingDeleteConfirm = false
    @State private var savedPreview: NSImage?
    @State private var showingSettings = false
    @State private var settingsTab: SettingsTab = .general

    private enum SettingsTab: String, CaseIterable, Identifiable {
        case general = "General"
        case display = "Display"
        case sharing = "Sharing"
        var id: Self { self }
    }

    var body: some View {
        Group {
            if let binding = configBinding {
                content(binding)
            } else {
                Text("This machine no longer exists.")
                    .foregroundStyle(.secondary)
            }
        }
        .onAppear(perform: load)
        .onChange(of: manager.previews[vmID]) { _, image in
            retainUsefulPreview(image)
        }
        .onChange(of: store.vms) { _, machines in
            // QEMU can update the saved startup device after an installer
            // blesses the hard disk. Keep this view's editable copy in sync.
            if let latest = machines.first(where: { $0.id == vmID }),
               latest != config {
                config = latest
            }
        }
    }

    private func load() {
        config = store.vms.first(where: { $0.id == vmID })
        if let config = config {
            retainUsefulPreview(NSImage(contentsOf: config.previewURL))
            retainUsefulPreview(manager.previews[vmID])
        }
    }

    private var configBinding: Binding<VMConfig>? {
        guard let initialConfig = config else { return nil }
        return Binding(
            get: { config ?? initialConfig },
            set: { newValue in
                config = newValue
                store.save(newValue)
            }
        )
    }

    private var running: Bool { manager.isRunning(vmID) }
    private var paused: Bool { manager.isPaused(vmID) }

    @ViewBuilder
    private func content(_ vm: Binding<VMConfig>) -> some View {
        GeometryReader { geometry in
            ScrollView {
                VStack(alignment: .leading, spacing: 22) {
                    header(vm)
                    MachineHomePreview(
                        image: savedPreview,
                        running: running,
                        paused: paused,
                        browserDisplay: vm.wrappedValue.useBrowserDisplay,
                        showMac: { manager.activate(vmID) }
                    )
                    .frame(height: previewHeight(in: geometry.size))

                    homeActions(vm)
                    machineSummary(vm.wrappedValue)
                    if vm.wrappedValue.machineFamily == .powerMac7500 {
                        GroupBox("Copland D11E4 · Experimental") {
                            VStack(alignment: .leading, spacing: 10) {
                                Text("Explore Apple's unfinished Mac OS rewrite. Some actions trigger developer assertions or crashes. Sound, networking, shared folders, removable media, and browser viewing are unavailable.")
                                Text("Control-G captures or releases the mouse. Control-+ and Control-− scale the window; Control-F toggles full screen.")
                                Text("The Activities download adds a desktop folder with games, graphics demos, a text editor and developer reading. Download a new Mac to get these files; existing disks are preserved.")
                                Link("Copland activities and compatibility guide", destination: URL(string: "https://github.com/amcchord/ClassicMac/blob/main/copland/ACTIVITIES.md")!)
                                if manager.coplandHaltedIDs.contains(vmID) {
                                    Text("Copland stopped at a developer assertion. Continue asks its debugger to resume; the affected feature may still fail.").foregroundStyle(.orange)
                                }
                            }.font(.callout).frame(maxWidth: .infinity, alignment: .leading).padding(8)
                        }
                    }

                    if running && vm.wrappedValue.useBrowserDisplay {
                        GroupBox {
                            browserAccessSection
                        }
                    }
                    if vm.wrappedValue.machineFamily == .powerMacG4,
                       vm.wrappedValue.bootFromCD,
                       vm.wrappedValue.cdImagePath?.isEmpty == false {
                        installationCard
                    }
                }
                .frame(maxWidth: 840)
                .padding(24)
                .frame(maxWidth: .infinity)
            }
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .navigationTitle(vm.wrappedValue.name)
        .toolbar { toolbar(vm) }
        .sheet(isPresented: $showingSettings) {
            settingsSheet(vm)
        }
        .confirmationDialog("Remove \(vm.wrappedValue.name)?", isPresented: $showingDeleteConfirm, titleVisibility: .visible) {
            Button("Move to Trash", role: .destructive) {
                store.moveToTrash(vm.wrappedValue)
            }
            Button("Remove from Library") {
                store.removeFromLibrary(vm.wrappedValue)
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Move to Trash deletes the machine file, including its disk and settings. Remove from Library keeps the file on disk but takes it out of ClassicMac.")
        }
    }

    private var installationCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Installing Mac OS", systemImage: "list.number")
                .font(.headline)
            PowerMacInstallGuide(includeGXMetal: false)
            Text("After the first hard-disk boot, ClassicMac Tools mounts automatically so you can install and test GXMetal.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.quaternary.opacity(0.4), in: RoundedRectangle(cornerRadius: 12))
    }

    // MARK: Home

    private func previewHeight(in size: CGSize) -> CGFloat {
        // Keep useful actions within reach in a small window, while letting
        // the desktop preview grow on larger displays. The home still scrolls.
        min(360, max(200, size.height - 310), max(200, (size.width - 48) * 0.58))
    }

    private func header(_ vm: Binding<VMConfig>) -> some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack(spacing: 14) {
                MachineBadgeView(family: vm.wrappedValue.machineFamily, size: 48)
                    .accessibilityHidden(true)
                VStack(alignment: .leading, spacing: 4) {
                    Text(vm.wrappedValue.name)
                        .font(.title2.weight(.semibold))
                        .lineLimit(2)
                        .textSelection(.enabled)
                    Text(vm.wrappedValue.machineFamily.label)
                        .foregroundStyle(.secondary)
                }
                Spacer(minLength: 8)
                statusBadge
            }
            HStack(alignment: .center, spacing: 16) {
                primaryAction(vm)
                    .controlSize(.large)
                    .buttonStyle(.borderedProminent)
                VStack(alignment: .leading, spacing: 3) {
                    Text(templateOSVersion == nil ? "Supports" : "Created with")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text(templateOSVersion ?? vm.wrappedValue.machineFamily.osSupportLabel)
                        .font(.callout)
                }
                .accessibilityElement(children: .combine)
                Spacer(minLength: 0)
            }
        }
    }

    private func primaryAction(_ vm: Binding<VMConfig>) -> some View {
        Button {
            if running {
                if paused { manager.resume(vmID) }
                manager.activate(vmID)
            } else {
                manager.start(vm.wrappedValue)
            }
        } label: {
            Label(
                running ? (paused ? "Resume Mac" : "Show Mac") : "Start Mac",
                systemImage: running && !paused ? (vm.wrappedValue.useBrowserDisplay ? "safari" : "macwindow") : "play.fill"
            )
            .padding(.horizontal, 6)
        }
        .disabled(!running && !AppPaths.qemuIsAvailable(for: vm.wrappedValue.machineFamily))
        .help(running ? "Open your Mac" : "Start this Mac (⌘R)")
    }

    private func homeActions(_ vm: Binding<VMConfig>) -> some View {
        HStack(spacing: 10) {
            if vm.wrappedValue.machineFamily != .powerMac7500 {
                Button(action: showMedia) {
                    Label("Media", systemImage: "opticaldisc")
                }
                .help("Manage inserted discs and the startup disk")
            }
            if vm.wrappedValue.hasSharedFolder,
               let path = vm.wrappedValue.sharedFolderPath {
                Button {
                    NSWorkspace.shared.open(URL(fileURLWithPath: path))
                } label: {
                    Label("Shared Folder", systemImage: "folder")
                }
                .help("Open the folder shared with this Mac")
            }
            if manager.coplandHaltedIDs.contains(vmID) {
                Button("Continue Copland") { manager.continueCopland(vmID) }.disabled(paused)
            }
            Spacer(minLength: 0)
            Button {
                settingsTab = .general
                showingSettings = true
            } label: {
                Label("Settings", systemImage: "slider.horizontal.3")
            }
            .help("Configure this Mac")
        }
        .controlSize(.large)
        .buttonStyle(.bordered)
    }

    private func machineSummary(_ vm: VMConfig) -> some View {
        HStack(alignment: .top, spacing: 12) {
            summaryItem("Memory", value: "\(vm.ramMB) MB", symbol: "memorychip")
            summaryItem("Hard disk", value: vm.machineFamily == .powerMac7500 ? ByteCountFormatter.string(fromByteCount: Int64((try? vm.diskImageURL.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0), countStyle: .memory) : "\(vm.diskSizeGB) GB", symbol: "internaldrive")
            summaryItem("Display", value: "\(vm.width) × \(vm.height)", symbol: "display")
        }
        .padding(16)
        .frame(maxWidth: .infinity)
        .background(.quaternary.opacity(0.35), in: RoundedRectangle(cornerRadius: 12))
    }

    private func summaryItem(_ title: String, value: String, symbol: String) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Label(title, systemImage: symbol)
                .font(.caption)
                .foregroundStyle(.secondary)
            Text(value)
                .font(.callout.weight(.medium))
                .monospacedDigit()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityElement(children: .combine)
    }

    private func showMedia() {
        MediaController.shared.present(for: vmID)
    }

    // MARK: Settings

    private func settingsSheet(_ vm: Binding<VMConfig>) -> some View {
        VStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 14) {
                HStack {
                    VStack(alignment: .leading, spacing: 3) {
                        Text("Machine Settings")
                            .font(.title2.weight(.semibold))
                        Text(vm.wrappedValue.name)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                    Spacer()
                    if running {
                        Label("Shut down to edit", systemImage: "lock")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
                Picker("Settings category", selection: $settingsTab) {
                    ForEach(SettingsTab.allCases) { tab in
                        Text(tab.rawValue).tag(tab)
                    }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
            }
            .padding(20)
            Divider()
            Form {
                switch settingsTab {
                case .general:
                    Section("Identity") {
                        TextField("Name", text: vm.name)
                            .disabled(running)
                        LabeledContent("Model", value: vm.wrappedValue.machineFamily.hardwareLabel)
                    }
                    if vm.wrappedValue.machineFamily == .powerMac7500 {
                        Section("Copland hardware") {
                            LabeledContent("Memory", value: "32 MB")
                            Text("Uses the tested Power Mac 7500 configuration. Copland's bundled startup disk and firmware are required.")
                        }
                    } else { hardwareSection(vm) }
                case .display:
                    if vm.wrappedValue.machineFamily == .powerMac7500 {
                        Section("Display") {
                            Text("640 × 480, 256 colors in a native window. Use Control-+ and Control-− to scale the view.")
                        }
                    } else {
                        viewingSection(vm)
                        displaySection(vm)
                    }
                case .sharing:
                    if vm.wrappedValue.machineFamily.supportsSharedFolder {
                        sharedFolderSection(vm)
                    } else { Text("Shared folders are unavailable for Copland.") }
                }
            }
            .formStyle(.grouped)
            Divider()
            HStack {
                Text(running ? "Settings are locked while this Mac is running." : "Changes are saved automatically.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Button("Done") { showingSettings = false }
                    .keyboardShortcut(.defaultAction)
            }
            .padding(16)
        }
        .frame(width: 570, height: 510)
    }

    private var statusBadge: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(statusColor)
                .frame(width: 8, height: 8)
            Text(statusText)
                .font(.caption.weight(.semibold))
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
        .foregroundStyle(statusColor)
        .background(statusColor.opacity(0.14), in: Capsule())
        .accessibilityLabel("Status: \(statusText)")
    }

    private var statusColor: Color {
        if manager.coplandHaltedIDs.contains(vmID) { return .orange }
        if paused {
            return .orange
        }
        if running {
            return .green
        }
        return .secondary
    }

    private var statusText: String {
        if manager.coplandHaltedIDs.contains(vmID) { return "Stopped at assertion" }
        if paused {
            return "Paused"
        }
        if running {
            return "Running"
        }
        return "Shut Down"
    }

    // MARK: Screen

    private var browserAccessSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Browser Display", systemImage: "globe")
                .font(.headline)
            if let url = manager.browserURLs[vmID] {
                Text(url.absoluteString)
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .textSelection(.enabled)
                HStack {
                    Button {
                        manager.activate(vmID)
                    } label: {
                        Label("Open in Browser", systemImage: "safari")
                    }
                    Button {
                        NSPasteboard.general.clearContents()
                        NSPasteboard.general.setString(url.absoluteString, forType: .string)
                    } label: {
                        Label("Copy URL", systemImage: "doc.on.doc")
                    }
                }
            } else {
                ProgressView("Preparing the browser display…")
            }
            Text("This private address works only on this Mac and disappears when the virtual Mac shuts down.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
    }

    private func retainUsefulPreview(_ image: NSImage?) {
        guard let image, MachinePreviewQuality.isUseful(image) else { return }
        savedPreview = image
    }

    // MARK: Display

    private func viewingSection(_ vm: Binding<VMConfig>) -> some View {
        Section {
            Toggle(isOn: vm.useBrowserDisplay) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("View in browser (VNC)")
                    Text("Off opens the virtual Mac in its own window")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .accessibilityLabel("View in browser")
            .disabled(running)
        } header: {
            Label("Viewer", systemImage: "macwindow")
        } footer: {
            if vm.wrappedValue.useBrowserDisplay {
                Text("Starts a private VNC display available only on this Mac and opens it in your web browser.")
            } else {
                Text("Starts the virtual Mac in a native window with resize, full-screen, input, and removable-media controls.")
            }
        }
    }

    @ViewBuilder
    private func displaySection(_ vm: Binding<VMConfig>) -> some View {
        if vm.wrappedValue.machineFamily == .powerMacG4 {
            powerMacDisplaySection(vm)
        } else {
            quadraDisplaySection(vm)
        }
    }

    @ViewBuilder
    private func powerMacDisplaySection(_ vm: Binding<VMConfig>) -> some View {
        Section {
            Toggle("Custom resolution", isOn: customResolutionSelection(vm))
                .disabled(running)

            if vm.wrappedValue.customResolution {
                customResolutionFields(vm)
            } else {
                Picker("Resolution", selection: resolutionSelection(vm)) {
                    ForEach(ResolutionPreset.all) { preset in
                        Text(preset.label).tag(preset)
                    }
                }
                .disabled(running)
            }

            Picker("Colors", selection: depthSelection(vm)) {
                ForEach(availableDepths(vm.wrappedValue)) { depth in
                    Text(depth.label).tag(depth)
                }
            }
            .disabled(running)
        } header: {
            Label("Display", systemImage: "display")
        } footer: {
            Text("Thousands reduces framebuffer bandwidth and is usually faster. Other depths remain available in the Monitors control panel.")
        }
    }

    @ViewBuilder
    private func quadraDisplaySection(_ vm: Binding<VMConfig>) -> some View {
        Section {
            Toggle(isOn: enhancedFramebufferSelection(vm)) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Enhanced video card")
                    Text("Any resolution, with richer color at every size")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .accessibilityLabel("Enhanced video card")
            .disabled(running)

            Toggle("Custom resolution", isOn: customResolutionSelection(vm))
                .disabled(running || !vm.wrappedValue.useEnhancedFramebuffer)

            if vm.wrappedValue.customResolution {
                customResolutionFields(vm)
            } else {
                Picker("Resolution", selection: resolutionSelection(vm)) {
                    ForEach(ResolutionPreset.all) { preset in
                        Text(preset.label).tag(preset)
                    }
                }
                .disabled(running)
            }

            Picker("Colors", selection: depthSelection(vm)) {
                ForEach(availableDepths(vm.wrappedValue)) { depth in
                    Text(depth.label).tag(depth)
                }
            }
            .disabled(running)
        } header: {
            Label("Display", systemImage: "display")
        } footer: {
            Text("The deepest color setting available to the Mac. You can pick lower settings inside the Mac, under Monitors.")
        }
    }

    @ViewBuilder
    private func customResolutionFields(_ vm: Binding<VMConfig>) -> some View {
        LabeledContent("Size") {
            HStack(spacing: 8) {
                TextField(
                    "Width",
                    value: bounded(vm.width, minimum: VMConfig.minWidth, maximum: VMConfig.maxWidth),
                    format: .number
                )
                .frame(width: 76)
                .multilineTextAlignment(.trailing)
                Text("\u{00D7}")
                    .foregroundStyle(.secondary)
                TextField(
                    "Height",
                    value: bounded(vm.height, minimum: VMConfig.minHeight, maximum: VMConfig.maxHeight),
                    format: .number
                )
                .frame(width: 76)
                .multilineTextAlignment(.trailing)
                Button("Match Display") {
                    matchMainDisplay(vm)
                }
            }
        }
        .disabled(running)
    }

    private func bounded(_ source: Binding<Int>, minimum: Int, maximum: Int) -> Binding<Int> {
        Binding(
            get: { source.wrappedValue },
            set: { source.wrappedValue = min(max($0, minimum), maximum) }
        )
    }

    private func matchMainDisplay(_ vm: Binding<VMConfig>) {
        guard let screen = NSScreen.main else { return }
        vm.wrappedValue.customResolution = true
        vm.wrappedValue.width = VMConfig.clampedWidth(Int(screen.frame.width))
        vm.wrappedValue.height = VMConfig.clampedHeight(Int(screen.frame.height))
    }

    // MARK: Hardware

    @ViewBuilder
    private func hardwareSection(_ vm: Binding<VMConfig>) -> some View {
        Section {
            Picker("Memory", selection: vm.ramMB) {
                ForEach(memoryChoices(vm.wrappedValue), id: \.self) { mb in
                    Text("\(mb) MB").tag(mb)
                }
            }
            .disabled(running)

            LabeledContent("Hard disk") {
                Text("\(vm.wrappedValue.diskSizeGB) GB")
                    .foregroundStyle(.secondary)
            }

            Toggle(isOn: vm.networking) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Networking")
                    Text("Connect the Mac to the Internet through this Mac")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .accessibilityLabel("Networking")
            .disabled(running)

            Toggle(isOn: vm.sound) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Sound")
                    Text("Play the Mac's sound through your speakers")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .accessibilityLabel("Sound")
            .disabled(running)

            if vm.wrappedValue.machineFamily == .powerMacG4 {
                Toggle(isOn: vm.useG4CPU) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("PowerPC G4 acceleration")
                        Text("Recommended for Mac OS 8.6 and 9. Turn off for Mac OS 8.5 compatibility.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
                .accessibilityLabel("PowerPC G4 acceleration")
                .disabled(running)
            }

            Toggle(isOn: vm.classicInputHelpers) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Secondary click & scrolling")
                    Text("Right-click opens contextual menus, and the scroll wheel scrolls in classic Mac apps. Turn this off if USB Overdrive is installed.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .accessibilityLabel("Secondary click and scrolling")
            .disabled(running)

            Toggle(isOn: vm.tabletInput) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Seamless mouse")
                    Text("Move the pointer freely in and out of the virtual Mac. Games can request captured relative motion.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .accessibilityLabel("Seamless mouse")
            .disabled(running)
        } header: {
            Label("Hardware", systemImage: "memorychip")
        }
    }

    // The family presets, plus the VM's current value if it is nonstandard, so
    // the picker never shows an empty selection.
    private func memoryChoices(_ vm: VMConfig) -> [Int] {
        var choices = vm.machineFamily.ramPresets
        if !choices.contains(vm.ramMB) {
            choices.append(vm.ramMB)
            choices.sort()
        }
        return choices
    }

    // MARK: Shared folder

    @ViewBuilder
    private func sharedFolderSection(_ vm: Binding<VMConfig>) -> some View {
        Section {
            if vm.wrappedValue.hasSharedFolder {
                LabeledContent("Folder") {
                    Text(URL(fileURLWithPath: vm.wrappedValue.sharedFolderPath!).lastPathComponent)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                Button("Stop Sharing", role: .destructive) {
                    vm.wrappedValue.sharedFolderPath = nil
                }
                .disabled(running)
            } else {
                Button("Choose Folder to Share\u{2026}") {
                    chooseSharedFolder(vm)
                }
                .disabled(running)
            }
        } header: {
            Label("Shared Folder", systemImage: "folder.badge.person.crop")
        } footer: {
            sharedFolderFooter(vm.wrappedValue)
        }
    }

    @ViewBuilder
    private func sharedFolderFooter(_ vm: VMConfig) -> some View {
        if vm.hasSharedFolder {
            if vm.machineFamily == .powerMacG4 && vm.bootFromCD && vm.cdImagePath?.isEmpty == false {
                Text("Appears on the Mac desktop as the disk \u{201C}\(vm.sharedVolumeName)\u{201D}. Sharing is off while the Mac starts up from CD.")
            } else {
                Text("Appears on the Mac desktop as the disk \u{201C}\(vm.sharedVolumeName)\u{201D}.")
            }
        } else {
            Text("Share a folder from this Mac so its files appear on your classic Mac's desktop.")
        }
    }

    private func chooseSharedFolder(_ vm: Binding<VMConfig>) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.message = "Choose a folder on this Mac to share with your classic Mac"
        if panel.runModal() == .OK, let url = panel.url {
            vm.wrappedValue.sharedFolderPath = url.path
        }
    }

    // MARK: Toolbar

    @ToolbarContentBuilder
    private func toolbar(_ vm: Binding<VMConfig>) -> some ToolbarContent {
        if vm.wrappedValue.machineFamily == .powerMacG4 {
            ToolbarItem {
                GXMetalStatusMenu(config: vm.wrappedValue, isRunning: running,
                                  isPaused: paused, showTools: showMedia)
            }
        }

        if running {
            ToolbarItemGroup {
                Button {
                    manager.activate(vmID)
                } label: {
                    Label(
                        vm.wrappedValue.useBrowserDisplay ? "Open in Browser" : "Show Window",
                        systemImage: vm.wrappedValue.useBrowserDisplay ? "safari" : "macwindow"
                    )
                }
                .help(
                    vm.wrappedValue.useBrowserDisplay
                        ? "Open the Mac in your preferred web browser"
                        : "Bring the virtual Mac window to the front"
                )

                if paused {
                    Button {
                        manager.resume(vmID)
                    } label: {
                        Label("Resume", systemImage: "play.fill")
                    }
                    .help("Resume the paused Mac")
                } else {
                    Button {
                        manager.pause(vmID)
                    } label: {
                        Label("Pause", systemImage: "pause.fill")
                    }
                    .help("Freeze the Mac in place")
                }

                Button {
                    manager.reboot(vmID)
                } label: {
                    Label("Restart", systemImage: "arrow.clockwise")
                }
                .help("Restart the Mac")

                Button(role: .destructive) {
                    manager.requestStop(vmID)
                } label: {
                    Label("Shut Down", systemImage: "power")
                }
                .help("Ask Mac OS to shut down safely")
            }
        } else {
            ToolbarItem {
                startButton(vm)
                    .disabled(!AppPaths.qemuIsAvailable(for: vm.wrappedValue.machineFamily))
                    .help("Start the Mac")
            }
        }

        if #available(macOS 26.0, *) {
            ToolbarSpacer(.fixed)
        }

        ToolbarItemGroup {
            if vm.wrappedValue.machineFamily != .powerMac7500 {
                Button(action: showMedia) {
                    Label("Media", systemImage: "opticaldisc")
                }
                .help("Manage discs and the startup disk")
            }
            Button {
                settingsTab = .general
                showingSettings = true
            } label: {
                Label("Settings", systemImage: "slider.horizontal.3")
            }
            .help("Configure this Mac")
        }

        ToolbarItem {
            Menu {
                Button("Reveal in Finder") {
                    NSWorkspace.shared.activateFileViewerSelecting([vm.wrappedValue.folder])
                }
                Divider()
                Button("Remove Machine\u{2026}", role: .destructive) {
                    showingDeleteConfirm = true
                }
                .disabled(running)
            } label: {
                Label("More", systemImage: "ellipsis.circle")
            }
        }
    }

    @ViewBuilder
    private func startButton(_ vm: Binding<VMConfig>) -> some View {
        let button = Button {
            manager.start(vm.wrappedValue)
        } label: {
            Label("Start", systemImage: "play.fill")
        }
        if #available(macOS 26.0, *) {
            button.buttonStyle(.glassProminent)
        } else {
            button.buttonStyle(.borderedProminent)
        }
    }

    // MARK: Selection helpers

    private func enhancedFramebufferSelection(_ vm: Binding<VMConfig>) -> Binding<Bool> {
        Binding(
            get: { vm.wrappedValue.useEnhancedFramebuffer },
            set: { enabled in
                vm.wrappedValue.useEnhancedFramebuffer = enabled
                if !enabled {
                    vm.wrappedValue.customResolution = false
                    let preset = ResolutionPreset.closest(
                        toWidth: vm.wrappedValue.width,
                        height: vm.wrappedValue.height
                    )
                    vm.wrappedValue.width = preset.width
                    vm.wrappedValue.height = preset.height
                    clampDepth(vm)
                }
            }
        )
    }

    private func customResolutionSelection(_ vm: Binding<VMConfig>) -> Binding<Bool> {
        Binding(
            get: { vm.wrappedValue.customResolution },
            set: { isCustom in
                vm.wrappedValue.customResolution = isCustom
                if !isCustom {
                    let preset = ResolutionPreset.closest(
                        toWidth: vm.wrappedValue.width,
                        height: vm.wrappedValue.height
                    )
                    vm.wrappedValue.width = preset.width
                    vm.wrappedValue.height = preset.height
                    clampDepth(vm)
                }
            }
        )
    }

    private func resolutionSelection(_ vm: Binding<VMConfig>) -> Binding<ResolutionPreset> {
        Binding(
            get: {
                ResolutionPreset.matching(
                    width: vm.wrappedValue.width,
                    height: vm.wrappedValue.height
                ) ?? ResolutionPreset.closest(
                    toWidth: vm.wrappedValue.width,
                    height: vm.wrappedValue.height
                )
            },
            set: { preset in
                vm.wrappedValue.width = preset.width
                vm.wrappedValue.height = preset.height
                clampDepth(vm)
            }
        )
    }

    private func depthSelection(_ vm: Binding<VMConfig>) -> Binding<ColorDepth> {
        Binding(
            get: { ColorDepth(rawValue: vm.wrappedValue.depth) ?? .thousands },
            set: { vm.wrappedValue.depth = $0.rawValue }
        )
    }

    // The stock framebuffer at 1152x870 only supports 8-bit; the enhanced one is
    // unrestricted. Constrain the available depths accordingly.
    private func availableDepths(_ vm: VMConfig) -> [ColorDepth] {
        if vm.machineFamily == .powerMacG4 {
            return [.thousands, .millions]
        }
        if vm.useEnhancedFramebuffer {
            return ColorDepth.allCases
        }
        if vm.width >= 1152 {
            return [.greys256]
        }
        return ColorDepth.allCases
    }

    private func clampDepth(_ vm: Binding<VMConfig>) {
        let allowed = availableDepths(vm.wrappedValue)
        let current = ColorDepth(rawValue: vm.wrappedValue.depth) ?? .thousands
        if !allowed.contains(current) {
            vm.wrappedValue.depth = allowed.first!.rawValue
        }
    }

}
