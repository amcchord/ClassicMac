import AppKit
import SwiftUI
import PlatinumKit

/// Platinum software update dialog driven by `AppUpdater.shared`.
struct UpdateDialog: View {
    static let size = NSSize(width: 460, height: 290)

    let handle: DialogHandle
    @ObservedObject var updater = AppUpdater.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            content
            Spacer(minLength: 0)
            footer
        }
        .padding(14)
        .frame(width: Self.size.width, height: Self.size.height)
        .background(Platinum.face.color)
    }

    @ViewBuilder
    private var content: some View {
        switch updater.state {
        case .idle, .checking:
            HStack(spacing: 12) {
                appIcon
                VStack(alignment: .leading, spacing: 4) {
                    Text("Checking for updates…").platinumText(.emphasis)
                    Text("Connecting to GitHub Releases for ClassicMac.")
                        .platinumText(.small)
                }
            }
            PlatinumProgressBar(value: nil)
                .padding(.top, 8)

        case .upToDate(let version):
            HStack(alignment: .top, spacing: 12) {
                appIcon
                VStack(alignment: .leading, spacing: 6) {
                    Text("ClassicMac is up to date.").platinumText(.emphasis)
                    Text("ClassicMac \(version) is currently the newest version available.")
                        .platinumText(.small)
                        .fixedSize(horizontal: false, vertical: true)
                    PlatinumCheckbox(
                        "Check for updates automatically",
                        isOn: $updater.automaticallyChecksForUpdates
                    )
                    .padding(.top, 4)
                }
            }

        case .available(let release):
            HStack(alignment: .top, spacing: 12) {
                appIcon
                VStack(alignment: .leading, spacing: 3) {
                    Text("A new version of ClassicMac is available!")
                        .platinumText(.emphasis)
                    Text("ClassicMac \(release.version) is now available (you have \(updater.currentVersion)). Download size: \(ByteCountFormatter.string(fromByteCount: release.zipSize, countStyle: .file)).")
                        .platinumText(.small)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            PlatinumWell {
                PlatinumScrollView(line: 14) {
                    Text(release.body.isEmpty ? "Includes bug fixes and improvements." : release.body)
                        .platinumText(.small)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(6)
                }
            }
            .frame(maxHeight: .infinity)
            if !updater.canInstallInPlace {
                Text("Move ClassicMac.app to Applications to install updates automatically, or download the release from GitHub.")
                    .platinumText(.detail)
                    .fixedSize(horizontal: false, vertical: true)
            }
            PlatinumCheckbox(
                "Check for updates automatically",
                isOn: $updater.automaticallyChecksForUpdates
            )

        case .downloading(let release, let done, let total, let rate, let eta):
            HStack(spacing: 12) {
                appIcon
                VStack(alignment: .leading, spacing: 3) {
                    Text("Downloading ClassicMac \(release.version)…")
                        .platinumText(.emphasis)
                        .lineLimit(1)
                    Text(downloadDetail(done: done, total: total, rate: rate, eta: eta))
                        .platinumText(.small)
                        .lineLimit(1)
                }
                Spacer(minLength: 0)
            }
            PlatinumProgressBar(value: total > 0 ? Double(done) / Double(total) : nil)
                .padding(.top, 6)

        case .verifying(let release):
            HStack(spacing: 12) {
                appIcon
                VStack(alignment: .leading, spacing: 3) {
                    Text("Verifying ClassicMac \(release.version)…")
                        .platinumText(.emphasis)
                        .lineLimit(1)
                    Text("Checking SHA-256 checksum and Developer ID signature…")
                        .platinumText(.small)
                }
            }
            PlatinumProgressBar(value: nil)
                .padding(.top, 6)

        case .readyToInstall(let release, _, _, _):
            HStack(alignment: .top, spacing: 12) {
                appIcon
                VStack(alignment: .leading, spacing: 6) {
                    Text("ClassicMac \(release.version) is ready to install.")
                        .platinumText(.emphasis)
                    Text("ClassicMac will quit, replace itself with the verified update, and reopen.")
                        .platinumText(.small)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

        case .error(let message):
            HStack(alignment: .top, spacing: 12) {
                PlatinumIcon("caution", size: 32)
                VStack(alignment: .leading, spacing: 6) {
                    Text("Software Update").platinumText(.emphasis)
                    Text(message)
                        .platinumText(.small)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        }
    }

    @ViewBuilder
    private var footer: some View {
        HStack(spacing: Platinum.buttonGap) {
            switch updater.state {
            case .idle, .checking, .downloading, .verifying:
                Spacer()
                PlatinumButton("Cancel", role: .cancel) {
                    updater.cancel()
                    handle.close()
                }

            case .upToDate:
                Spacer()
                PlatinumButton("OK", role: .default) {
                    handle.close()
                }

            case .available(let release):
                PlatinumButton("Skip This Version") {
                    updater.skipVersion(release)
                    handle.close()
                }
                Spacer()
                PlatinumButton("Later", role: .cancel) {
                    handle.close()
                }
                if updater.canInstallInPlace {
                    PlatinumButton("Install Update", role: .default) {
                        updater.downloadAndVerify(release)
                    }
                } else {
                    PlatinumButton("Download on GitHub…", role: .default) {
                        NSWorkspace.shared.open(release.htmlURL ?? release.zipURL)
                        handle.close()
                    }
                }

            case .readyToInstall:
                Spacer()
                PlatinumButton("Later", role: .cancel) {
                    handle.close()
                }
                PlatinumButton("Install and Relaunch", role: .default) {
                    updater.installPreparedUpdate()
                }

            case .error:
                Spacer()
                PlatinumButton("Close", role: .cancel) {
                    handle.close()
                }
                PlatinumButton("Try Again", role: .default) {
                    updater.checkForUpdates(interactive: true)
                }
            }
        }
    }

    private var appIcon: some View {
        Image(nsImage: NSApp.applicationIconImage)
            .resizable()
            .interpolation(.high)
            .frame(width: 48, height: 48)
    }

    private func downloadDetail(done: Int64, total: Int64, rate: Double?, eta: Double?) -> String {
        let doneText = ByteCountFormatter.string(fromByteCount: done, countStyle: .file)
        let totalText = ByteCountFormatter.string(fromByteCount: total, countStyle: .file)
        var text = "\(doneText) of \(totalText)"
        if total > 0 {
            let percent = min(max(Int((Double(done) / Double(total) * 100).rounded(.down)), 0), 100)
            text += " (\(percent)%)"
        }
        if let rate {
            text += " · " + TransferMeter.formatSpeed(rate)
            if let eta {
                text += " · " + TransferMeter.formatETA(eta)
            }
        }
        return text
    }
}
