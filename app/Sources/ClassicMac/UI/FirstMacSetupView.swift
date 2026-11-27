import AppKit
import SwiftUI
import UniformTypeIdentifiers
import PlatinumKit

/// Stays in the main window through creation/import so the ready screen is
/// not lost when the first machine appears in the library.
struct FirstMacSetupView: View {
    enum Step: Int { case welcome = 1, source, prepare, ready }
    enum Source: Hashable { case download, installer }

    let onFinish: () -> Void
    @ObservedObject private var store = VMStore.shared
    @StateObject private var download = MachineDownloadModel()
    @State private var step = Step.welcome
    @State private var source = Source.download
    @State private var acknowledged = false
    @State private var selection: String?
    @State private var name = ""
    @State private var family = MachineFamily.powerMacG4
    @State private var cd: URL?
    @State private var directory = AppPaths.defaultLibraryDir
    @State private var created: VMConfig?
    @State private var working = false
    @State private var error: String?
    @State private var supportsSparse = false

    private var entries: [LibraryEntry] {
        LibraryEntry.entries(from: download.machines, includeOlder: false)
            .filter { (try? $0.machine.checkCompatibility()) != nil }
    }
    private var selected: LibraryEntry? { entries.first { $0.id == selection } ?? entries.first }
    private var busy: Bool { working || download.isBusy }
    private var validName: Bool { (try? MachineTemplateInstaller.normalizedName(name)) != nil }
    private var readyFromInstaller: Bool { created?.bootFromCD ?? (source == .installer) }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 14) {
                PlatinumIcon(step == .ready ? (created?.machineFamily.iconName ?? "mac") : "powermac_g4", size: 32, scale: 2)
                VStack(alignment: .leading, spacing: 5) {
                    Text(title).platinumText(.large)
                    Text("Step \(step.rawValue) of 4 · Your first classic Mac").platinumText(.detail)
                }
                Spacer(minLength: 0)
            }
            PlatinumSeparator()
            Group {
                if step == .welcome {
                    ViewThatFits(in: .vertical) {
                        stepBody
                        PlatinumScrollView(line: 16) { stepBody }
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                } else {
                    PlatinumScrollView(line: 16) {
                        stepBody
                    }
                }
            }
            PlatinumSeparator()
            footer
        }
        .padding(12)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .onChange(of: entries.map(\.id)) { _, _ in
            if selection == nil || !entries.contains(where: { $0.id == selection }) {
                selection = entries.first?.id
            }
        }
        .onChange(of: selected?.id) { oldID, _ in
            guard source == .download, let selected else { return }
            let oldTitle = entries.first(where: { $0.id == oldID })?.title
            if name == oldTitle || isAutoFilledName(name) {
                name = selected.title
            }
        }
        .onChange(of: store.vms.map(\.id)) { _, _ in
            // File > Open and Finder opens can arrive while setup is showing.
            // Creation/download callbacks set their own ready screen first.
            guard created == nil, !busy,
                  let vm = store.vms.first(where: { $0.id == store.selectedID }) else { return }
            source = vm.bootFromCD ? .installer : .download
            created = vm
            step = .ready
        }
        .task(id: directory) {
            AppPaths.ensureDirectory(directory)
            let target = directory
            supportsSparse = await Task.detached(priority: .utility) {
                MachineTemplateInstaller.supportsSparseFiles(at: target)
            }.value
        }
    }

    private var stepBody: some View {
        VStack(alignment: .leading, spacing: 14) {
            switch step {
            case .welcome: welcome
            case .source: sourceChoice
            case .prepare:
                if source == .download { downloadChoice } else { installerChoice }
            case .ready: ready
            }
            if let message = error ?? download.errorMessage {
                Text(message).platinumText(.small)
                    .foregroundStyle(Platinum.errorText.color)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(4)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func isAutoFilledName(_ value: String) -> Bool {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { return true }
        if MachineFamily.allCases.map(\.defaultName).contains(trimmed) { return true }
        return entries.contains { $0.title == trimmed }
    }

    private var title: String {
        switch step {
        case .welcome: return "Welcome to ClassicMac"
        case .source: return "How would you like to start?"
        case .prepare: return source == .download ? "Choose your first Mac" : "Bring your own Mac OS"
        case .ready: return readyFromInstaller ? "Your Mac is ready to install" : "Your Mac is ready"
        }
    }

    private var welcome: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("This is 30-year-old software.").platinumText(.emphasis)
            Text("ClassicMac brings the classic Macintosh to your computer. We’ll help you download a pre-installed Mac or install Mac OS from your own CD image.")
                .platinumText(.body).fixedSize(horizontal: false, vertical: true)
            PlatinumGroupBox("A quick note about licenses") {
                Text("You are responsible for knowing whether you have the right licenses for Mac OS and any other software you download, install or use. A pre-installed download does not provide those licenses.")
                    .platinumText(.body).fixedSize(horizontal: false, vertical: true)
            }
            PlatinumCheckbox("I understand that I am responsible for the software licenses.", isOn: $acknowledged)
        }
    }

    private var sourceChoice: some View {
        VStack(alignment: .leading, spacing: 16) {
            PlatinumRadioGroup(selection: $source, options: [
                (Source.download, "Download a pre-installed Mac — easiest"),
                (Source.installer, "Install my own copy of Mac OS"),
            ])
            PlatinumGroupBox(source == .download ? "Ready to run" : "Use your own installer") {
                Text(source == .download
                     ? "Choose one of our pre-installed versions. We’ll download it, check it and prepare your Mac. No OS installation is needed."
                     : "Choose a Mac OS CD image (.iso, .toast or .cdr). We’ll create a Mac with the right defaults, then start your installer.")
                    .platinumText(.body).fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: 10) {
                Text("Already have a .classic Mac?").platinumText(.detail)
                PlatinumButton("Open Existing Mac…") {
                    VMStore.shared.presentOpenPanel()
                    if let id = VMStore.shared.selectedID,
                       let vm = VMStore.shared.vms.first(where: { $0.id == id }) {
                        source = vm.bootFromCD ? .installer : .download
                        created = vm
                        step = .ready
                    }
                }
            }
        }
    }

    @ViewBuilder private var downloadChoice: some View {
        if download.isBusy && download.phase != .loadingCatalog {
            DownloadProgressView(model: download, title: name)
            Text("Your Mac will appear here when it is ready. You can pause a download and resume it later.")
                .platinumText(.detail)
        } else if download.phase == .loadingCatalog {
            Text("Finding pre-installed Macs…").platinumText(.body)
            PlatinumProgressBar(value: nil)
        } else if entries.isEmpty {
            Text("The Library is unavailable right now. Try again, or go back to use your own installer.")
                .platinumText(.body)
            PlatinumButton("Try Again") { download.loadCatalog() }
        } else {
            Text("Mac OS is already installed. Choose an edition and we’ll do the rest.").platinumText(.body)
            if let warning = download.partialCatalogWarning {
                HStack(spacing: 6) {
                    PlatinumIcon("caution", size: 16)
                    Text(warning).platinumText(.small).fixedSize(horizontal: false, vertical: true)
                }
            }
            PlatinumPopUp(selection: Binding(get: { selected?.id ?? "" }, set: { selection = $0 }),
                          options: entries.map { ($0.id, $0.title) }, width: 360)
                .disabled(busy)
            if let entry = selected {
                Text(entry.description).platinumText(.small).fixedSize(horizontal: false, vertical: true)
                let required = download.totalRequiredFreeBytes(
                    for: entry.machine,
                    saveFolder: directory,
                    supportsSparse: supportsSparse
                )
                Text("\(ByteCountFormatter.string(fromByteCount: entry.machine.archiveBytes, countStyle: .file)) download · needs \(ByteCountFormatter.string(fromByteCount: required, countStyle: .file)) free")
                    .platinumText(.detail)
            }
            nameAndLocation
            if let entry = selected {
                if download.hasCompletedCache(entry.machine) {
                    Text("Download complete. Choose Create Mac to finish setting up your Mac.")
                        .platinumText(.small)
                } else if download.hasPartialDownload(entry.machine) {
                    Text("Paused. Your download is saved.")
                        .platinumText(.small)
                }
            }
        }
    }

    private var installerChoice: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Choose the model for your Mac OS version. Memory, disk and display are set up for you.")
                .platinumText(.body).fixedSize(horizontal: false, vertical: true)
            PlatinumPopUp(selection: Binding(get: { family }, set: { newFamily in
                if isAutoFilledName(name) {
                    name = newFamily.defaultName
                }
                family = newFamily
            }), options: [
                (MachineFamily.powerMacG4, "Power Mac G4 — Mac OS 8.5 to 9.2.2"),
                (MachineFamily.powerMacOSX, "Power Mac G4 — Mac OS X"),
                (MachineFamily.quadra800, "Quadra 800 — System 7.1 to Mac OS 8.1"),
                (MachineFamily.pippin, "Apple / Bandai Pippin — Pippin OS"),
            ], width: 360)
            HStack(spacing: 8) {
                PlatinumIcon("cd", size: 16)
                Text(cd?.lastPathComponent ?? "Choose your Mac OS installation CD image")
                    .platinumText(.small).lineLimit(1).truncationMode(.middle)
                PlatinumButton("Choose CD…", action: chooseCD)
            }
            nameAndLocation
            Text(working ? "Creating your Mac and copying the CD image…" : "We’ll keep a copy of the CD image in ClassicMac. You’ll complete the Mac OS installation inside your new Mac.")
                .platinumText(.detail).fixedSize(horizontal: false, vertical: true)
            if working { PlatinumProgressBar(value: nil) }
        }
        .disabled(working)
    }

    private var nameAndLocation: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 10) {
                Text("Mac name:").platinumText(.emphasis)
                PlatinumTextField(text: $name, width: 290)
            }
            HStack(spacing: 8) {
                Text("Save in:").platinumText(.small)
                PlatinumIcon("folder", size: 16)
                Text(directory.lastPathComponent).platinumText(.small).lineLimit(1).help(directory.path)
                PlatinumButton("Change…") {
                    let panel = NSOpenPanel()
                    panel.canChooseDirectories = true
                    panel.canChooseFiles = false
                    panel.canCreateDirectories = true
                    panel.directoryURL = directory
                    panel.prompt = "Choose"
                    if panel.runModal() == .OK, let url = panel.url { directory = url }
                }
            }
        }
    }

    private var ready: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(created?.name ?? "Your Mac").platinumText(.emphasis)
            if readyFromInstaller {
                let targetFamily = created?.machineFamily ?? family
                Text("Start the installer, then follow these steps inside Mac OS:").platinumText(.body)
                Text(targetFamily == .powerMacOSX
                     ? "1. In the installer, open Disk Utility and format the blank disk with Apple Partition Map and Mac OS Extended.\n2. Install Mac OS X on that disk.\n3. If the installer opens again, shut down, choose the hard disk as the startup disk in ClassicMac, and start your Mac."
                     : targetFamily == .powerMacG4
                     ? "1. Open Utilities → Drive Setup on the CD and initialize the blank hard disk.\n2. Open Installer. In Options…, turn off Update Apple Hard Disk Drivers, then install Mac OS.\n3. Restart when installation finishes. Your Mac will start from its hard disk."
                     : targetFamily == .pippin
                     ? "1. Ensure your 4 MB Kinka boot ROM is configured in Get Info or placed as pippin.rom inside ClassicMac's ROMs folder.\n2. Start your Pippin to boot straight from the selected disc.\n3. Saved games and settings persist automatically in Save Flash (128 KB)."
                     : "1. Use the disk utility on your CD to initialize the blank hard disk.\n2. Run the Mac OS installer and follow its instructions.\n3. Shut down when finished, eject the CD from Media, then start your Mac.")
                    .platinumText(.small).fixedSize(horizontal: false, vertical: true)
            } else {
                Text("Everything is in place. Start your Mac to go straight to the classic desktop.")
                    .platinumText(.body).fixedSize(horizontal: false, vertical: true)
            }
            Text("You can change settings later with Get Info and add software with Media. When you’re done, choose Shut Down inside Mac OS.")
                .platinumText(.detail).fixedSize(horizontal: false, vertical: true)
        }
    }

    private var prepareButtonTitle: String {
        guard source == .download else { return "Create Mac" }
        guard let machine = selected?.machine else { return "Download Mac" }
        if download.hasCompletedCache(machine) { return "Create Mac" }
        if download.hasPartialDownload(machine) { return "Resume Download" }
        return "Download Mac"
    }

    private var footer: some View {
        HStack(spacing: 12) {
            if step != .welcome && step != .ready {
                PlatinumButton("Back") {
                    error = nil
                    step = step == .prepare ? .source : .welcome
                }.disabled(busy)
            }
            Spacer(minLength: 0)
            if step == .ready {
                PlatinumButton("Later") { onFinish() }
                PlatinumButton(readyFromInstaller ? "Start Installer" : "Start Mac", role: .default) {
                    guard let created else { return }
                    onFinish()
                    QEMUManager.shared.start(created)
                }.disabled(created == nil || !AppPaths.qemuIsAvailable(for: created?.machineFamily ?? .powerMacG4))
            } else if step == .prepare {
                PlatinumButton(prepareButtonTitle, role: .default) {
                    error = nil
                    if source == .download { installDownload() } else { createInstallerMac() }
                }
                .disabled(busy || !validName || (source == .download ? selected == nil : cd == nil))
            } else {
                PlatinumButton("Continue", role: .default) {
                    if step == .welcome {
                        guard acknowledged else { return }
                        step = .source
                    } else {
                        step = .prepare
                        if isAutoFilledName(name) {
                            name = source == .download ? (selected?.title ?? "") : family.defaultName
                        }
                        if source == .download && download.machines.isEmpty { download.loadCatalog() }
                    }
                }.disabled(step == .welcome && !acknowledged)
            }
        }
    }

    private func chooseCD() {
        let panel = NSOpenPanel()
        let exts = family == .pippin ? ["iso", "bin", "cue", "toast", "cdr"] : ["iso", "toast", "cdr"]
        panel.allowedContentTypes = exts.compactMap { UTType(filenameExtension: $0) } + [.diskImage]
        panel.message = family == .pippin
            ? "Choose a Pippin CD disc image (.iso, .bin, .cue, .toast or .cdr)"
            : "Choose your Mac OS installation CD image"
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if panel.runModal() == .OK { cd = panel.url }
    }

    private func installDownload() {
        guard let entry = selected else { return }
        download.install(entry.machine, name: name, directory: directory) { url in
            let store = VMStore.shared
            store.openBundle(at: url, autostart: false)
            guard let id = store.selectedID, var vm = store.vms.first(where: { $0.id == id }) else {
                error = "Your Mac was downloaded, but it could not be opened."
                return
            }
            if entry.graphicsCard == .atiRadeon && vm.graphicsCard != .atiRadeon && vm.machineFamily == .powerMacG4 {
                vm.graphicsCard = .atiRadeon
                let size = ATIDisplay.size(fitting: vm.width, height: vm.height)
                vm.width = size.width
                vm.height = size.height
                guard store.save(vm) else {
                    error = "Your Mac was downloaded, but its settings could not be saved."
                    return
                }
            }
            created = vm
            step = .ready
        }
    }

    private func createInstallerMac() {
        guard let cd else { return }
        working = true
        let needsRawConvert = family == .pippin && (
            cd.pathExtension.lowercased() == "bin" ||
            cd.pathExtension.lowercased() == "cue" ||
            PippinMachine.isRaw2352CDImage(at: cd)
        )
        let suggested = needsRawConvert ? "\(cd.deletingPathExtension().lastPathComponent).iso" : cd.lastPathComponent
        let destination = VMStore.uniqueFileURL(in: AppPaths.mediaDir, suggestedName: suggested)
        Task {
            do {
                try await Task.detached(priority: .userInitiated) {
                    if needsRawConvert {
                        try PippinMachine.convertRawBinToISO(from: cd, to: destination)
                    } else {
                        try FileManager.default.copyItem(at: cd, to: destination)
                    }
                }.value
                let preset = ResolutionPreset.recommended
                let trimmed = try MachineTemplateInstaller.normalizedName(name)
                let dims = family == .pippin ? PippinVideoOut.ntsc.dimensions : (preset.width, preset.height)
                let config = VMConfig(name: trimmed, machineFamily: family,
                    ramMB: family.defaultRAMMB, diskSizeGB: family.defaultDiskSizeGB,
                    width: dims.0, height: dims.1, depth: ColorDepth.thousands.rawValue,
                    useEnhancedFramebuffer: family.supportsEnhancedFramebuffer,
                    cdImagePath: destination.path, bootFromCD: true, networking: family != .pippin,
                    sound: family.supportsSound, useG4CPU: family == .powerMacG4,
                    pippinVideoOut: family == .pippin ? .ntsc : nil,
                    flashImageName: family == .pippin ? "flash.img" : nil,
                    bundleURL: VMStore.shared.uniqueBundleURL(in: directory, name: trimmed))
                if let vm = VMStore.shared.createVM(config) {
                    created = vm
                    step = .ready
                } else {
                    try? FileManager.default.removeItem(at: destination)
                    error = "Your Mac could not be created. Try a different name or save location."
                }
            } catch {
                try? FileManager.default.removeItem(at: destination)
                self.error = error.localizedDescription
            }
            working = false
        }
    }
}
