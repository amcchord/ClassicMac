import AppKit
import SwiftUI
import UniformTypeIdentifiers
import PlatinumKit

/// The Media window: the discs, Tools volume and floppy a Mac starts with or
/// has inserted, the startup choice, and recently used images.
struct MediaDrawerView: View {
    @Binding var config: VMConfig
    let isRunning: Bool
    var isPaused = false
    @ObservedObject private var controller = MediaController.shared
    @ObservedObject private var recents = MediaRecents.shared
    @State private var recentSelection: String?

    private var busy: Bool { controller.busyIDs.contains(config.id) }
    private var live: Bool { isRunning && config.machineFamily == .quadra800 }
    private var snapshot: MediaSnapshot? { controller.snapshots[config.id] }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .top, spacing: 12) {
                PlatinumIcon("cd", size: 32)
                VStack(alignment: .leading, spacing: 3) {
                    Text("Media for \u{201C}\(config.name)\u{201D}").platinumText(.emphasis)
                    Text(guidance).platinumText(.small).fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 0)
            }
            status
            PlatinumGroupBox("CD") { drive(.disc, icon: "cd") }
            PlatinumGroupBox("ClassicMac Tools") {
                VStack(alignment: .leading, spacing: 6) {
                    drive(.tools, icon: "cd")
                    Text(config.machineFamily == .powerMacG4
                         ? "Classic Mac utilities. Tools mounts at startup; while the Mac starts from an installer CD it waits for the first hard-disk startup."
                         : "Classic Mac utilities, including StuffIt Expander and Disk Copy.")
                        .platinumText(.detail).fixedSize(horizontal: false, vertical: true)
                }
            }
            if config.machineFamily.supportsFloppyDisk {
                PlatinumGroupBox("Floppy Disk") {
                    VStack(alignment: .leading, spacing: 6) {
                        drive(.floppy, icon: "disk")
                        Text("Floppy images are writable. Eject waits for the Mac to finish writing.")
                            .platinumText(.detail)
                    }
                }
            }
            HStack(alignment: .top, spacing: 10) {
                Text("Start up from:").platinumText(.emphasis)
                PlatinumRadioGroup(selection: $config.bootFromCD, options: [
                    (false, "Hard disk", true),
                    (true, "CD", config.cdImagePath?.isEmpty == false),
                ])
                .disabled(busy)
                Spacer(minLength: 0)
            }
            if !visibleRecents.isEmpty {
                recentsSection
            }
            Spacer(minLength: 0)
        }
        .padding(12)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Platinum.face.color)
        .task(id: "\(config.id):\(isRunning)") {
            while !Task.isCancelled {
                await controller.refresh(config.id, running: isRunning)
                do { try await Task.sleep(nanoseconds: 2_000_000_000) } catch { return }
            }
        }
    }

    @ViewBuilder
    private var status: some View {
        if busy {
            HStack(spacing: 8) {
                Text(controller.messages[config.id] ?? "Updating media…").platinumText(.small)
                PlatinumProgressBar(value: nil).frame(width: 140)
            }
        } else if let error = controller.errors[config.id] {
            HStack(spacing: 6) {
                PlatinumIcon("caution", size: 16)
                Text(error).platinumText(.small).fixedSize(horizontal: false, vertical: true)
            }
        } else if let message = controller.messages[config.id] {
            Text(message).platinumText(.detail)
        }
    }

    private var visibleRecents: [RecentMedia] {
        recents.items.filter { $0.device != .floppy || config.machineFamily.supportsFloppyDisk }
    }

    private var recentsSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Recent images:").platinumText(.emphasis)
            PlatinumList(visibleRecents, selection: $recentSelection, rowHeight: 18,
                         onDoubleClick: { item in insertRecent(item) }) { item, _ in
                HStack(spacing: 6) {
                    PlatinumIcon(item.device == .floppy ? "disk" : "cd", size: 16, dimmed: !item.available)
                    Text(item.name).platinumText(item.available ? .emphasis : .disabled)
                        .lineLimit(1).truncationMode(.middle)
                    Spacer(minLength: 4)
                    Text(item.available ? item.device.label : "Missing").platinumText(.detail)
                }
                .padding(.horizontal, 4)
                .help(item.path)
            }
            .frame(height: 94)
            HStack(spacing: Platinum.buttonGap) {
                let item = visibleRecents.first { $0.id == recentSelection }
                PlatinumButton(item?.available == false ? "Locate…" : "Insert") {
                    if let item { insertRecent(item) }
                }
                .disabled(item == nil || busy)
                PlatinumButton("Remove from List") {
                    if let item { recents.forget(item) }
                }
                .disabled(item == nil)
                Spacer()
                PlatinumButton("Clear List") { recents.clear() }
            }
        }
    }

    private func insertRecent(_ item: RecentMedia) {
        if item.available {
            change(item.device, path: item.path)
        } else {
            choose(item.device, replacingRecent: item)
        }
    }

    private var guidance: String {
        if !isRunning { return "Choose what is inserted when this Mac starts up." }
        if !live { return "Changes are saved for the next startup. Shut down and start the Mac to use them." }
        if isPaused { return "Resume the Mac before changing media so it can finish disk activity." }
        if snapshot == nil { return "Waiting for the Mac's drives. You can change media once startup finishes." }
        return "Changes go straight to the Mac's drives. Eject a floppy before inserting another."
    }

    @ViewBuilder
    private func drive(_ device: MediaDevice, icon: String) -> some View {
        let savedPath = device.path(in: config)
        let runningDrive = snapshot?[device]
        let runningPath = runningDrive?.inserted == true ? runningDrive?.path : nil
        let displayedPath = isRunning ? runningPath : savedPath
        let available = displayedPath.map { FileManager.default.isReadableFile(atPath: $0) } ?? true
        let liveBlocked = live && (isPaused || snapshot == nil)
        HStack(alignment: .center, spacing: 10) {
            PlatinumIcon(icon, size: 16, dimmed: displayedPath == nil)
            VStack(alignment: .leading, spacing: 1) {
                Text(isRunning && snapshot == nil ? "Checking…"
                     : displayedPath.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "Nothing inserted")
                    .platinumText(displayedPath == nil ? .disabled : .emphasis)
                    .lineLimit(1).truncationMode(.middle)
                    .help(displayedPath ?? "")
                if isRunning && (config.machineFamily == .powerMacG4 || (live && runningPath != savedPath && snapshot != nil)) {
                    Text("Next startup: \(savedPath.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "nothing")")
                        .platinumText(.detail).lineLimit(1).truncationMode(.middle)
                }
                if !available {
                    Text("This image is missing. Choose it again.").platinumText(.detail)
                }
            }
            Spacer(minLength: 6)
            if device == .tools {
                PlatinumButton(savedPath == nil ? "Insert" : "Reinsert") { change(.tools, path: AppPaths.toolsCD?.path) }
                    .disabled(AppPaths.toolsCD == nil || busy || liveBlocked)
            } else {
                PlatinumButton((live ? runningPath : savedPath) == nil ? "Choose…" : "Replace…") { choose(device) }
                    .disabled(busy || liveBlocked || (live && device == .floppy && runningDrive?.inserted == true))
            }
            PlatinumButton("Eject") { change(device, path: nil) }
                .disabled(!(live ? runningDrive?.inserted == true : savedPath != nil) || busy || liveBlocked)
        }
    }

    private func choose(_ device: MediaDevice, replacingRecent: RecentMedia? = nil) {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.prompt = isRunning && !live ? "Use Next Startup" : "Insert"
        let extensions = device == .floppy ? ["img", "dsk", "ima", "raw"] : ["iso", "toast", "cdr", "img", "raw"]
        panel.allowedContentTypes = extensions.compactMap { UTType(filenameExtension: $0) }
        panel.message = device == .floppy
            ? "Choose a writable raw floppy image (.img, .dsk, .ima or .raw)."
            : "Choose an uncompressed CD image (.iso, .toast, .cdr or .img)."
        if let replacingRecent { panel.nameFieldStringValue = replacingRecent.name }
        guard panel.runModal() == .OK, let url = panel.url else { return }
        change(device, path: url.path, replacingRecent: replacingRecent)
    }

    private func change(_ device: MediaDevice, path: String?, replacingRecent: RecentMedia? = nil) {
        Task {
            if await controller.change(device, path: path, config: config, running: isRunning, paused: isPaused) {
                config = device.setting(path: path, in: config)
                if let replacingRecent { recents.forget(replacingRecent) }
            }
        }
    }
}
