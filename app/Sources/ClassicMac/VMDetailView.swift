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
        guard config != nil else { return nil }
        return Binding(
            get: { config! },
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
                Text(vm.wrappedValue.machineFamily.hardwareLabel)
                    .foregroundStyle(.secondary)
                if running {
                    Text("Settings can be changed after the Mac shuts down.")
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
                        .frame(maxHeight: 300)
                        .saturation(running ? 1 : 0.6)
                        .opacity(running ? 1 : 0.75)
                        .clipShape(RoundedRectangle(cornerRadius: 8))
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .strokeBorder(.separator, lineWidth: 1)
                        )
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
            Toggle("Custom resolution", isOn: vm.customResolution)
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
        } header: {
            Label("Display", systemImage: "display")
        } footer: {
            Text("The Mac starts at this size, in millions of colors. While it's running, drag the window to any size and the Mac follows, or pick any depth from Black & White up to millions in the Monitors control panel.")
        }
    }

    @ViewBuilder
    private func quadraDisplaySection(_ vm: Binding<VMConfig>) -> some View {
        Section {
            Toggle(isOn: vm.useEnhancedFramebuffer) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Enhanced video card")
                    Text("Any resolution, with richer color at every size")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .disabled(running)

            Toggle("Custom resolution", isOn: vm.customResolution)
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
                TextField("Width", value: maxClamped(vm.width, VMConfig.maxWidth), format: .number)
                    .frame(width: 76)
                    .multilineTextAlignment(.trailing)
                Text("\u{00D7}")
                    .foregroundStyle(.secondary)
                TextField("Height", value: maxClamped(vm.height, VMConfig.maxHeight), format: .number)
                    .frame(width: 76)
                    .multilineTextAlignment(.trailing)
                Button("Match Display") {
                    matchMainDisplay(vm)
                }
            }
        }
        .disabled(running)
    }

    private func maxClamped(_ source: Binding<Int>, _ maxValue: Int) -> Binding<Int> {
        Binding(
            get: { source.wrappedValue },
            set: { newValue in
                var value = newValue
                if value > maxValue {
                    value = maxValue
                }
                if value < 1 {
                    value = 1
                }
                source.wrappedValue = value
            }
        )
    }

    private func matchMainDisplay(_ vm: Binding<VMConfig>) {
        guard let screen = NSScreen.main else { return }
        var width = Int(screen.frame.width)
        var height = Int(screen.frame.height)
        if width > VMConfig.maxWidth {
            width = VMConfig.maxWidth
        }
        if height > VMConfig.maxHeight {
            height = VMConfig.maxHeight
        }
        vm.wrappedValue.customResolution = true
        vm.wrappedValue.width = width
        vm.wrappedValue.height = height
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

            Toggle(isOn: vm.classicInputHelpers) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Right-click & scroll wheel helpers")
                    Text("Right-click opens contextual menus (Control+click) and the scroll wheel scrolls via arrow keys. Turn off if USB Overdrive is installed in this Mac.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .disabled(running)

            Toggle(isOn: vm.tabletInput) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Tablet input (seamless mouse)")
                    Text("The mouse moves freely in and out of the Mac window without capturing. Uses the classicvirtio tablet driver.")
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

    // MARK: CD-ROM

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
                        Text("Tools CD")
                        if vm.wrappedValue.machineFamily == .powerMacG4 &&
                            vm.wrappedValue.bootFromCD &&
                            vm.wrappedValue.cdImagePath?.isEmpty == false &&
                            vm.wrappedValue.networking {
                            Text("With networking enabled, the Tools tray starts empty during a Power Mac CD boot for Mac OS 9 compatibility. After the desktop appears, insert it from the Machine menu.")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        } else {
                            Text("Guest essentials in a second CD drive: StuffIt Expander, Disk Copy, a CD image mounter, and (Power Mac) the USB Overdrive scroll wheel driver. Can also be inserted from the Machine menu while the Mac is running.")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }
                .disabled(running)
            }
        } header: {
            Label("CD-ROM", systemImage: "opticaldiscdrive")
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
            Text("Share a folder from this Mac so its files appear on the emulated Mac's desktop.")
        }
    }

    private func chooseSharedFolder(_ vm: Binding<VMConfig>) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.message = "Choose a folder on your Mac to share with the emulated Mac"
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
                    manager.stop(vmID)
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

    private func resolutionSelection(_ vm: Binding<VMConfig>) -> Binding<ResolutionPreset> {
        Binding(
            get: {
                let match = ResolutionPreset.all.first { $0.width == vm.wrappedValue.width && $0.height == vm.wrappedValue.height }
                if let match = match {
                    return match
                }
                return ResolutionPreset.all[2]
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
