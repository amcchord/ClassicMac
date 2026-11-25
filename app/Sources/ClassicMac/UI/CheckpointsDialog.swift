import AppKit
import SwiftUI
import PlatinumKit

/// Platinum dialog for creating, restoring, and deleting APFS CoW checkpoints.
struct CheckpointsDialog: View {
    static let size = NSSize(width: 540, height: 400)

    let vm: VMConfig
    let handle: DialogHandle
    @ObservedObject private var manager = QEMUManager.shared
    @State private var checkpoints: [VMCheckpoint] = []
    @State private var selectedID: String?
    @State private var isCreating = false
    @State private var newLabel = ""
    @State private var isBusy = false
    @State private var errorMessage: String?

    private var running: Bool { manager.isRunning(vm.id) }
    private var selectedCheckpoint: VMCheckpoint? {
        guard let selectedID else { return nil }
        return checkpoints.first { $0.id == selectedID }
    }

    private static let dateFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateStyle = .medium
        f.timeStyle = .short
        return f
    }()

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 12) {
                PlatinumIcon(vm.machineFamily.iconName, size: 32)
                VStack(alignment: .leading, spacing: 2) {
                    Text(vm.name).platinumText(.emphasis).lineLimit(1)
                    Text("Save instant snapshots of this Mac's disk and startup memory.")
                        .platinumText(.detail)
                }
                Spacer()
            }

            PlatinumSeparator()

            if checkpoints.isEmpty {
                PlatinumWell(fill: Platinum.white, preserveFillWhenDisabled: true) {
                    VStack(spacing: 6) {
                        Text("No checkpoints saved yet.").platinumText(.emphasis)
                        Text("Create a checkpoint before installing software or changing system files so you can restore anytime.")
                            .platinumText(.small)
                            .multilineTextAlignment(.center)
                            .frame(maxWidth: 340)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                PlatinumList(checkpoints, selection: $selectedID, rowHeight: 38) { cp, _ in
                    HStack(spacing: 8) {
                        PlatinumIcon("disk", size: 16)
                        VStack(alignment: .leading, spacing: 1) {
                            HStack(spacing: 6) {
                                Text(cp.label).platinumText(.emphasis).lineLimit(1)
                                if let badge = cp.kind.badgeLabel, badge != cp.label {
                                    Text("(\(badge))").platinumText(.detail).lineLimit(1)
                                }
                            }
                            Text(detailText(for: cp))
                                .platinumText(.detail)
                                .lineLimit(1)
                        }
                        Spacer(minLength: 0)
                    }
                    .padding(.horizontal, 6)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }

            if isCreating {
                HStack(spacing: 8) {
                    Text("Label:").platinumText(.emphasis)
                    PlatinumTextField("Checkpoint", text: $newLabel, onCommit: createCheckpoint)
                    PlatinumButton("Save", role: .default, action: createCheckpoint)
                        .disabled(isBusy)
                    PlatinumButton("Cancel") {
                        isCreating = false
                        newLabel = ""
                    }
                    .disabled(isBusy)
                }
            }

            if running {
                HStack(spacing: 8) {
                    PlatinumIcon("lock", size: 16)
                    Text("Shut down this Mac to create or restore checkpoints.").platinumText(.small)
                }
            } else if let errorMessage {
                HStack(alignment: .top, spacing: 8) {
                    PlatinumIcon("stop", size: 16)
                    Text(errorMessage).platinumText(.small)
                        .foregroundStyle(Platinum.errorText.color)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            HStack(spacing: Platinum.buttonGap) {
                if !isCreating {
                    PlatinumButton("Create Checkpoint…") {
                        errorMessage = nil
                        newLabel = ""
                        isCreating = true
                    }
                    .disabled(running || isBusy)
                }
                PlatinumButton("Restore…", action: confirmRestore)
                    .disabled(running || isBusy || selectedCheckpoint == nil)
                PlatinumButton("Delete", action: deleteSelected)
                    .disabled(isBusy || selectedCheckpoint == nil)
                Spacer()
                PlatinumButton("Done", role: .default) { handle.close() }
                    .disabled(isBusy)
            }
        }
        .padding(12)
        .frame(width: Self.size.width, height: Self.size.height)
        .background(Platinum.face.color)
        .onAppear(perform: reload)
    }

    private func reload() {
        checkpoints = VMCheckpointStore.list(in: vm.folder)
        if let selectedID, checkpoints.contains(where: { $0.id == selectedID }) {
            return
        }
        selectedID = checkpoints.first?.id
    }

    private func detailText(for cp: VMCheckpoint) -> String {
        let date = Self.dateFormatter.string(from: cp.createdAt)
        let size = ByteCountFormatter.string(fromByteCount: cp.diskBytes, countStyle: .file)
        return "\(date) · \(size) · \(cp.method.label)"
    }

    private func createCheckpoint() {
        guard !running, !isBusy else { return }
        let label = newLabel.trimmingCharacters(in: .whitespacesAndNewlines)
        isBusy = true
        errorMessage = nil
        let targetConfig = vm
        Task {
            let result = await Task.detached(priority: .userInitiated) {
                Result {
                    try VMCheckpointStore.create(
                        for: targetConfig,
                        label: label.isEmpty ? "Checkpoint" : label
                    )
                }
            }.value
            isBusy = false
            switch result {
            case .success(let created):
                isCreating = false
                newLabel = ""
                reload()
                selectedID = created.id
            case .failure(let error):
                errorMessage = error.localizedDescription
                NSSound.beep()
            }
        }
    }

    private func confirmRestore() {
        guard !running, !isBusy, let cp = selectedCheckpoint else { return }
        WindowManager.shared.alert(
            .caution,
            "Restore \u{201C}\(cp.label)\u{201D}?",
            informative: "ClassicMac will automatically save a \u{201C}Pre-restore backup\u{201D} checkpoint of your current disk first, then restore this snapshot.",
            buttons: [
                PlatinumAlertButton("Restore", role: .default) { performRestore(cp) },
                PlatinumAlertButton("Cancel", role: .cancel),
            ],
            parent: handle.window
        )
    }

    private func performRestore(_ cp: VMCheckpoint) {
        guard !running, !isBusy else { return }
        isBusy = true
        errorMessage = nil
        let targetConfig = vm
        Task {
            let result = await Task.detached(priority: .userInitiated) {
                Result {
                    try VMCheckpointStore.restore(cp.id, for: targetConfig, createBackup: true)
                }
            }.value
            isBusy = false
            switch result {
            case .success:
                reload()
            case .failure(let error):
                errorMessage = error.localizedDescription
                NSSound.beep()
            }
        }
    }

    private func deleteSelected() {
        guard !isBusy, let cp = selectedCheckpoint else { return }
        do {
            try VMCheckpointStore.delete(cp.id, in: vm.folder)
            errorMessage = nil
            reload()
        } catch {
            errorMessage = error.localizedDescription
            NSSound.beep()
        }
    }
}
