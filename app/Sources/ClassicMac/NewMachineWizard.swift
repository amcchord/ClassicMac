import SwiftUI

struct NewMachineWizard: View {
    var onCreate: (VMConfig) -> Bool
    var onInstall: (URL) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var selection: SetupMethod = .download
    @State private var route: SetupMethod?

    private enum SetupMethod {
        case download
        case custom
    }

    var body: some View {
        Group {
            switch route {
            case .download:
                DownloadMachineSheet(onBack: { route = nil }, onInstall: onInstall)
            case .custom:
                NewVMSheet(onChooseSetup: { route = nil }, onCreate: onCreate)
            case nil:
                setupChoice
            }
        }
        .frame(width: 620, height: 620)
    }

    private var setupChoice: some View {
        VStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 5) {
                Text("New Machine")
                    .font(.title2.bold())
                Text("How would you like to set up your Mac?")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(24)

            Divider()

            VStack(spacing: 16) {
                setupOption(
                    .download,
                    title: "Download Mac OS 9",
                    icon: "arrow.down.circle.fill",
                    description: "The easiest way to get started. Mac OS 9 and GXMetal are already installed and ready to use.",
                    detail: "No install discs or setup steps needed."
                )
                setupOption(
                    .custom,
                    title: "Install from your own disc",
                    icon: "opticaldisc",
                    description: "Create a Quadra 800 or Power Mac G4, choose its hardware, and install Mac OS yourself.",
                    detail: "Use your own installation disc image."
                )

                Label("New disks use space as you add files, up to their chosen capacity.", systemImage: "internaldrive")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 4)

                Spacer(minLength: 0)
            }
            .padding(24)

            Divider()

            HStack {
                Spacer()
                Button("Cancel") { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Continue") { route = selection }
                    .keyboardShortcut(.defaultAction)
                    .buttonStyle(.borderedProminent)
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 16)
        }
    }

    private func setupOption(
        _ method: SetupMethod,
        title: String,
        icon: String,
        description: String,
        detail: String
    ) -> some View {
        Button {
            selection = method
        } label: {
            HStack(alignment: .top, spacing: 14) {
                Image(systemName: icon)
                    .font(.system(size: 28))
                    .foregroundStyle(method == .download ? Color.accentColor : .secondary)
                    .frame(width: 34)
                    .accessibilityHidden(true)
                VStack(alignment: .leading, spacing: 8) {
                    HStack(spacing: 10) {
                        Text(title).font(.headline)
                        if method == .download {
                            Text("Recommended")
                                .font(.caption.weight(.semibold))
                                .foregroundStyle(Color.accentColor)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 3)
                                .background(Color.accentColor.opacity(0.12), in: Capsule())
                        }
                    }
                    Text(description)
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    Text(detail)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                Image(systemName: selection == method ? "checkmark.circle.fill" : "circle")
                    .foregroundStyle(selection == method ? Color.accentColor : .secondary)
                    .font(.title3)
                    .accessibilityHidden(true)
            }
            .padding(18)
            .background(selection == method ? Color.accentColor.opacity(0.06) : Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 12))
            .overlay {
                RoundedRectangle(cornerRadius: 12)
                    .strokeBorder(selection == method ? Color.accentColor : Color.secondary.opacity(0.25), lineWidth: selection == method ? 2 : 1)
            }
            .contentShape(RoundedRectangle(cornerRadius: 12))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(method == .download ? "Download Mac OS 9, recommended" : title)
        .accessibilityValue(selection == method ? "Selected" : "Not selected")
        .accessibilityHint(description)
    }
}
