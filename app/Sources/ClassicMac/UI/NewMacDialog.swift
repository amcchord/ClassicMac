import AppKit
import SwiftUI
import UniformTypeIdentifiers
import PlatinumKit

/// Create a Mac to install Mac OS on from your own CD image.
struct NewMacDialog: View {
    static let size = NSSize(width: 500, height: 372)

    private final class CloseGuard: ObservableObject {
        var canClose = true
    }

    let handle: DialogHandle
    @ObservedObject private var store = VMStore.shared
    @StateObject private var closeGuard = CloseGuard()
    @State private var family: MachineFamily = .powerMacG4
    @State private var name = MachineFamily.powerMacG4.defaultName
    @State private var ramMB = MachineFamily.powerMacG4.defaultRAMMB
    @State private var diskSizeGB = MachineFamily.powerMacG4.defaultDiskSizeGB
    @State private var isoURL: URL?
    @State private var copyISO = true
    @State private var saveFolder = AppPaths.defaultLibraryDir
    @State private var working = false
    @State private var errorMessage: String?

    private var validName: Bool {
        (try? MachineTemplateInstaller.normalizedName(name)) != nil
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .top, spacing: 12) {
                PlatinumIcon(family.iconName, size: 32)
                VStack(alignment: .leading, spacing: 3) {
                    Text("Create a Mac and install Mac OS from your own CD image.").platinumText(.emphasis)
                    Text("For a ready-to-run Mac, use the Mac OS Library instead.").platinumText(.detail)
                }
            }
            PlatinumSeparator()
            Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 9) {
                GridRow {
                    Text("Model:").platinumText(.emphasis).gridColumnAlignment(.trailing)
                    PlatinumPopUp(selection: familyBinding, options: [
                        (MachineFamily.powerMacG4, "Power Mac G4 — Mac OS 8.5 to 9.2.2"),
                        (MachineFamily.powerMacOSX, "Power Mac G4 — Mac OS X"),
                        (MachineFamily.quadra800, "Quadra 800 — System 7.1 to Mac OS 8.1"),
                    ], width: 300)
                }
                GridRow {
                    Text("Name:").platinumText(.emphasis)
                    PlatinumTextField(text: $name, width: 300)
                }
                GridRow {
                    Text("Memory:").platinumText(.emphasis)
                    HStack(spacing: 16) {
                        PlatinumPopUp(selection: $ramMB, options: family.ramPresets.map { ($0, "\($0) MB") }, width: 104)
                        Text("Hard disk:").platinumText(.emphasis)
                        PlatinumPopUp(selection: $diskSizeGB,
                                      options: family.diskSizePresets.map { ($0, "\($0) GB") }, width: 90)
                    }
                }
                GridRow(alignment: .top) {
                    Text("Install CD:").platinumText(.emphasis)
                    VStack(alignment: .leading, spacing: 6) {
                        HStack(spacing: 8) {
                            PlatinumIcon("cd", size: 16, dimmed: isoURL == nil)
                            Text(isoURL?.lastPathComponent ?? "None chosen")
                                .platinumText(isoURL == nil ? .detail : .body)
                                .lineLimit(1).truncationMode(.middle)
                                .frame(maxWidth: 200, alignment: .leading)
                            PlatinumButton("Choose…", action: chooseISO)
                        }
                        PlatinumCheckbox("Copy the CD image into ClassicMac", isOn: $copyISO)
                            .disabled(isoURL == nil)
                    }
                }
                GridRow {
                    Text("Save in:").platinumText(.emphasis)
                    HStack(spacing: 8) {
                        PlatinumIcon("folder", size: 16)
                        Text(saveFolder.lastPathComponent).platinumText(.body).lineLimit(1)
                            .frame(maxWidth: 200, alignment: .leading)
                            .help(saveFolder.path)
                        PlatinumButton("Choose…", action: chooseFolder)
                    }
                }
            }
            .disabled(working)
            Spacer(minLength: 0)
            if working {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Copying \(isoURL?.lastPathComponent ?? "the CD image")…").platinumText(.small)
                    PlatinumProgressBar(value: nil)
                }
            } else if let errorMessage {
                HStack(alignment: .top, spacing: 8) {
                    PlatinumIcon("stop", size: 16)
                    Text(errorMessage).platinumText(.small).fixedSize(horizontal: false, vertical: true)
                }
            } else {
                Text(isoURL == nil
                     ? "Without a CD the Mac starts with a blank disk. Insert a CD later from Media."
                     : "The Mac starts from the CD. Once Mac OS is installed it starts from its hard disk.")
                    .platinumText(.detail)
            }
            HStack(spacing: Platinum.buttonGap) {
                Spacer()
                PlatinumButton("Cancel", role: .cancel) {
                    guard !working else { return }
                    handle.close()
                }
                .disabled(working)
                PlatinumButton("Create", role: .default, action: create)
                    .disabled(working || !validName)
            }
        }
        .padding(12)
        .frame(width: Self.size.width, height: Self.size.height)
        .background(Platinum.face.color)
        .onAppear { [closeGuard] in
            closeGuard.canClose = !working
            handle.window?.shouldClose = { [weak closeGuard] in closeGuard?.canClose ?? true }
        }
        .onDisappear {
            handle.window?.shouldClose = nil
        }
    }

    private var familyBinding: Binding<MachineFamily> {
        Binding(
            get: { family },
            set: { newFamily in
                if MachineFamily.allCases.map(\.defaultName).contains(name) { name = newFamily.defaultName }
                family = newFamily
                ramMB = newFamily.defaultRAMMB
                diskSizeGB = newFamily.defaultDiskSizeGB
            }
        )
    }

    private func chooseISO() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        var types: [UTType] = [.diskImage]
        for ext in ["iso", "toast", "cdr"] { if let t = UTType(filenameExtension: ext) { types.append(t) } }
        panel.allowedContentTypes = types
        panel.message = "Choose a Mac OS CD image (.iso, .toast or .cdr)"
        panel.prompt = "Choose"
        if panel.runModal() == .OK { isoURL = panel.url }
    }

    private func chooseFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.directoryURL = saveFolder
        panel.message = "Choose where to save this Mac"
        panel.prompt = "Choose"
        if panel.runModal() == .OK, let url = panel.url { saveFolder = url }
    }

    private func create() {
        errorMessage = nil
        let trimmed: String
        do {
            trimmed = try MachineTemplateInstaller.normalizedName(name)
        } catch {
            errorMessage = error.localizedDescription
            return
        }
        let preset = ResolutionPreset.recommended
        let config = VMConfig(
            name: trimmed,
            machineFamily: family,
            ramMB: ramMB,
            diskSizeGB: diskSizeGB,
            width: preset.width,
            height: preset.height,
            depth: ColorDepth.thousands.rawValue,
            useEnhancedFramebuffer: family.supportsEnhancedFramebuffer,
            cdImagePath: isoURL?.path,
            bootFromCD: isoURL != nil,
            networking: true,
            sound: family.supportsSound,
            useG4CPU: family == .powerMacG4,
            bundleURL: store.uniqueBundleURL(in: saveFolder, name: trimmed)
        )
        guard let source = isoURL, copyISO else {
            finish(config)
            return
        }
        working = true
        closeGuard.canClose = false
        let destination = VMStore.uniqueFileURL(in: AppPaths.mediaDir, suggestedName: source.lastPathComponent)
        Task {
            let failure: String? = await Task.detached(priority: .userInitiated) {
                do {
                    try FileManager.default.copyItem(at: source, to: destination)
                    return nil
                } catch {
                    try? FileManager.default.removeItem(at: destination)
                    return "The CD image could not be copied. \(error.localizedDescription)"
                }
            }.value
            if let failure {
                closeGuard.canClose = true
                working = false
                errorMessage = failure
                return
            }
            var copied = config
            copied.cdImagePath = destination.path
            finish(copied, copiedMedia: destination)
            closeGuard.canClose = true
            working = false
        }
    }

    private func finish(_ config: VMConfig, copiedMedia: URL? = nil) {
        if let created = store.createVM(config) {
            store.selectedID = created.id
            closeGuard.canClose = true
            working = false
            handle.close()
        } else {
            if let copiedMedia {
                try? FileManager.default.removeItem(at: copiedMedia)
            }
            if errorMessage == nil {
                errorMessage = "The Mac could not be created."
            }
        }
    }
}
