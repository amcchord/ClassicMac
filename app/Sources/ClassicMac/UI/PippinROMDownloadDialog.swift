import AppKit
import SwiftUI
import PlatinumKit

/// Prompts the user with a copyright and software-license notice before downloading
/// the 4 MB Apple / Bandai Pippin Kinka 1.3 boot ROM from the Internet Archive.
struct PippinROMDownloadDialog: View {
    static let size = NSSize(width: 492, height: 356)

    let machineName: String?
    let handle: DialogHandle
    let onChooseLocal: (() -> Void)?
    let onInstalled: (URL) -> Void

    @State private var acknowledged = false
    @State private var phase: PippinROMDownloader.DownloadPhase?
    @State private var isDownloading = false
    @State private var errorMessage: String?
    @State private var downloadTask: Task<Void, Never>?

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .top, spacing: 12) {
                PlatinumIcon("apple_pippin", size: 32)
                VStack(alignment: .leading, spacing: 3) {
                    Text("Download Pippin Boot ROM").platinumText(.emphasis)
                    Text(subtitleText)
                        .platinumText(.detail)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            PlatinumSeparator()

            PlatinumGroupBox("Copyright & Software License Notice") {
                VStack(alignment: .leading, spacing: 7) {
                    Text("The Apple / Bandai Pippin boot ROM (Kinka 1.3, 1996) is copyrighted firmware owned by Apple Inc. and Bandai Co., Ltd.")
                        .platinumText(.body)
                        .fixedSize(horizontal: false, vertical: true)
                    Text("ClassicMac does not bundle or redistribute Pippin firmware. Clicking Download ROM fetches the 4 MB ROM directly from the Internet Archive (archive.org) and verifies its SHA-256 checksum. You are responsible for confirming you have the legal right to download and use this ROM.")
                        .platinumText(.small)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            PlatinumCheckbox(
                "I understand the copyright notice and confirm I am responsible for my right to use this ROM.",
                isOn: $acknowledged
            )
            .disabled(isDownloading)

            if let phase {
                VStack(alignment: .leading, spacing: 6) {
                    Text(statusText(for: phase)).platinumText(.small)
                    PlatinumProgressBar(value: progressValue(for: phase))
                }
            } else if let errorMessage {
                HStack(alignment: .top, spacing: 8) {
                    PlatinumIcon("stop", size: 16)
                    Text(errorMessage)
                        .platinumText(.small)
                        .foregroundStyle(Platinum.errorText.color)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            Spacer(minLength: 0)

            HStack(spacing: Platinum.buttonGap) {
                if let onChooseLocal {
                    PlatinumButton("Choose File…", minWidth: 94) {
                        guard !isDownloading else { return }
                        handle.close()
                        onChooseLocal()
                    }
                    .disabled(isDownloading)
                }
                Spacer()
                PlatinumButton("Cancel", role: .cancel) {
                    cancelDownloadAndClose()
                }
                PlatinumButton("Download ROM", role: .default, minWidth: 112, action: startDownload)
                    .disabled(!acknowledged || isDownloading)
            }
        }
        .padding(14)
        .frame(width: Self.size.width, height: Self.size.height)
        .background(Platinum.face.color)
        .onAppear {
            handle.window?.shouldClose = {
                downloadTask?.cancel()
                downloadTask = nil
                return true
            }
        }
        .onDisappear {
            downloadTask?.cancel()
            downloadTask = nil
            handle.window?.shouldClose = nil
        }
    }

    private var subtitleText: String {
        if let machineName, !machineName.isEmpty {
            return "\u{201C}\(machineName)\u{201D} needs a 4 MB Pippin Kinka boot ROM before it can start."
        }
        return "Download Pippin Kinka 1.3 (4 MB) from the Internet Archive (archive.org)."
    }

    private func statusText(for phase: PippinROMDownloader.DownloadPhase) -> String {
        switch phase {
        case .connecting:
            return "Connecting to archive.org…"
        case .downloading(let received, let total):
            let rec = ByteCountFormatter.string(fromByteCount: received, countStyle: .file)
            let tot = ByteCountFormatter.string(fromByteCount: total, countStyle: .file)
            return "Downloading Pippin Kinka 1.3… (\(rec) of \(tot))"
        case .verifying:
            return "Verifying SHA-256 checksum…"
        }
    }

    private func progressValue(for phase: PippinROMDownloader.DownloadPhase) -> Double? {
        switch phase {
        case .connecting, .verifying:
            return nil
        case .downloading(let received, let total):
            guard total > 0 else { return nil }
            return min(1.0, max(0.0, Double(received) / Double(total)))
        }
    }

    private func startDownload() {
        guard acknowledged, !isDownloading else { return }
        errorMessage = nil
        isDownloading = true
        phase = .connecting

        downloadTask = Task {
            do {
                let installedURL = try await PippinROMDownloader.downloadKinka13 { nextPhase in
                    Task { @MainActor in
                        phase = nextPhase
                    }
                }
                await MainActor.run {
                    isDownloading = false
                    phase = nil
                    downloadTask = nil
                    handle.close()
                    onInstalled(installedURL)
                }
            } catch is CancellationError {
                await MainActor.run {
                    isDownloading = false
                    phase = nil
                    downloadTask = nil
                }
            } catch {
                await MainActor.run {
                    isDownloading = false
                    phase = nil
                    downloadTask = nil
                    errorMessage = error.localizedDescription
                }
            }
        }
    }

    private func cancelDownloadAndClose() {
        downloadTask?.cancel()
        downloadTask = nil
        isDownloading = false
        phase = nil
        handle.close()
    }
}
