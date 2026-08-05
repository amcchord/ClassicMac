import SwiftUI
import UniformTypeIdentifiers

struct NewVMSheet: View {
    var onCreate: (VMConfig) -> Bool

    @Environment(\.dismiss) private var dismiss
    @EnvironmentObject private var store: VMStore

    @State private var step: SetupStep = .machine
    @State private var family: MachineFamily = .quadra800
    @State private var name = MachineFamily.quadra800.defaultName
    @State private var ramMB = MachineFamily.quadra800.defaultRAMMB
    @State private var diskSizeGB = MachineFamily.quadra800.defaultDiskSizeGB
    @State private var useEnhancedFramebuffer = true
    @State private var resolution = ResolutionPreset.recommended
    @State private var depth = ColorDepth.thousands
    @State private var customResolution = false
    @State private var customWidth = ResolutionPreset.recommended.width
    @State private var customHeight = ResolutionPreset.recommended.height
    @State private var sound = true

    @State private var saveFolder: URL = AppPaths.defaultLibraryDir
    @State private var isoURL: URL?
    @State private var copyISOIntoLibrary = true
    @State private var sharedFolderURL: URL?

    @State private var working = false
    @State private var workingMessage = ""
    @State private var errorMessage: String?

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            stepContent
            Divider()
            footer
        }
        .frame(width: 620, height: 620)
        .overlay {
            if working {
                workingOverlay
            }
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 3) {
                    Text("New Machine")
                        .font(.title2.bold())
                    Text(step.subtitle)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Text("Step \(step.rawValue + 1) of \(SetupStep.allCases.count)")
                    .font(.caption.weight(.medium))
                    .foregroundStyle(.secondary)
            }

            HStack(spacing: 8) {
                ForEach(SetupStep.allCases) { item in
                    Button {
                        step = item
                    } label: {
                        Label(item.title, systemImage: item.systemImage)
                            .font(.caption.weight(.semibold))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 7)
                            .foregroundStyle(step == item ? Color.accentColor : Color.secondary)
                            .background(
                                step == item ? Color.accentColor.opacity(0.12) : Color.clear,
                                in: Capsule()
                            )
                    }
                    .buttonStyle(.plain)
                    .disabled(item.rawValue > step.rawValue && !hasValidName)
                }
            }
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 20)
    }

    @ViewBuilder
    private var stepContent: some View {
        switch step {
        case .machine:
            machineForm
        case .hardware:
            hardwareForm
        case .media:
            mediaForm
        }
    }

    private var machineForm: some View {
        Form {
            Section("Mac model") {
                HStack(spacing: 12) {
                    ForEach(MachineFamily.allCases) { candidate in
                        MachineTile(family: candidate, selected: family == candidate) {
                            family = candidate
                        }
                    }
                }
                .listRowBackground(Color.clear)
                .listRowInsets(EdgeInsets())
            }

            Section {
                TextField("Name", text: $name, prompt: Text(family.defaultName))

                LabeledContent("Save in") {
                    Text(saveFolder.path(percentEncoded: false))
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                        .help(saveFolder.path(percentEncoded: false))
                }
                Button("Choose Folder\u{2026}") {
                    chooseSaveFolder()
                }
            } header: {
                Text("Machine file")
            } footer: {
                Text("Creates \u{201C}\(bundleFileName)\u{201D}, a portable machine containing its settings and hard disk.")
            }
        }
        .formStyle(.grouped)
        .onChange(of: family) { _, newFamily in
            applyFamilyDefaults(newFamily)
        }
    }

    private var hardwareForm: some View {
        Form {
            Section {
                Picker("Memory", selection: $ramMB) {
                    ForEach(family.ramPresets, id: \.self) { mb in
                        Text("\(mb) MB").tag(mb)
                    }
                }
                Picker("Hard disk", selection: $diskSizeGB) {
                    ForEach(family.diskSizePresets, id: \.self) { gb in
                        Text("\(gb) GB").tag(gb)
                    }
                }
                Toggle("Sound", isOn: $sound)
            } header: {
                Label("Hardware", systemImage: "memorychip")
            } footer: {
                if family == .powerMacG4 {
                    Text("Mac OS 9 is most stable with less than 1 GB of memory, so presets stop at 896 MB.")
                }
            }

            Section {
                if family.supportsEnhancedFramebuffer {
                    Toggle(isOn: $useEnhancedFramebuffer) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Enhanced video card")
                            Text("Flexible sizes and richer color modes")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }

                Toggle("Custom resolution", isOn: $customResolution)
                    .disabled(family.supportsEnhancedFramebuffer && !useEnhancedFramebuffer)

                if customResolution {
                    resolutionFields
                } else {
                    Picker("Resolution", selection: $resolution) {
                        ForEach(ResolutionPreset.all) { preset in
                            Text(preset.label).tag(preset)
                        }
                    }
                }

                if family.supportsEnhancedFramebuffer {
                    Picker("Colors", selection: $depth) {
                        ForEach(availableDepths) { candidate in
                            Text(candidate.label).tag(candidate)
                        }
                    }
                } else {
                    LabeledContent("Colors") {
                        Text("Millions")
                            .foregroundStyle(.secondary)
                    }
                }
            } header: {
                Label("Display", systemImage: "display")
            } footer: {
                Text(displayFooter)
            }
        }
        .formStyle(.grouped)
        .onChange(of: useEnhancedFramebuffer) { _, isEnabled in
            if !isEnabled {
                customResolution = false
            }
            clampDepth()
        }
        .onChange(of: resolution) { clampDepth() }
        .onChange(of: customWidth) { clampDepth() }
    }

    private var resolutionFields: some View {
        LabeledContent("Size") {
            HStack(spacing: 8) {
                TextField(
                    "Width",
                    value: bounded($customWidth, minimum: VMConfig.minWidth, maximum: VMConfig.maxWidth),
                    format: .number
                )
                .frame(width: 76)
                .multilineTextAlignment(.trailing)
                Text("\u{00D7}")
                    .foregroundStyle(.secondary)
                TextField(
                    "Height",
                    value: bounded($customHeight, minimum: VMConfig.minHeight, maximum: VMConfig.maxHeight),
                    format: .number
                )
                .frame(width: 76)
                .multilineTextAlignment(.trailing)
                Button("Match Display") {
                    matchMainDisplay()
                }
            }
        }
    }

    private var mediaForm: some View {
        Form {
            Section {
                if let isoURL {
                    LabeledContent("Disc") {
                        Text(isoURL.lastPathComponent)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                            .help(isoURL.path(percentEncoded: false))
                    }
                    Toggle("Keep a copy in ClassicMac", isOn: $copyISOIntoLibrary)
                    HStack {
                        Button("Choose Another\u{2026}") {
                            chooseISO()
                        }
                        Button("Remove", role: .destructive) {
                            self.isoURL = nil
                        }
                    }
                } else {
                    Button("Choose Install Disc\u{2026}") {
                        chooseISO()
                    }
                    Text("Optional. You can insert a disc from the machine settings later.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            } header: {
                Label("Installation", systemImage: "opticaldiscdrive")
            } footer: {
                if isoURL != nil {
                    Text("The new Mac will start from this disc so you can install Mac OS.")
                }
            }

            Section {
                if let sharedFolderURL {
                    LabeledContent("Folder") {
                        Text(sharedFolderURL.lastPathComponent)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .help(sharedFolderURL.path(percentEncoded: false))
                    }
                    HStack {
                        Button("Choose Another\u{2026}") {
                            chooseSharedFolder()
                        }
                        Button("Stop Sharing", role: .destructive) {
                            self.sharedFolderURL = nil
                        }
                    }
                } else {
                    Button("Choose Folder to Share\u{2026}") {
                        chooseSharedFolder()
                    }
                    Text("Optional. The folder appears as a disk on the classic Mac desktop.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            } header: {
                Label("Shared Folder", systemImage: "folder.badge.person.crop")
            }

            Section {
                LabeledContent("Machine") {
                    Text(family.label)
                }
                LabeledContent("Hardware") {
                    Text("\(ramMB) MB memory \u{2022} \(diskSizeGB) GB disk")
                }
                LabeledContent("Display") {
                    Text(displaySummary)
                }
            } header: {
                Label("Ready to Create", systemImage: "checkmark.circle")
            }
        }
        .formStyle(.grouped)
    }

    private var footer: some View {
        HStack(spacing: 10) {
            if let errorMessage {
                Label(errorMessage, systemImage: "exclamationmark.triangle.fill")
                    .font(.caption)
                    .foregroundStyle(.red)
                    .lineLimit(2)
            }

            if step != .machine {
                Button("Back") {
                    moveStep(by: -1)
                }
                .disabled(working)
            }

            Spacer()

            Button("Cancel") {
                dismiss()
            }
            .keyboardShortcut(.cancelAction)
            .disabled(working)

            if step == .media {
                Button("Create Machine") {
                    create()
                }
                .keyboardShortcut(.defaultAction)
                .buttonStyle(.borderedProminent)
                .disabled(!hasValidName || working)
            } else {
                Button("Continue") {
                    moveStep(by: 1)
                }
                .keyboardShortcut(.defaultAction)
                .buttonStyle(.borderedProminent)
                .disabled(!hasValidName || working)
            }
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 16)
    }

    private var workingOverlay: some View {
        ZStack {
            Color.black.opacity(0.25)
            VStack(spacing: 12) {
                ProgressView()
                Text(workingMessage)
                    .font(.callout)
            }
            .padding(24)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 12))
        }
        .contentShape(Rectangle())
    }

    private var hasValidName: Bool {
        !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    private var availableDepths: [ColorDepth] {
        guard !useEnhancedFramebuffer else { return ColorDepth.allCases }
        return resolution.width >= 1152 ? [.greys256] : ColorDepth.allCases
    }

    private var bundleFileName: String {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        return "\(trimmed.isEmpty ? "Machine" : trimmed).\(VMConfig.packageExtension)"
    }

    private var displayFooter: String {
        if family == .powerMacG4 {
            return "The Mac starts at this size in millions of colors. Drag its window after startup to resize the guest display."
        }
        return "This is the startup size and deepest available color mode. Lower modes remain available in the Monitors control panel."
    }

    private var displaySummary: String {
        let size = customResolution ? "\(customWidth) \u{00D7} \(customHeight)" : resolution.label
        if family == .powerMacG4 {
            return "\(size) \u{2022} Millions"
        }
        return "\(size) \u{2022} \(depth.label)"
    }

    private func moveStep(by offset: Int) {
        guard let next = SetupStep(rawValue: step.rawValue + offset) else { return }
        errorMessage = nil
        step = next
    }

    // Reset fields with model-specific defaults while preserving a custom name.
    private func applyFamilyDefaults(_ newFamily: MachineFamily) {
        let defaultNames = MachineFamily.allCases.map(\.defaultName)
        if defaultNames.contains(name) {
            name = newFamily.defaultName
        }
        ramMB = newFamily.defaultRAMMB
        diskSizeGB = newFamily.defaultDiskSizeGB
        useEnhancedFramebuffer = newFamily.supportsEnhancedFramebuffer
        customResolution = false
        resolution = .recommended
        depth = .thousands
        sound = newFamily.supportsSound
    }

    private func bounded(_ source: Binding<Int>, minimum: Int, maximum: Int) -> Binding<Int> {
        Binding(
            get: { source.wrappedValue },
            set: { source.wrappedValue = min(max($0, minimum), maximum) }
        )
    }

    private func chooseSaveFolder() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.directoryURL = saveFolder
        panel.message = "Choose where to save this machine"
        panel.prompt = "Choose"
        if panel.runModal() == .OK, let url = panel.url {
            saveFolder = url
        }
    }

    private func chooseSharedFolder() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.message = "Choose a folder on this Mac to share with your classic Mac"
        panel.prompt = "Share"
        if panel.runModal() == .OK {
            sharedFolderURL = panel.url
        }
    }

    private func matchMainDisplay() {
        guard let screen = NSScreen.main else { return }
        customWidth = VMConfig.clampedWidth(Int(screen.frame.width))
        customHeight = VMConfig.clampedHeight(Int(screen.frame.height))
    }

    private func clampDepth() {
        if !availableDepths.contains(depth), let fallback = availableDepths.first {
            depth = fallback
        }
    }

    private func chooseISO() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.allowedContentTypes = isoContentTypes
        panel.message = "Choose a CD image (.iso, .toast, or .cdr)"
        panel.prompt = "Choose"
        if panel.runModal() == .OK {
            isoURL = panel.url
        }
    }

    private var isoContentTypes: [UTType] {
        var types: [UTType] = [.diskImage]
        for ext in ["iso", "toast", "cdr"] {
            if let type = UTType(filenameExtension: ext) {
                types.append(type)
            }
        }
        return types
    }

    private func create() {
        errorMessage = nil

        let width = customResolution ? VMConfig.clampedWidth(customWidth) : resolution.width
        let height = customResolution ? VMConfig.clampedHeight(customHeight) : resolution.height
        let trimmedName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        let bundleURL = store.uniqueBundleURL(in: saveFolder, name: trimmedName)

        let config = VMConfig(
            name: trimmedName,
            machineFamily: family,
            ramMB: ramMB,
            diskSizeGB: diskSizeGB,
            width: width,
            height: height,
            depth: depth.rawValue,
            useEnhancedFramebuffer: useEnhancedFramebuffer,
            customResolution: customResolution,
            cdImagePath: isoURL?.path,
            bootFromCD: isoURL != nil,
            networking: true,
            sound: sound,
            sharedFolderPath: sharedFolderURL?.path,
            bundleURL: bundleURL
        )

        guard let source = isoURL, copyISOIntoLibrary else {
            if onCreate(config) {
                dismiss()
            }
            return
        }

        working = true
        workingMessage = "Copying \(source.lastPathComponent)\u{2026}"
        let destination = VMStore.uniqueFileURL(
            in: AppPaths.mediaDir,
            suggestedName: source.lastPathComponent
        )
        Task {
            let copyError = await copyFile(from: source, to: destination)
            await MainActor.run {
                working = false
                if let copyError {
                    errorMessage = copyError
                    return
                }
                var finalConfig = config
                finalConfig.cdImagePath = destination.path
                if onCreate(finalConfig) {
                    dismiss()
                }
            }
        }
    }

    private func copyFile(from source: URL, to destination: URL) async -> String? {
        await Task.detached(priority: .userInitiated) {
            do {
                try FileManager.default.copyItem(at: source, to: destination)
                return nil
            } catch {
                return "The install disc could not be copied. \(error.localizedDescription)"
            }
        }.value
    }
}

private enum SetupStep: Int, CaseIterable, Identifiable {
    case machine
    case hardware
    case media

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .machine: return "Machine"
        case .hardware: return "Hardware"
        case .media: return "Install & Share"
        }
    }

    var subtitle: String {
        switch self {
        case .machine: return "Choose a Mac model and where to keep it."
        case .hardware: return "Tune memory, storage, and display settings."
        case .media: return "Add installation media and review your choices."
        }
    }

    var systemImage: String {
        switch self {
        case .machine: return "macpro.gen1"
        case .hardware: return "memorychip"
        case .media: return "opticaldiscdrive"
        }
    }
}

// A selectable card for choosing the machine family, showing the machine
// itself instead of a dropdown of model numbers.
private struct MachineTile: View {
    let family: MachineFamily
    let selected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 14) {
                MachineBadgeView(family: family, size: 52)
                VStack(alignment: .leading, spacing: 3) {
                    Text(family.label)
                        .font(.headline)
                    Text(family.cpuLabel)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text(family.osSupportLabel)
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
                Spacer(minLength: 0)
                if selected {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.tint)
                        .accessibilityHidden(true)
                }
            }
            .frame(maxWidth: .infinity, minHeight: 74)
            .padding(.horizontal, 14)
            .background(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(selected ? Color.accentColor.opacity(0.1) : Color(nsColor: .quaternarySystemFill))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .strokeBorder(selected ? Color.accentColor : Color.clear, lineWidth: 2)
            )
            .contentShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selected ? .isSelected : [])
    }
}
