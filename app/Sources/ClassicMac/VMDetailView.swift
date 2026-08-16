import SwiftUI
import UniformTypeIdentifiers

struct VMDetailView: View {
    let vmID: UUID
    @EnvironmentObject var store: VMStore
    @EnvironmentObject var manager: QEMUManager

    @State private var config: VMConfig?
    @State private var showingDeleteConfirm = false
    @State private var savedPreview: NSImage?

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
    }

    private func load() {
        config = store.vms.first(where: { $0.id == vmID })
        if let config = config {
            savedPreview = NSImage(contentsOf: config.previewURL)
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
        VStack(spacing: 0) {
            header(vm)
                .padding(.horizontal, 24)
                .padding(.top, 20)
                .padding(.bottom, 12)
            Form {
                if previewImage != nil {
                    screenSection
                }
                displaySection(vm)
                hardwareSection(vm)
                mediaSection(vm)
                if vm.wrappedValue.machineFamily.supportsSharedFolder {
                    sharedFolderSection(vm)
                }
            }
            .formStyle(.grouped)
        }
        .navigationTitle(vm.wrappedValue.name)
        .toolbar { toolbar(vm) }
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

    // MARK: Header

    @ViewBuilder
    private func header(_ vm: Binding<VMConfig>) -> some View {
        HStack(spacing: 16) {
            MachineBadgeView(family: vm.wrappedValue.machineFamily, size: 64)
            VStack(alignment: .leading, spacing: 4) {
                TextField("Name", text: vm.name)
                    .font(.title2.bold())
                    .textFieldStyle(.plain)
                    .disabled(running)
                Text(vm.wrappedValue.machineFamily.hardwareLabel)
                    .foregroundStyle(.secondary)
                HStack(spacing: 14) {
                    Label("\(vm.wrappedValue.ramMB) MB", systemImage: "memorychip")
                    Label("\(vm.wrappedValue.diskSizeGB) GB", systemImage: "internaldrive")
                    Label("\(vm.wrappedValue.width) \u{00D7} \(vm.wrappedValue.height)", systemImage: "display")
                }
                .font(.caption)
                .foregroundStyle(.secondary)
                .padding(.top, 2)
                if running {
                    Text("Settings are locked while this Mac is running.")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }
            }
            Spacer()
            statusBadge
        }
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
        if paused {
            return .orange
        }
        if running {
            return .green
        }
        return .secondary
    }

    private var statusText: String {
        if paused {
            return "Paused"
        }
        if running {
            return "Running"
        }
        return "Shut Down"
    }

    // MARK: Screen

    // The live capture while running; the saved capture from the last run
    // otherwise.
    private var previewImage: NSImage? {
        if let live = manager.previews[vmID] {
            return live
        }
        return savedPreview
    }

    @ViewBuilder
    private var screenSection: some View {
        if let image = previewImage {
            Section {
                Button {
                    if running {
                        manager.activate(vmID)
                    }
                } label: {
                    Image(nsImage: image)
                        .resizable()
                        .scaledToFit()
                        .frame(maxWidth: .infinity)
                        .frame(maxHeight: 210)
                        .saturation(running ? 1 : 0.6)
                        .opacity(running ? 1 : 0.75)
                        .clipShape(RoundedRectangle(cornerRadius: 8))
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .strokeBorder(.separator, lineWidth: 1)
                        )
                        .overlay(alignment: .bottomTrailing) {
                            if running {
                                Label("Open Screen", systemImage: "arrow.up.forward.app")
                                    .font(.caption.weight(.semibold))
                                    .padding(.horizontal, 9)
                                    .padding(.vertical, 6)
                                    .background(.regularMaterial, in: Capsule())
                                    .padding(10)
                            }
                        }
                }
                .buttonStyle(.plain)
                .disabled(!running)
                .help(running ? "Click to open the Mac's window" : "The Mac's screen when it last shut down")
                .listRowBackground(Color.clear)
                .listRowInsets(EdgeInsets())
            } footer: {
                if !running {
                    Text("The Mac's screen when it last shut down.")
                }
            }
        }
    }

    // MARK: Display

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
            Text("Thousands reduces framebuffer bandwidth and is usually faster. While it's running, drag the window to any size and the Mac follows; other depths remain available in the Monitors control panel.")
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
            .disabled(running)

            Toggle(isOn: vm.sound) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Sound")
                    Text("Play the Mac's sound through your speakers")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
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
            .disabled(running)

            Toggle(isOn: vm.tabletInput) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Seamless mouse")
                    Text("Move the pointer freely in and out of the Mac window without releasing it first.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
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

    // MARK: Removable media

    @ViewBuilder
    private func mediaSection(_ vm: Binding<VMConfig>) -> some View {
        Section {
            if let cd = vm.wrappedValue.cdImagePath, !cd.isEmpty {
                LabeledContent("Disc") {
                    Text(discDisplayName(cd))
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                Toggle("Start up from this disc", isOn: vm.bootFromCD)
                    .disabled(running)
                Button("Eject Disc", role: .destructive) {
                    vm.wrappedValue.cdImagePath = nil
                    vm.wrappedValue.bootFromCD = false
                }
                .disabled(running)
            } else {
                LabeledContent("Disc") {
                    Text("No disc inserted")
                        .foregroundStyle(.secondary)
                }
                Button("Insert Disc\u{2026}") {
                    chooseISO(vm)
                }
                .disabled(running)
            }

            if AppPaths.toolsCD != nil {
                Toggle(isOn: vm.toolsCDInserted) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("ClassicMac Tools")
                        if vm.wrappedValue.toolsCDInserted &&
                            vm.wrappedValue.machineFamily == .powerMacG4 &&
                            vm.wrappedValue.bootFromCD &&
                            vm.wrappedValue.cdImagePath?.isEmpty == false {
                            Text("During this Power Mac disc startup, ClassicMac Tools waits until the desktop appears. Then choose Insert “ClassicMac Tools” from the Mac menu.")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        } else {
                            Text("Useful classic Mac apps, including StuffIt Expander, Disk Copy, a disc image mounter, and USB Overdrive for Power Macs. You can insert or eject it from the Mac menu while the Mac is running.")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }
                .disabled(running)
            }

            if vm.wrappedValue.machineFamily.supportsFloppyDisk {
                Divider()

                if let floppy = vm.wrappedValue.floppyImagePath,
                   !floppy.isEmpty {
                    LabeledContent("Floppy disk") {
                        Text(URL(fileURLWithPath: floppy).lastPathComponent)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                    Button("Eject Floppy Disk", role: .destructive) {
                        vm.wrappedValue.floppyImagePath = nil
                    }
                    .disabled(running)
                } else {
                    LabeledContent("Floppy disk") {
                        Text("No floppy inserted")
                            .foregroundStyle(.secondary)
                    }
                    Button("Insert Floppy Disk\u{2026}") {
                        chooseFloppy(vm)
                    }
                    .disabled(running)
                }
            }
        } header: {
            Label("Media", systemImage: "opticaldiscdrive")
        } footer: {
            if vm.wrappedValue.machineFamily.supportsFloppyDisk {
                Text("While the Mac is running, use Mac → Disc or Mac → Floppy to change removable media.")
            } else {
                Text("While the Mac is running, use Mac → Disc to insert or eject a disc image.")
            }
        }
    }

    // Show the bundled Tools CD under a friendly name instead of a raw
    // file name.
    private func discDisplayName(_ path: String) -> String {
        if let toolsCD = AppPaths.toolsCD, toolsCD.path == path {
            return "ClassicMac Tools CD"
        }
        return URL(fileURLWithPath: path).lastPathComponent
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
        if running {
            ToolbarItemGroup {
                Button {
                    manager.activate(vmID)
                } label: {
                    Label("Show Screen", systemImage: "macwindow")
                }
                .help("Bring the Mac's window to the front")

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
                .help("Turn the Mac off immediately")
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

    private func chooseISO(_ vm: Binding<VMConfig>) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.allowedContentTypes = isoContentTypes
        panel.message = "Choose a CD image (.iso, .toast, or .cdr)"
        if panel.runModal() == .OK, let url = panel.url {
            vm.wrappedValue.cdImagePath = url.path
            vm.wrappedValue.bootFromCD = true
        }
    }

    private func chooseFloppy(_ vm: Binding<VMConfig>) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.message = "Choose a raw floppy disk image (.img, .dsk, .ima, or .raw)"
        panel.prompt = "Insert"
        if panel.runModal() == .OK, let url = panel.url {
            vm.wrappedValue.floppyImagePath = url.path
        }
    }

    private var isoContentTypes: [UTType] {
        var types: [UTType] = [.diskImage]
        if let iso = UTType(filenameExtension: "iso") {
            types.append(iso)
        }
        if let toast = UTType(filenameExtension: "toast") {
            types.append(toast)
        }
        if let cdr = UTType(filenameExtension: "cdr") {
            types.append(cdr)
        }
        return types
    }
}
