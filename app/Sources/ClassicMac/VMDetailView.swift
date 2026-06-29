import SwiftUI
import UniformTypeIdentifiers

struct VMDetailView: View {
    let vmID: UUID
    @EnvironmentObject var store: VMStore
    @EnvironmentObject var manager: QEMUManager

    @State private var config: VMConfig?
    @State private var showingDeleteConfirm = false

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

    @ViewBuilder
    private func content(_ vm: Binding<VMConfig>) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                header(vm)
                Divider()
                displaySection(vm)
                hardwareSection(vm)
                mediaSection(vm)
            }
            .padding(24)
            .frame(maxWidth: 640, alignment: .leading)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .navigationTitle(vm.wrappedValue.name)
        .toolbar { toolbar(vm) }
        .confirmationDialog("Delete \(vm.wrappedValue.name)?", isPresented: $showingDeleteConfirm, titleVisibility: .visible) {
            Button("Delete Machine and Disk", role: .destructive) {
                store.delete(vm.wrappedValue)
            }
        } message: {
            Text("This permanently deletes the virtual machine and its hard disk image.")
        }
    }

    @ViewBuilder
    private func header(_ vm: Binding<VMConfig>) -> some View {
        HStack(spacing: 16) {
            Image(systemName: "desktopcomputer")
                .font(.system(size: 44))
                .foregroundStyle(.tint)
            VStack(alignment: .leading, spacing: 4) {
                TextField("Name", text: vm.name)
                    .font(.title2.bold())
                    .textFieldStyle(.plain)
                Text("Macintosh Quadra 800 - Motorola 68040")
                    .foregroundStyle(.secondary)
            }
            Spacer()
            statusBadge
        }
    }

    private var statusBadge: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(running ? Color.green : Color.secondary)
                .frame(width: 9, height: 9)
            Text(running ? "Running" : "Stopped")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
    }

    @ViewBuilder
    private func displaySection(_ vm: Binding<VMConfig>) -> some View {
        SettingsCard(title: "Display", systemImage: "display") {
            Toggle(isOn: vm.useEnhancedFramebuffer) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Enhanced framebuffer")
                    Text("Arbitrary resolutions and Thousands color (nubus-qfb)")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .disabled(running)

            Picker("Resolution", selection: resolutionSelection(vm)) {
                ForEach(ResolutionPreset.all) { preset in
                    Text(preset.label).tag(preset)
                }
            }
            .disabled(running)

            Picker("Color depth", selection: depthSelection(vm)) {
                ForEach(availableDepths(vm.wrappedValue)) { depth in
                    Text(depth.label).tag(depth)
                }
            }
            .disabled(running)
        }
    }

    @ViewBuilder
    private func hardwareSection(_ vm: Binding<VMConfig>) -> some View {
        SettingsCard(title: "Hardware", systemImage: "memorychip") {
            Picker("Memory", selection: vm.ramMB) {
                ForEach(ramPresets, id: \.self) { mb in
                    Text("\(mb) MB").tag(mb)
                }
            }
            .disabled(running)

            LabeledContent("Hard disk") {
                Text("\(vm.wrappedValue.diskSizeGB) GB")
                    .foregroundStyle(.secondary)
            }

            Toggle("User-mode networking", isOn: vm.networking)
                .disabled(running)
        }
    }

    @ViewBuilder
    private func mediaSection(_ vm: Binding<VMConfig>) -> some View {
        SettingsCard(title: "CD-ROM", systemImage: "opticaldiscdrive") {
            if let cd = vm.wrappedValue.cdImagePath, !cd.isEmpty {
                LabeledContent("Disc") {
                    Text(URL(fileURLWithPath: cd).lastPathComponent)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                Toggle("Boot from CD-ROM", isOn: vm.bootFromCD)
                    .disabled(running)
                Button("Eject Disc", role: .destructive) {
                    vm.wrappedValue.cdImagePath = nil
                    vm.wrappedValue.bootFromCD = false
                }
                .disabled(running)
            } else {
                Text("No disc inserted.")
                    .foregroundStyle(.secondary)
                Button("Insert CD/ISO Image...") {
                    chooseISO(vm)
                }
                .disabled(running)
            }
        }
    }

    @ToolbarContentBuilder
    private func toolbar(_ vm: Binding<VMConfig>) -> some ToolbarContent {
        ToolbarItemGroup {
            if running {
                Button {
                    manager.stop(vmID)
                } label: {
                    Label("Stop", systemImage: "stop.fill")
                }
            } else {
                Button {
                    manager.start(vm.wrappedValue)
                } label: {
                    Label("Start", systemImage: "play.fill")
                }
                .disabled(!AppPaths.qemuIsAvailable)
            }

            Menu {
                Button("Reveal Files in Finder") {
                    NSWorkspace.shared.activateFileViewerSelecting([vm.wrappedValue.folder])
                }
                Divider()
                Button("Delete Machine...", role: .destructive) {
                    showingDeleteConfirm = true
                }
                .disabled(running)
            } label: {
                Label("More", systemImage: "ellipsis.circle")
            }
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
        panel.message = "Choose a Mac OS install CD image (.iso / .toast / .cdr)"
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

struct SettingsCard<Content: View>: View {
    let title: String
    let systemImage: String
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label(title, systemImage: systemImage)
                .font(.headline)
            VStack(alignment: .leading, spacing: 12) {
                content
            }
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(.quaternary.opacity(0.4), in: RoundedRectangle(cornerRadius: 10))
        }
    }
}
