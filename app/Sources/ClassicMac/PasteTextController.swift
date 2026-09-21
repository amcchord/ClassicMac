import AppKit
import SwiftUI
import Combine

@MainActor
final class PasteTextController: NSObject, NSWindowDelegate {
    static let shared = PasteTextController()
    static let requestNotification = Notification.Name("com.classicmac.paste-text")
    private var observer: NSObjectProtocol?
    private var window: NSWindow?
    private var session: PasteTextSession?

    func startListening() {
        guard observer == nil else { return }
        observer = DistributedNotificationCenter.default().addObserver(
            forName: Self.requestNotification, object: nil, queue: .main
        ) { notification in
            guard let object = notification.object as? String, let id = UUID(uuidString: object) else { return }
            Task { @MainActor in Self.shared.present(for: id) }
        }
    }

    /// Only opens a review window. Input is never sent by a notification or
    /// browser request; the person must press Paste Text in this window.
    func present(for id: UUID) {
        guard QEMUManager.shared.isRunning(id), !QEMUManager.shared.isPaused(id),
              let vm = VMStore.shared.vms.first(where: { $0.id == id }),
              vm.machineFamily != .powerMac7500 else { return }
        if session?.isPasting == true {
            window?.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }
        window?.close()
        let session = PasteTextSession(id: id, name: vm.name)
        self.session = session
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 590, height: 455),
                              styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
        window.title = "Paste Text into \(vm.name)"
        window.contentViewController = NSHostingController(rootView: PasteTextView(session: session))
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.center()
        self.window = window
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func windowWillClose(_ notification: Notification) {
        session?.cancel()
        session = nil
        window = nil
    }
}

@MainActor
final class PasteTextSession: ObservableObject {
    let id: UUID
    let name: String
    @Published var text: String
    @Published var slow = false
    @Published private(set) var isPasting = false
    @Published private(set) var sent = 0
    @Published private(set) var total = 0
    @Published private(set) var status = ""
    private var task: Task<Void, Never>?
    private var stateObserver: AnyCancellable?
    private var focusObserver: AnyCancellable?

    init(id: UUID, name: String) {
        self.id = id
        self.name = name
        // Reading is on demand, once, after an explicit Paste Text action.
        text = NSPasteboard.general.string(forType: .string) ?? ""
        stateObserver = QEMUManager.shared.$runningIDs.combineLatest(QEMUManager.shared.$pausedIDs)
            .sink { [weak self] running, paused in
                // A stop/restart boundary must end this job even if the same
                // machine is already running again before the next keystroke.
                if !running.contains(id) || paused.contains(id) { self?.cancel() }
            }
        focusObserver = NotificationCenter.default.publisher(for: NSApplication.didResignActiveNotification)
            .sink { [weak self] _ in self?.cancel() }
    }

    var validation: String? {
        do { _ = try PasteTextPlan(text); return nil }
        catch { return error.localizedDescription }
    }

    func paste() {
        guard !isPasting else { return }
        guard QEMUManager.shared.isRunning(id), !QEMUManager.shared.isPaused(id) else {
            status = PasteTextTransferError.unavailable.localizedDescription
            return
        }
        guard !NSEvent.modifierFlags.contains(.capsLock) else {
            status = "Turn Caps Lock off before pasting. The Mac must also use its US keyboard layout."
            return
        }
        let plan: PasteTextPlan
        do { plan = try PasteTextPlan(text) }
        catch { status = error.localizedDescription; return }
        sent = 0
        total = plan.characters.count
        status = "Keep this window active until pasting finishes. Switching apps cancels the paste."
        isPasting = true
        let interval: UInt64 = slow ? 125_000_000 : 50_000_000
        let path = QEMUManager.monitorSocketURL(for: id).path
        task = Task { [weak self] in
            guard let self else { return }
            do {
                try await PasteTextTransfer.run(plan, intervalNanoseconds: interval, available: {
                    await MainActor.run {
                        QEMUManager.shared.isRunning(self.id) && !QEMUManager.shared.isPaused(self.id)
                    }
                }, send: { command in
                    await Task.detached(priority: .userInitiated) {
                        guard let response = HMPClient.command(command, socketPath: path) else { return false }
                        let message = response.lowercased()
                        return !message.contains("unknown key") && !message.contains("error:") && !message.contains("unknown command")
                    }.value
                }, progress: { count in
                    await MainActor.run { self.sent = count }
                })
                status = "Finished pasting \(total) characters."
            } catch is CancellationError {
                status = "Cancelled after \(sent) characters. Text already entered remains in the Mac."
            } catch {
                status = error.localizedDescription
            }
            isPasting = false
            task = nil
        }
    }

    func cancel() { task?.cancel() }
}

private struct PasteTextView: View {
    @ObservedObject var session: PasteTextSession
    @ObservedObject private var manager = QEMUManager.shared
    @FocusState private var editingText: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Paste Text into \(session.name)").font(.title2.bold())
            Text("Place the text cursor in the Mac first. This types the text below using the Mac's US keyboard layout, with Caps Lock off. It supports English text and common accented letters; other characters must be edited first.")
                .foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            TextEditor(text: $session.text)
                .focused($editingText)
                .font(.system(.body, design: .monospaced))
                .scrollContentBackground(.hidden)
                .padding(6)
                .background(.background, in: RoundedRectangle(cornerRadius: 6))
                .overlay(RoundedRectangle(cornerRadius: 6).stroke(.quaternary))
                .disabled(session.isPasting)
                .frame(minHeight: 135)
            Text("Tabs and line breaks press Tab and Return in the Mac. Keep the destination app and cursor in place while pasting.")
                .font(.caption).foregroundStyle(.secondary)
            if session.isPasting {
                ProgressView(value: Double(session.sent), total: Double(max(session.total, 1))) {
                    Text("Pasting \(session.sent) of \(session.total) characters…")
                }
            } else if let validation = session.validation {
                Text(validation).font(.callout).foregroundStyle(.secondary)
            }
            if !session.status.isEmpty {
                Text(session.status).font(.callout).accessibilityAddTraits(.updatesFrequently)
            }
            HStack {
                Toggle("Slow typing for older apps", isOn: $session.slow).disabled(session.isPasting)
                Spacer()
                if session.isPasting {
                    Button("Cancel Paste", role: .cancel) { session.cancel() }.keyboardShortcut(.cancelAction)
                } else {
                    Button("Paste Text") { session.paste() }
                        .buttonStyle(.borderedProminent)
                        .disabled(session.validation != nil || !manager.isRunning(session.id) || manager.isPaused(session.id))
                }
            }
        }
        .padding(22)
        .frame(minWidth: 540, minHeight: 410)
        .onAppear { editingText = true }
    }
}
