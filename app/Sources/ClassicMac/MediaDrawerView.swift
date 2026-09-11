import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct MediaDrawerView: View {
    @Binding var config: VMConfig
    let isRunning: Bool
    var isPaused = false
    @ObservedObject private var controller = MediaController.shared
    @ObservedObject private var recents = MediaRecents.shared

    private var busy: Bool { controller.busyIDs.contains(config.id) }
    private var live: Bool { isRunning && config.machineFamily == .quadra800 }
    private var snapshot: MediaSnapshot? { controller.snapshots[config.id] }

    var body: some View {
        Form {
            Section {
                HStack(alignment: .top, spacing: 12) {
                    Image(systemName: "opticaldiscdrive").font(.title2).foregroundStyle(.secondary)
                    VStack(alignment: .leading, spacing: 5) {
                        Text("Media for \(config.name)").font(.headline)
                        Text(guidance).font(.callout).foregroundStyle(.secondary)
                    }
                }
                if busy {
                    HStack { ProgressView().controlSize(.small); Text(controller.messages[config.id] ?? "Updating media…") }
                } else if let error = controller.errors[config.id] {
                    Label(error, systemImage: "exclamationmark.triangle").foregroundStyle(.orange)
                } else if let message = controller.messages[config.id] {
                    Label(message, systemImage: "checkmark.circle").foregroundStyle(.secondary)
                }
            }

            Section("Disc") { drive(.disc, symbol: "opticaldisc") }
            Section("ClassicMac Tools") {
                drive(.tools, symbol: "shippingbox")
                Text(config.machineFamily == .powerMacG4
                     ? "Tools includes GXMetal and classic Mac utilities. It mounts at startup; during an installer-disc boot, it waits for the first hard-disk startup."
                     : "Classic Mac utilities, including StuffIt Expander and Disk Copy.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            if config.machineFamily.supportsFloppyDisk {
                Section("Floppy disk") {
                    drive(.floppy, symbol: "externaldrive")
                    Text("Floppy images are writable. Eject waits for the Mac to finish disk activity before removing the image.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            Section("Next startup") {
                Picker("Start up from", selection: $config.bootFromCD) {
                    Text("Hard disk").tag(false)
                    Text("Disc").tag(true).disabled(config.cdImagePath?.isEmpty != false)
                }
                .disabled(busy)
                if config.bootFromCD {
                    Text("After an installer makes the hard disk bootable, ClassicMac automatically selects it for the next startup.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            if !visibleRecents.isEmpty {
                Section("Recent images") {
                    ForEach(visibleRecents) { item in
                        HStack {
                            VStack(alignment: .leading, spacing: 3) {
                                Text(item.name).lineLimit(1).truncationMode(.middle)
                                Text(item.available ? item.device.label : "Unavailable — locate this image again")
                                    .font(.caption).foregroundStyle(item.available ? Color.secondary : Color.orange)
                            }
                            .help(item.path)
                            Spacer()
                            Button(item.available ? "Insert" : "Locate…") {
                                if item.available { change(item.device, path: item.path) }
                                else { choose(item.device, replacingRecent: item) }
                            }
                            .disabled(busy || (live && (isPaused || snapshot == nil || (item.device == .floppy && snapshot?[.floppy]?.inserted == true))))
                            Button { recents.forget(item) } label: { Image(systemName: "xmark") }
                                .buttonStyle(.borderless).help("Remove from Recent Images")
                                .accessibilityLabel("Remove \(item.name) from Recent Images")
                        }
                    }
                    Button("Clear Recent Images") { recents.clear() }
                        .font(.caption)
                }
            }
        }
        .formStyle(.grouped)
        .task(id: "\(config.id):\(isRunning)") {
            while !Task.isCancelled {
                await controller.refresh(config.id, running: isRunning)
                do { try await Task.sleep(nanoseconds: 2_000_000_000) } catch { return }
            }
        }
    }

    private var visibleRecents: [RecentMedia] {
        recents.items.filter { $0.device != .floppy || config.machineFamily.supportsFloppyDisk }
    }

    private var guidance: String {
        if !isRunning { return "Choose the images to insert when this Mac starts." }
        if !live { return "Changes here are saved for the next startup. Shut down and start the Mac to apply them." }
        if isPaused { return "Resume the Mac before changing media so it can finish disk activity safely." }
        if snapshot == nil { return "Waiting for the Mac's media controls. You can change media after startup finishes, or while the Mac is shut down." }
        return "Disc changes appear in the running Mac. For a floppy, eject the current disk before inserting another."
    }

    @ViewBuilder
    private func drive(_ device: MediaDevice, symbol: String) -> some View {
        let savedPath = device.path(in: config)
        let runningDrive = snapshot?[device]
        let runningPath = runningDrive?.inserted == true ? runningDrive?.path : nil
        let displayedPath = isRunning ? runningPath : savedPath
        let available = displayedPath.map { FileManager.default.isReadableFile(atPath: $0) } ?? true
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: symbol).font(.title3).foregroundStyle(.secondary).frame(width: 26)
            VStack(alignment: .leading, spacing: 4) {
                Text(isRunning && snapshot == nil ? "Checking inserted media…" : (displayedPath.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "Nothing inserted"))
                    .lineLimit(2).truncationMode(.middle)
                    .help(displayedPath ?? "No image")
                if isRunning && config.machineFamily == .powerMacG4 {
                    Text("Next startup: \(savedPath.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "Nothing inserted")")
                        .font(.caption).foregroundStyle(.secondary)
                        .help(savedPath ?? "No image selected")
                } else if live && runningPath != savedPath && snapshot != nil {
                    Text("Next startup: \(savedPath.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "Nothing inserted")")
                        .font(.caption).foregroundStyle(.secondary)
                }
                if !available { Text("Image unavailable — choose it again.").font(.caption).foregroundStyle(.orange) }
            }
            Spacer(minLength: 0)
            if let path = displayedPath, available {
                Button { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)]) } label: { Image(systemName: "folder") }
                    .help("Show Source Image in Finder").accessibilityLabel("Show \(device.label) Source Image in Finder")
            }
        }
        HStack {
            if device == .tools {
                Button(savedPath == nil ? "Insert Tools" : "Reinsert Tools") { change(.tools, path: AppPaths.toolsCD?.path) }
                    .disabled(AppPaths.toolsCD == nil || busy || (live && (isPaused || snapshot == nil)))
                if AppPaths.toolsCD == nil { Text("Tools is missing from this copy of ClassicMac.").font(.caption).foregroundStyle(.orange) }
            } else {
                Button((live ? runningPath : savedPath) == nil ? "Choose Image…" : "Replace Image…") { choose(device) }
                    .disabled(busy || (live && (isPaused || snapshot == nil || (device == .floppy && runningDrive?.inserted == true))))
            }
            if (live ? runningDrive?.inserted == true : savedPath != nil) {
                Button("Eject") { change(device, path: nil) }
                    .disabled(busy || (live && (isPaused || snapshot == nil)))
            }
        }
    }

    private func choose(_ device: MediaDevice, replacingRecent: RecentMedia? = nil) {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.prompt = isRunning && !live ? "Use Next Startup" : "Insert"
        let extensions = device == .floppy ? ["img", "dsk", "ima", "raw"] : ["iso", "toast", "cdr", "img", "raw"]
        panel.allowedContentTypes = extensions.compactMap { UTType(filenameExtension: $0) }
        panel.message = device == .floppy ? "Choose a writable raw floppy image (.img, .dsk, .ima, or .raw)." : "Choose an uncompressed CD image (.iso, .toast, .cdr, or .img)."
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
