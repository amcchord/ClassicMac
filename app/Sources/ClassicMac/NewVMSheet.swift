import SwiftUI
import UniformTypeIdentifiers

struct NewVMSheet: View {
    var onCreate: (VMConfig) -> Void

    @Environment(\.dismiss) private var dismiss

    @State private var name = "Mac OS 8.1"
    @State private var ramMB = 128
    @State private var diskSizeGB = 2
    @State private var useEnhancedFramebuffer = true
    @State private var resolution = ResolutionPreset.all[2]
    @State private var depth = ColorDepth.thousands
    @State private var customResolution = false
    @State private var customWidth = 1024
    @State private var customHeight = 768
    @State private var sound = false

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
        .frame(width: 520)
        .overlay {
            if working {
                workingOverlay
            }
        }
    }

    private var header: some View {
        HStack(spacing: 12) {
            Image(systemName: "desktopcomputer")
                .font(.system(size: 30))
                .foregroundStyle(.tint)
            VStack(alignment: .leading) {
                Text("New Quadra 800")
                    .font(.headline)
                Text("Configure a new classic Macintosh.")
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
                TextField("Name", text: $name)
            }

            Section("Hardware") {
                Picker("Memory", selection: $ramMB) {
                    ForEach(ramPresets, id: \.self) { mb in
                        Text("\(mb) MB").tag(mb)
                    }
                }
                Picker("Hard disk size", selection: $diskSizeGB) {
                    ForEach([1, 2, 4, 8], id: \.self) { gb in
                        Text("\(gb) GB").tag(gb)
                    }
                }
                Toggle("Sound", isOn: $sound)
            }

            Section("Display") {
                Toggle("Enhanced framebuffer", isOn: $useEnhancedFramebuffer)
                Toggle("Custom resolution", isOn: $customResolution)
                    .disabled(!useEnhancedFramebuffer)
                if customResolution {
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
                Picker("Color depth", selection: $depth) {
                    ForEach(availableDepths) { d in
                        Text(d.label).tag(d)
                    }
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
                    Toggle("Copy disc into ClassicMac library", isOn: $copyISOIntoLibrary)
                    Button("Choose a different disc...") {
                        chooseISO()
                    }
                } else {
                    Button("Choose Install CD (.iso)...") {
                        chooseISO()
                    }
                    Text("Boot from this disc to format the disk and install Mac OS.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

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
        .formStyle(.grouped)
        .onChange(of: useEnhancedFramebuffer) { _ in clampDepth() }
        .onChange(of: resolution) { _ in clampDepth() }
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

    private func chooseSharedFolder() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.message = "Choose a folder on your Mac to share with the guest"
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
        if customResolution {
            width = customWidth
            if width > VMConfig.maxWidth {
                width = VMConfig.maxWidth
            }
            height = customHeight
            if height > VMConfig.maxHeight {
                height = VMConfig.maxHeight
            }
        }

        let config = VMConfig(
            name: name.trimmingCharacters(in: .whitespaces),
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
            sharedFolderPath: sharedFolderURL?.path
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
