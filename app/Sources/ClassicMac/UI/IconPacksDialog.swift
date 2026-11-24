import AppKit
import SwiftUI
import UniformTypeIdentifiers
import PlatinumKit

/// Credits the included artwork and allows additional local collections.
struct IconPacksDialog: View {
    static let size = NSSize(width: 600, height: 330)

    let handle: DialogHandle
    @ObservedObject private var packs = IconPacks.shared
    @State private var errorMessage: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Classic Mac icons from The Iconfactory").platinumText(.emphasis)
            Text("ClassicMac includes classic Macintosh machine icons by The Iconfactory, used with permission. You can also add collections from your own downloaded disk images. Removing added collections restores the included icons. The original collection licenses and readmes are in the app’s Resources/Iconfactory folder.")
                .platinumText(.small)
                .fixedSize(horizontal: false, vertical: true)
            PlatinumGroupBox(packs.isUsingIconfactory ? "Now showing" : "Now showing ClassicMac\u{2019}s own icons") {
                HStack(alignment: .top, spacing: 0) {
                    ForEach(IconPacks.choices, id: \.icon) { choice in
                        VStack(spacing: 4) {
                            PlatinumIcon(choice.icon, size: 32)
                            Text(choice.label).platinumText(.small)
                            Text(packs.activeIcons[choice.icon] ?? "ClassicMac")
                                .platinumText(.detail).lineLimit(1)
                        }
                        .frame(maxWidth: .infinity)
                    }
                }
            }
            if packs.isImporting {
                HStack(spacing: 8) {
                    Text("Adding icons…").platinumText(.small)
                    PlatinumProgressBar(value: nil).frame(width: 160)
                }
            } else if let errorMessage {
                HStack(alignment: .top, spacing: 6) {
                    PlatinumIcon("caution", size: 16)
                    Text(errorMessage).platinumText(.small).fixedSize(horizontal: false, vertical: true)
                }
            } else if !packs.collections.isEmpty {
                Text("Collections on this computer: " + packs.collections.joined(separator: ", ") + ".")
                    .platinumText(.detail).lineLimit(2)
            }
            Spacer(minLength: 0)
            HStack(spacing: Platinum.buttonGap) {
                PlatinumButton("Visit The Iconfactory") { NSWorkspace.shared.open(IconPacks.website) }
                PlatinumButton("Show Licenses…", action: showLicenses)
                if !packs.collections.isEmpty {
                    PlatinumButton("Remove Added") { packs.removeAll() }
                }
                Spacer()
                PlatinumButton("Add Icons…", action: chooseCollections)
                    .disabled(packs.isImporting)
                PlatinumButton("Done", role: .default) { handle.close() }
            }
        }
        .padding(12)
        .frame(width: Self.size.width, height: Self.size.height)
        .background(Platinum.face.color)
    }

    static func licensesDirectoryURL() -> URL? {
        let fm = FileManager.default
        let candidates: [URL?] = [
            AppPaths.resourcesDir.appendingPathComponent("Iconfactory", isDirectory: true),
            ProcessInfo.processInfo.environment["CLASSICMAC_REPO"].map {
                URL(fileURLWithPath: $0).appendingPathComponent("Resources/Iconfactory", isDirectory: true)
            },
            URL(fileURLWithPath: fm.currentDirectoryPath).deletingLastPathComponent().appendingPathComponent("Resources/Iconfactory", isDirectory: true),
            URL(fileURLWithPath: fm.currentDirectoryPath).appendingPathComponent("Resources/Iconfactory", isDirectory: true),
        ]
        return candidates.compactMap { $0 }.first { fm.fileExists(atPath: $0.path) }
    }

    private func showLicenses() {
        guard let dir = Self.licensesDirectoryURL() else {
            WindowManager.shared.alert(.note, "The Iconfactory folder isn\u{2019}t included in this build.",
                                       informative: "Icon licenses and readmes live in Resources/Iconfactory in the ClassicMac repository.",
                                       buttons: [PlatinumAlertButton("OK", role: .default)])
            return
        }
        NSWorkspace.shared.activateFileViewerSelecting([dir])
    }

    private func chooseCollections() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [.diskImage, .folder]
        panel.directoryURL = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
        panel.message = "Choose the Iconfactory disk images (or folders) you downloaded"
        panel.prompt = "Add"
        guard panel.runModal() == .OK else { return }
        let urls = panel.urls
        errorMessage = nil
        Task {
            do { try await packs.add(urls) } catch { errorMessage = error.localizedDescription }
        }
    }
}
