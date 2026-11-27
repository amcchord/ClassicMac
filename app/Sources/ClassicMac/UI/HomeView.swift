import AppKit
import SwiftUI
import PlatinumKit

extension MachineFamily {
    /// The Platinum icon for this model.
    var iconName: String {
        switch self {
        case .quadra800: return "quadra_800"
        case .powerMacG4, .powerMacOSX: return "powermac_g4"
        case .powerMac7500: return "powermac_7500"
        case .pippin: return "apple_pippin"
        }
    }
}

@MainActor
private enum TemplateOSCache {
    private static var entries: [String: (id: UUID, osVersion: String?)] = [:]

    static func osVersion(for vm: VMConfig) -> String? {
        let key = vm.folder.standardizedFileURL.path
        if let cached = entries[key], cached.id == vm.id {
            return cached.osVersion
        }
        let version = VMTemplateMetadata.load(from: vm.folder)?.osVersion
        entries[key] = (vm.id, version)
        return version
    }

    static func osLabel(for version: String, family: MachineFamily) -> String {
        let trimmed = version.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return family.osSupportLabel }
        guard trimmed.first?.isNumber == true else { return trimmed }
        if family == .powerMacOSX || trimmed.hasPrefix("10.") { return "Mac OS X \(trimmed)" }
        if trimmed.hasPrefix("7.") { return "System \(trimmed)" }
        return "Mac OS \(trimmed)"
    }
}

/// The main window: the Macs on this computer and the selected Mac.
struct HomeView: View {
    @ObservedObject private var store = VMStore.shared
    @ObservedObject private var manager = QEMUManager.shared
    @State private var settingUp = VMStore.shared.vms.isEmpty
    @State private var dismissSetupOnShowMain = false

    var body: some View {
        Group {
            if settingUp || store.vms.isEmpty {
                FirstMacSetupView {
                    dismissSetupOnShowMain = false
                    settingUp = false
                }
                .onAppear {
                    if store.vms.isEmpty { settingUp = true }
                }
            } else {
                HStack(alignment: .top, spacing: 14) {
                    machineColumn
                    if let id = store.selectedID, let vm = store.vms.first(where: { $0.id == id }) {
                        MachinePanel(vm: vm)
                            .id(vm.id)
                    } else {
                        nothingSelected
                    }
                }
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Platinum.face.color)
        .onAppear(perform: selectFirstIfNeeded)
        .onChange(of: store.vms.map(\.id)) { oldIDs, newIDs in
            selectFirstIfNeeded()
            if settingUp && newIDs.count > oldIDs.count && !newIDs.isEmpty {
                if WindowManager.shared.libraryWindowRef?.isVisible == true ||
                    NSApp.keyWindow !== WindowManager.shared.mainWindow {
                    dismissSetupOnShowMain = true
                }
            }
            if newIDs.isEmpty {
                dismissSetupOnShowMain = false
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { note in
            guard let window = note.object as? PlatinumWindow,
                  window === WindowManager.shared.mainWindow else { return }
            if dismissSetupOnShowMain && !store.vms.isEmpty {
                dismissSetupOnShowMain = false
                settingUp = false
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: WindowManager.didShowMainNotification)) { _ in
            if dismissSetupOnShowMain && !store.vms.isEmpty {
                dismissSetupOnShowMain = false
                settingUp = false
            }
        }
    }

    private func selectFirstIfNeeded() {
        if store.selectedID == nil || !store.vms.contains(where: { $0.id == store.selectedID }) {
            store.selectedID = store.vms.first?.id
        }
    }

    private var machineColumn: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Macs on this computer:").platinumText(.emphasis)
            PlatinumList(store.vms, selection: $store.selectedID, rowHeight: 40,
                         onDoubleClick: { vm in startOrShow(vm) }) { vm, _ in
                let isRunning = manager.isRunning(vm.id)
                let isPaused = manager.isPaused(vm.id)
                MachineRow(vm: vm, running: isRunning, paused: isPaused,
                           halted: manager.coplandHaltedIDs.contains(vm.id))
                    .contextMenu {
                        Button(isRunning ? (isPaused ? "Resume and Show Mac" : "Show Mac") : "Start Up") {
                            store.selectedID = vm.id
                            startOrShow(vm)
                        }
                        .disabled(!isRunning && !AppPaths.qemuIsAvailable(for: vm.machineFamily))
                        Divider()
                        Button("Get Info…") {
                            store.selectedID = vm.id
                            WindowManager.shared.presentSettings(for: vm.id)
                        }
                        Button("Checkpoints…") {
                            store.selectedID = vm.id
                            WindowManager.shared.presentCheckpoints(for: vm.id)
                        }
                        Button("Media…") {
                            store.selectedID = vm.id
                            MediaController.shared.present(for: vm.id)
                        }
                        .disabled(vm.machineFamily == .powerMac7500)
                        Button("Show in Finder") {
                            NSWorkspace.shared.activateFileViewerSelecting([vm.folder])
                        }
                        Divider()
                        Button("Delete Mac…") {
                            store.selectedID = vm.id
                            WindowManager.shared.confirmRemove(vm)
                        }
                    }
            }
            HStack(spacing: 6) {
                PlatinumButton("New Mac…") { WindowManager.shared.presentNewMac() }
                PlatinumButton("Library…") { WindowManager.shared.showLibrary() }
                Spacer(minLength: 0)
                PlatinumButton("Delete…") {
                    if let id = store.selectedID, let vm = store.vms.first(where: { $0.id == id }) {
                        WindowManager.shared.confirmRemove(vm)
                    }
                }
                .disabled(store.selectedID == nil)
            }
        }
        .frame(width: 262)
    }

    private var nothingSelected: some View {
        VStack {
            Spacer()
            Text("Select a Mac to see it here.").platinumText(.detail)
            Spacer()
        }
        .frame(maxWidth: .infinity)
    }

    private func startOrShow(_ vm: VMConfig) {
        if manager.isRunning(vm.id) {
            if manager.isPaused(vm.id) { manager.resume(vm.id) }
            manager.activate(vm.id)
        } else if AppPaths.qemuIsAvailable(for: vm.machineFamily) {
            manager.start(vm)
        }
    }
}

/// One Mac in the list: its model icon, name and what it is doing.
struct MachineRow: View {
    let vm: VMConfig
    let running: Bool
    let paused: Bool
    let halted: Bool

    var body: some View {
        HStack(spacing: 8) {
            PlatinumIcon(vm.machineFamily.iconName, size: 32)
                .overlay(alignment: .bottomTrailing) {
                    if running { StatusLight(color: halted || paused ? RGB(0xFF8600) : RGB(0x00A600)) }
                }
            VStack(alignment: .leading, spacing: 1) {
                Text(vm.name).platinumText(.emphasis).lineLimit(1)
                Text(detail).platinumText(.detail).lineLimit(1)
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 5)
        .accessibilityElement(children: .combine)
    }

    private var detail: String {
        if halted { return "Stopped at an assertion" }
        if paused { return "Paused" }
        if running { return "Running now" }
        if let os = TemplateOSCache.osVersion(for: vm) {
            let label = TemplateOSCache.osLabel(for: os, family: vm.machineFamily)
            return "\(label) · \(vm.machineFamily.label)"
        }
        return vm.machineFamily.label
    }
}

/// A tiny LED, as drawn on Platinum icons: black ring, colored lamp, a glint.
struct StatusLight: View {
    let color: RGB

    var body: some View {
        PixelCanvas { p, _ in
            p.frame(PixelRect(0, 0, 7, 7), Platinum.black)
            p.fill(1, 1, 5, 5, color)
            p.pixel(2, 2, Platinum.white)
        }
        .frame(width: 7, height: 7)
        .offset(x: 2, y: 1)
    }
}

/// The selected Mac: its screen, what it is, and what you can do with it.
struct MachinePanel: View {
    let vm: VMConfig
    @ObservedObject private var manager = QEMUManager.shared
    @State private var savedPreview: NSImage?

    private var running: Bool { manager.isRunning(vm.id) }
    private var paused: Bool { manager.isPaused(vm.id) }
    private var halted: Bool { manager.coplandHaltedIDs.contains(vm.id) }
    private var copland: Bool { vm.machineFamily == .powerMac7500 }
    private var templateOS: String? { TemplateOSCache.osVersion(for: vm) }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            ScreenPreview(vm: vm, image: savedPreview, running: running, paused: paused) {
                if running { manager.activate(vm.id) }
            }
            HStack(alignment: .firstTextBaseline) {
                Text(vm.name).platinumText(.emphasis).lineLimit(1)
                Spacer(minLength: 8)
                Text(statusText)
                    .font(.platinumSmall)
                    .foregroundStyle(halted ? Platinum.errorText.color : Platinum.dimText.color)
            }
            aboutGroup
            contextual
            Spacer(minLength: 0)
            lifecycleButtons
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .onAppear {
            retain(NSImage(contentsOf: vm.previewURL))
            retain(manager.previews[vm.id])
        }
        .onChange(of: manager.previews[vm.id]) { _, image in retain(image) }
    }

    private func retain(_ image: NSImage?) {
        guard let image, MachinePreviewQuality.isUseful(image) else { return }
        savedPreview = image
    }

    private var statusText: String {
        if halted { return "Stopped at an assertion" }
        if paused { return "Paused" }
        if running { return "Running now" }
        return "Shut down"
    }

    // MARK: About This Mac

    private var aboutGroup: some View {
        PlatinumGroupBox("About This Mac") {
            HStack(alignment: .top, spacing: 12) {
                Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 3) {
                    infoRow("Model:", vm.machineFamily.hardwareLabel)
                    infoRow("Mac OS:", osText)
                    infoRow("Memory:", "\(vm.ramMB) MB")
                    if vm.machineFamily == .pippin {
                        infoRow("Save Flash:", "128 KB")
                        infoRow("Video:", vm.effectivePippinVideoOut.label)
                    } else {
                        infoRow("Hard disk:", diskText)
                        infoRow("Screen:", screenText)
                    }
                    if vm.machineFamily == .powerMacG4 {
                        infoRow("Graphics:", graphicsText)
                    }
                    if vm.machineFamily != .pippin {
                        infoRow("Sharing:", sharingText)
                        infoRow("Printing:", printingText)
                    }
                }
                Spacer(minLength: 0)
                VStack(alignment: .trailing, spacing: 10) {
                    PlatinumButton("Get Info…", minWidth: 86) { WindowManager.shared.presentSettings(for: vm.id) }
                    PlatinumButton("Checkpoints…", minWidth: 86) { WindowManager.shared.presentCheckpoints(for: vm.id) }
                    if !copland {
                        PlatinumButton("Media…", minWidth: 86) { MediaController.shared.present(for: vm.id) }
                    }
                    PlatinumButton("Show in Finder", minWidth: 86) {
                        NSWorkspace.shared.activateFileViewerSelecting([vm.folder])
                    }
                }
            }
        }
    }

    private func infoRow(_ label: String, _ value: String) -> some View {
        GridRow {
            Text(label).platinumText(.small).gridColumnAlignment(.trailing)
            Text(value).platinumText(.small).lineLimit(1).truncationMode(.middle)
        }
    }

    private var osText: String {
        if let os = templateOS { return TemplateOSCache.osLabel(for: os, family: vm.machineFamily) }
        return vm.machineFamily.osSupportLabel
    }

    private var diskText: String {
        if copland {
            let bytes = (try? vm.diskImageURL.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
            return ByteCountFormatter.string(fromByteCount: Int64(bytes), countStyle: .memory)
        }
        return "\(vm.diskSizeGB) GB"
    }

    private var screenText: String {
        if copland { return "640 × 480, 256 colors" }
        if vm.graphicsCard == .atiRadeon {
            let size = ATIDisplay.size(fitting: vm.width, height: vm.height)
            return "\(size.width) × \(size.height)"
        }
        let depth = ColorDepth(rawValue: vm.depth)?.label ?? "Thousands"
        return "\(vm.width) × \(vm.height), \(depth)"
    }

    private var graphicsText: String {
        let card = QEMUManager.effectiveGraphicsCard(for: vm)
        guard running, card == .atiRadeon else {
            if vm.graphicsCard == .atiRadeon && card == .standard {
                return "Standard while starting from a CD"
            }
            return vm.graphicsCard.longLabel
        }
        switch manager.atiRenderers[vm.id] {
        case nil: return "ATI Radeon"
        case "": return "ATI Radeon (3D unavailable)"
        case let renderer?: return "ATI Radeon, 3D on \(renderer)"
        }
    }

    private var sharingText: String {
        if !vm.machineFamily.supportsSharedFolder { return "Unavailable" }
        guard let path = vm.sharedFolderPath, !path.isEmpty else { return "Off" }
        return "\u{201C}\(URL(fileURLWithPath: path).lastPathComponent)\u{201D}"
    }

    private var printingText: String {
        if !vm.machineFamily.supportsPrinting { return "Unavailable" }
        if !vm.printingEnabled { return "Off" }
        if vm.printerName == HostPrinterManager.pdfTargetValue { return "Save as PDF" }
        if let name = vm.printerName, !name.isEmpty { return name }
        return "System Default"
    }

    // MARK: Context

    @ViewBuilder
    private var contextual: some View {
        if halted {
            HStack(alignment: .top, spacing: 10) {
                PlatinumIcon("caution", size: 16)
                Text("Copland stopped at a developer assertion. Continue asks its debugger to resume; the affected feature may still fail.")
                    .platinumText(.small)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 0)
                PlatinumButton("Continue Copland") { manager.continueCopland(vm.id) }
                    .disabled(paused)
            }
        } else if running && vm.useBrowserDisplay {
            PlatinumGroupBox("Browser Display") {
                VStack(alignment: .leading, spacing: 8) {
                    if let url = manager.browserURLs[vm.id] {
                        Text(url.absoluteString).platinumText(.small).lineLimit(1).truncationMode(.middle)
                            .textSelection(.enabled)
                        HStack(spacing: 10) {
                            PlatinumButton("Open in Browser") { manager.activate(vm.id) }
                            PlatinumButton("Copy Address") {
                                NSPasteboard.general.clearContents()
                                NSPasteboard.general.setString(url.absoluteString, forType: .string)
                            }
                        }
                    } else {
                        PlatinumProgressBar(value: nil)
                    }
                    Text("This private address works only on this computer and ends when the Mac shuts down.")
                        .platinumText(.detail)
                }
            }
        } else if vm.machineFamily == .powerMacG4 && vm.bootFromCD && vm.cdImagePath?.isEmpty == false {
            PlatinumGroupBox("Installing Mac OS") {
                VStack(alignment: .leading, spacing: 4) {
                    InstallStep(number: 1, text: "Open Utilities → Drive Setup on the CD, select the blank hard disk and choose Initialize.")
                    InstallStep(number: 2, text: "In Installer, open Options… and turn off Update Apple Hard Disk Drivers, then install.")
                    InstallStep(number: 3, text: "Restart. Once Mac OS is installed, the Mac starts from its hard disk by itself.")
                }
            }
        } else if vm.machineFamily == .pippin && !running && PippinMachine.resolveROMURL(for: vm) == nil {
            HStack(alignment: .top, spacing: 8) {
                PlatinumIcon("caution", size: 16)
                Text("Select a 4 MB Pippin Kinka boot ROM in Get Info… (or place pippin.rom inside this bundle) before starting.")
                    .platinumText(.detail)
                    .fixedSize(horizontal: false, vertical: true)
            }
        } else if copland {
            Text("Control-G captures or releases the mouse. Control-+ and Control-− scale the window; Control-F toggles full screen.")
                .platinumText(.detail)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    // MARK: Buttons

    private var lifecycleButtons: some View {
        HStack(spacing: Platinum.buttonGap) {
            if running {
                PlatinumButton(paused ? "Resume" : "Pause") {
                    if paused { manager.resume(vm.id) } else { manager.pause(vm.id) }
                }
                PlatinumButton("Restart") { manager.reboot(vm.id) }
                PlatinumButton("Shut Down…") { manager.requestStop(vm.id) }
                Spacer(minLength: 0)
                PlatinumButton(vm.useBrowserDisplay ? "Open in Browser" : "Show Mac", role: .default, minWidth: 90) {
                    if paused { manager.resume(vm.id) }
                    manager.activate(vm.id)
                }
            } else {
                if !AppPaths.qemuIsAvailable(for: vm.machineFamily) {
                    Text("This copy of ClassicMac is missing its emulator.").platinumText(.detail)
                }
                Spacer(minLength: 0)
                PlatinumButton("Start Up", role: .default, minWidth: 90) { manager.start(vm) }
                    .disabled(!AppPaths.qemuIsAvailable(for: vm.machineFamily))
            }
        }
        .padding(.trailing, 16)
    }
}

private struct InstallStep: View {
    let number: Int
    let text: String

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            Text("\(number).").platinumText(.small)
            Text(text).platinumText(.small).fixedSize(horizontal: false, vertical: true)
        }
    }
}

/// The Mac's last screen in a sunken frame, or its model icon on the
/// desktop blue before it has ever started.
struct ScreenPreview: View {
    let vm: VMConfig
    let image: NSImage?
    let running: Bool
    let paused: Bool
    let onActivate: () -> Void

    private var aspect: CGFloat {
        guard let image, image.size.width > 0, image.size.height > 0 else {
            return CGFloat(max(vm.width, 1)) / CGFloat(max(vm.height, 1))
        }
        return image.size.width / image.size.height
    }

    var body: some View {
        PlatinumWell(fill: image == nil ? Platinum.desktop : Platinum.black, preserveFillWhenDisabled: true) {
            ZStack {
                if let image {
                    Image(nsImage: image)
                        .resizable()
                        .interpolation(.none)
                        .aspectRatio(contentMode: .fit)
                        .opacity(paused ? 0.55 : 1)
                } else {
                    VStack(spacing: 10) {
                        PlatinumIcon(vm.machineFamily.iconName, size: 32, scale: 2)
                        Text(running ? "Starting up…" : "Start up this Mac to see its screen here.")
                            .font(.platinumSmall)
                            .foregroundStyle(Color.white)
                        if running {
                            PlatinumProgressBar(value: nil).frame(width: 120)
                        }
                    }
                }
                if paused {
                    Text("Paused")
                        .platinumText(.emphasis)
                        .padding(.horizontal, 10)
                        .frame(height: 20)
                        .background(Platinum.face.color)
                        .overlay(Rectangle().stroke(Color.black, lineWidth: 1))
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .aspectRatio(aspect, contentMode: .fit)
        .frame(maxWidth: .infinity, maxHeight: 300)
        .contentShape(Rectangle())
        .onTapGesture(perform: onActivate)
        .help(running ? "Show this Mac" : "")
        .accessibilityLabel(running ? "Screen of \(vm.name). Click to show the Mac." : "Last screen of \(vm.name)")
    }
}
