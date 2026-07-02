import SwiftUI
import UniformTypeIdentifiers

struct NewVMSheet: View {
    var onCreate: (VMConfig) -> Void

    @Environment(\.dismiss) private var dismiss

    @State private var family: MachineFamily = .quadra800
    @State private var name = MachineFamily.quadra800.defaultName
    @State private var ramMB = MachineFamily.quadra800.defaultRAMMB
    @State private var diskSizeGB = MachineFamily.quadra800.defaultDiskSizeGB
    @State private var useEnhancedFramebuffer = true
    @State private var resolution = ResolutionPreset.all[2]
    @State private var depth = ColorDepth.thousands
    @State private var customResolution = false
    @State private var customWidth = 1024
    @State private var customHeight = 768
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
            form
            Divider()
            footer
        }
        .frame(width: 560)
        .overlay {
            if working {
                workingOverlay
            }
        }
    }

    private var header: some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading) {
                Text("New Machine")
                    .font(.headline)
                Text("Set up a classic Macintosh.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            Spacer()
        }
        .padding(20)
    }

    private var form: some View {
        Form {
            Section {
                HStack(spacing: 12) {
                    ForEach(MachineFamily.allCases) { f in
                        MachineTile(family: f, selected: family == f) {
                            family = f
                        }
                    }
                }
                .listRowBackground(Color.clear)
                .listRowInsets(EdgeInsets())
                TextField("Name", text: $name)
            }

            Section("Location") {
                LabeledContent("Save in") {
                    Text(saveFolder.path)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                Button("Choose Folder...") {
                    chooseSaveFolder()
                }
                Text("Creates \u{201C}\(bundleFileName)\u{201D}, a self-contained machine file you can move anywhere.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Section("Hardware") {
                Picker("Memory", selection: $ramMB) {
                    ForEach(family.ramPresets, id: \.self) { mb in
                        Text("\(mb) MB").tag(mb)
                    }
                }
                Picker("Hard disk size", selection: $diskSizeGB) {
                    ForEach(family.diskSizePresets, id: \.self) { gb in
                        Text("\(gb) GB").tag(gb)
                    }
                }
                Toggle("Sound", isOn: $sound)
                if family == .powerMacG4 {
                    Text("Mac OS 9 needs less than 1 GB of memory for stable sound; the presets stop at 896 MB.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Section("Display") {
                if family.supportsEnhancedFramebuffer {
                    Toggle("Enhanced video card", isOn: $useEnhancedFramebuffer)
                    Toggle("Custom resolution", isOn: $customResolution)
                        .disabled(!useEnhancedFramebuffer)
                } else if family.supportsCustomResolution {
                    Toggle("Custom resolution", isOn: $customResolution)
                }
                if customResolution && family.supportsCustomResolution {
                    HStack(spacing: 8) {
                        TextField("Width", value: $customWidth, format: .number)
                            .frame(width: 76)
                            .multilineTextAlignment(.trailing)
                        Text("x")
                            .foregroundStyle(.secondary)
                        TextField("Height", value: $customHeight, format: .number)
                            .frame(width: 76)
                            .multilineTextAlignment(.trailing)
                        Button("Match Display") {
                            matchMainDisplay()
                        }
                    }
                } else {
                    Picker("Resolution", selection: $resolution) {
                        ForEach(ResolutionPreset.all) { preset in
                            Text(preset.label).tag(preset)
                        }
                    }
                }
                if family.supportsEnhancedFramebuffer {
                    Picker("Color depth", selection: $depth) {
                        ForEach(availableDepths) { d in
                            Text(d.label).tag(d)
                        }
                    }
                } else {
                    Text("The Power Mac display starts at millions of colors. Once Mac OS is running you can drag the window to any size, or pick lower depths via the Monitors control panel.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Section("Installation") {
                if let isoURL = isoURL {
                    LabeledContent("Install CD") {
                        Text(isoURL.lastPathComponent)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                    Toggle("Keep a copy of the disc in ClassicMac", isOn: $copyISOIntoLibrary)
                    Button("Choose a different disc...") {
                        chooseISO()
                    }
                } else {
                    Button("Choose Install Disc\u{2026}") {
                        chooseISO()
                    }
                    Text("The Mac starts up from this disc so you can install Mac OS.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            if family.supportsSharedFolder {
                Section("Shared Folder") {
                    if let sharedFolderURL = sharedFolderURL {
                        LabeledContent("Folder") {
                            Text(sharedFolderURL.lastPathComponent)
                                .foregroundStyle(.secondary)
                                .lineLimit(1)
                                .truncationMode(.middle)
                        }
                        Button("Remove") {
                            self.sharedFolderURL = nil
                        }
                    } else {
                        Button("Share a Folder from My Mac...") {
                            chooseSharedFolder()
                        }
                        Text("Optional. The folder appears as a disk on the Mac desktop.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .onChange(of: useEnhancedFramebuffer) { clampDepth() }
        .onChange(of: resolution) { clampDepth() }
        .onChange(of: family) { _, newFamily in applyFamilyDefaults(newFamily) }
    }

    // Reset the fields that have per-family defaults, but keep a name the user
    // typed themselves.
    private func applyFamilyDefaults(_ newFamily: MachineFamily) {
        let defaultNames = MachineFamily.allCases.map { $0.defaultName }
        if defaultNames.contains(name) {
            name = newFamily.defaultName
        }
        ramMB = newFamily.defaultRAMMB
        diskSizeGB = newFamily.defaultDiskSizeGB
        if !newFamily.supportsSharedFolder {
            sharedFolderURL = nil
        }
        if !newFamily.supportsCustomResolution {
            customResolution = false
        }
    }

    private var footer: some View {
        HStack {
            if let errorMessage = errorMessage {
                Label(errorMessage, systemImage: "exclamationmark.triangle.fill")
                    .font(.caption)
                    .foregroundStyle(.red)
                    .lineLimit(2)
            }
            Spacer()
            Button("Cancel") {
                dismiss()
            }
            .keyboardShortcut(.cancelAction)
            Button("Create") {
                create()
            }
            .keyboardShortcut(.defaultAction)
            .buttonStyle(.borderedProminent)
            .disabled(name.trimmingCharacters(in: .whitespaces).isEmpty || working)
        }
        .padding(20)
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
    }

    private var availableDepths: [ColorDepth] {
        if useEnhancedFramebuffer {
            return ColorDepth.allCases
        }
        var activeWidth = resolution.width
        if customResolution {
            activeWidth = customWidth
        }
        if activeWidth >= 1152 {
            return [.greys256]
        }
        return ColorDepth.allCases
    }

    private var bundleFileName: String {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        let base = trimmed.isEmpty ? "Machine" : trimmed
        return "\(base).\(VMConfig.packageExtension)"
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
        panel.message = "Choose a folder on your Mac to share with the emulated Mac"
        if panel.runModal() == .OK {
            sharedFolderURL = panel.url
        }
    }

    private func matchMainDisplay() {
        guard let screen = NSScreen.main else { return }
        var width = Int(screen.frame.width)
        var height = Int(screen.frame.height)
        if width > VMConfig.maxWidth {
            width = VMConfig.maxWidth
        }
        if height > VMConfig.maxHeight {
            height = VMConfig.maxHeight
        }
        customWidth = width
        customHeight = height
    }

    private func clampDepth() {
        if !availableDepths.contains(depth) {
            depth = availableDepths.first!
        }
    }

    private func chooseISO() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        var types: [UTType] = [.diskImage]
        if let iso = UTType(filenameExtension: "iso") { types.append(iso) }
        if let toast = UTType(filenameExtension: "toast") { types.append(toast) }
        if let cdr = UTType(filenameExtension: "cdr") { types.append(cdr) }
        panel.allowedContentTypes = types
        if panel.runModal() == .OK {
            isoURL = panel.url
        }
    }

    private func create() {
        errorMessage = nil

        var width = resolution.width
        var height = resolution.height
        if customResolution && family.supportsCustomResolution {
            width = customWidth
            if width > VMConfig.maxWidth {
                width = VMConfig.maxWidth
            }
            height = customHeight
            if height > VMConfig.maxHeight {
                height = VMConfig.maxHeight
            }
        }

        let trimmedName = name.trimmingCharacters(in: .whitespaces)
        let bundleURL = VMStore.shared.uniqueBundleURL(in: saveFolder, name: trimmedName)

        var effectiveSound = sound
        if !family.supportsSound {
            effectiveSound = false
        }
        var effectiveSharedFolder = sharedFolderURL?.path
        if !family.supportsSharedFolder {
            effectiveSharedFolder = nil
        }

        let config = VMConfig(
            name: trimmedName,
            machineFamily: family,
            ramMB: ramMB,
            diskSizeGB: diskSizeGB,
            width: width,
            height: height,
            depth: depth.rawValue,
            useEnhancedFramebuffer: useEnhancedFramebuffer && family.supportsEnhancedFramebuffer,
            customResolution: customResolution && family.supportsCustomResolution,
            cdImagePath: isoURL?.path,
            bootFromCD: isoURL != nil,
            networking: true,
            sound: effectiveSound,
            sharedFolderPath: effectiveSharedFolder,
            bundleURL: bundleURL
        )

        let needsCopy = isoURL != nil && copyISOIntoLibrary
        if !needsCopy {
            onCreate(config)
            dismiss()
            return
        }

        working = true
        workingMessage = "Copying install disc..."
        let source = isoURL!
        Task {
            let destination = AppPaths.mediaDir.appendingPathComponent(source.lastPathComponent)
            let copyError = await copyFile(from: source, to: destination)
            await MainActor.run {
                working = false
                if let copyError = copyError {
                    errorMessage = copyError
                    return
                }
                var finalConfig = config
                finalConfig.cdImagePath = destination.path
                onCreate(finalConfig)
                dismiss()
            }
        }
    }

    private func copyFile(from source: URL, to destination: URL) async -> String? {
        await Task.detached(priority: .userInitiated) {
            let fm = FileManager.default
            if fm.fileExists(atPath: destination.path) {
                return nil
            }
            do {
                try fm.copyItem(at: source, to: destination)
                return nil
            } catch {
                return error.localizedDescription
            }
        }.value
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
            VStack(spacing: 8) {
                MachineBadgeView(family: family, size: 56)
                Text(family.label)
                    .font(.headline)
                Text(family.osSupportLabel)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity)
            .padding(.vertical, 14)
            .padding(.horizontal, 8)
            .background(
                RoundedRectangle(cornerRadius: 12)
                    .fill(selected ? Color.accentColor.opacity(0.1) : Color(nsColor: .quaternarySystemFill))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 12)
                    .strokeBorder(selected ? Color.accentColor : Color.clear, lineWidth: 2)
            )
            .contentShape(RoundedRectangle(cornerRadius: 12))
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selected ? .isSelected : [])
    }
}
