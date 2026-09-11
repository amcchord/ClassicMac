import SwiftUI
import AppKit

/// Drop this view into a toolbar or the machine home. It polls only while
/// visible, discards failed/stale readings, and needs no QEMUManager storage.
struct GXMetalStatusMenu: View {
    let config: VMConfig
    let isRunning: Bool
    var isPaused: Bool = false
    var showTools: (() -> Void)? = nil

    @State private var snapshot: GXMetalStatusSnapshot?
    @State private var hasChecked = false
    @State private var showingHelp = false

    private struct ObservationKey: Hashable {
        let id: UUID
        let family: MachineFamily
        let running: Bool
        let paused: Bool
    }

    private var status: GXMetalStatus {
        GXMetalStatus.resolve(snapshot: snapshot,
                              isPowerMac: config.machineFamily == .powerMacG4,
                              isRunning: isRunning, isPaused: isPaused,
                              hasChecked: hasChecked)
    }

    private var color: Color {
        switch status {
        case .accelerating: return .green
        case .faulted, .softwareFallback: return .orange
        default: return .secondary
        }
    }

    var body: some View {
        Menu {
            Text(status.title)
            if let snapshot, isRunning {
                Divider()
                Text(snapshot.rendererDetail)
                Text(snapshot.guestDetail)
                Text("Active 3D contexts: \(snapshot.activeContexts)")
                Text("Host protocol: \(snapshot.protocolVersion)")
                if snapshot.errorCode != 0 {
                    Text("Graphics error: \(snapshot.errorCode)")
                }
            }
            Divider()
            Button("How to Test or Repair GXMetal…") { showingHelp = true }
            if let showTools {
                Button("Show Tools in Media Controls…", action: showTools)
            }
        } label: {
            Label("GXMetal", systemImage: status.symbol)
                .labelStyle(.iconOnly)
                .foregroundStyle(color)
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
        .help("GXMetal: \(status.title)")
        .accessibilityLabel("GXMetal graphics status")
        .accessibilityValue(status.title)
        .alert("Test or Repair GXMetal", isPresented: $showingHelp) {
            Button("OK", role: .cancel) { }
        } message: {
            Text(status.explanation + "\n\n" + GXMetalStatusHelp.instructions)
        }
        .task(id: ObservationKey(id: config.id, family: config.machineFamily,
                                running: isRunning, paused: isPaused)) {
            snapshot = nil
            hasChecked = false
            guard isRunning, config.machineFamily == .powerMacG4 else { return }
            let path = QEMUManager.monitorSocketURL(for: config.id).path
            while !Task.isCancelled {
                let reading = await Task.detached(priority: .utility) {
                    HMPClient.command("qom-get / gxmetal-status", socketPath: path)
                        .flatMap(GXMetalStatusSnapshot.parseHMP)
                }.value
                guard !Task.isCancelled else { return }
                snapshot = reading
                hasChecked = true
                do {
                    try await Task.sleep(for: .seconds(2))
                } catch { return }
            }
        }
    }
}
