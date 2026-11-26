import AppKit
import SwiftUI
import PlatinumKit
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
        let window = PlatinumWindow(title: "Paste Text into \(vm.name)", kind: .document,
                                    boxes: Chrome.TitleBoxes(close: true, collapse: true), growable: true,
                                    contentSize: NSSize(width: 560, height: 420)) {
            PasteTextView(session: session)
        }
        window.minSize = PlatinumWindow.frameSize(forContent: NSSize(width: 480, height: 360), kind: .document)
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
        let env = ProcessInfo.processInfo.environment
        if env["CLASSICMAC_UI_SNAPSHOTS"] != nil, let sample = env["CLASSICMAC_UI_PASTE_SAMPLE"] {
            text = sample
        } else {
            text = NSPasteboard.general.string(forType: .string) ?? ""
        }
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

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .top, spacing: 12) {
                PlatinumIcon("note", size: 32)
                VStack(alignment: .leading, spacing: 4) {
                    Text("Type this text into \u{201C}\(session.name)\u{201D}.").platinumText(.emphasis)
                    Text("Place the text cursor in the Mac first. ClassicMac types the text using the Mac's U.S. keyboard layout with Caps Lock off. Tabs and line breaks press Tab and Return.")
                        .platinumText(.small)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            PlatinumWell {
                PlatinumTextEditor(text: $session.text, editable: !session.isPasting)
            }
            .frame(maxHeight: .infinity)
            if session.isPasting {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Typing \(session.sent) of \(session.total) characters…").platinumText(.small)
                    PlatinumProgressBar(value: Double(session.sent) / Double(max(session.total, 1)))
                }
            } else if let validation = session.validation {
                Text(validation).platinumText(.small)
            }
            if !session.status.isEmpty {
                Text(session.status).platinumText(.detail).accessibilityAddTraits(.updatesFrequently)
            }
            HStack(spacing: Platinum.buttonGap) {
                PlatinumCheckbox("Type slowly for older apps", isOn: $session.slow)
                    .disabled(session.isPasting)
                Spacer()
                if session.isPasting {
                    PlatinumButton("Stop", role: .cancel) { session.cancel() }
                } else {
                    PlatinumButton("Paste Text", role: .default, minWidth: 90) { session.paste() }
                        .disabled(session.validation != nil || !manager.isRunning(session.id) || manager.isPaused(session.id))
                }
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Platinum.face.color)
    }
}

/// A multi-line text area in the Platinum regular font.
private struct PlatinumTextEditor: NSViewRepresentable {
    @Binding var text: String
    let editable: Bool

    func makeNSView(context: Context) -> NSScrollView {
        let scroll = NSTextView.scrollableTextView()
        scroll.drawsBackground = false
        scroll.hasVerticalScroller = true
        scroll.borderType = .noBorder
        guard let view = scroll.documentView as? NSTextView else { return scroll }
        view.font = PlatinumFont.regular.nsFont()
        view.textColor = .black
        view.backgroundColor = .white
        view.insertionPointColor = .black
        view.selectedTextAttributes = [.backgroundColor: NSColor(Platinum.highlight.color), .foregroundColor: NSColor.black]
        view.isRichText = false
        view.allowsUndo = true
        view.textContainerInset = NSSize(width: 3, height: 3)
        view.isAutomaticQuoteSubstitutionEnabled = false
        view.isAutomaticDashSubstitutionEnabled = false
        view.delegate = context.coordinator
        view.string = text
        DispatchQueue.main.async { view.window?.makeFirstResponder(view) }
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        context.coordinator.parent = self
        guard let view = scroll.documentView as? NSTextView else { return }
        if view.string != text { view.string = text }
        view.isEditable = editable
    }

    func makeCoordinator() -> Coordinator { Coordinator(parent: self) }

    final class Coordinator: NSObject, NSTextViewDelegate {
        var parent: PlatinumTextEditor
        init(parent: PlatinumTextEditor) { self.parent = parent }
        func textDidChange(_ notification: Notification) {
            if let view = notification.object as? NSTextView { parent.text = view.string }
        }
    }
}
