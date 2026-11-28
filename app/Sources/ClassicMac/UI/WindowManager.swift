import AppKit
import Combine
import SwiftUI
import PlatinumKit

/// Closes the dialog that hosts a view.
@MainActor
final class DialogHandle {
    weak var window: PlatinumWindow?
    func close() { window?.close() }
}

/// Owns ClassicMac's Platinum windows and presents its dialogs and alerts.
@MainActor
final class WindowManager {
    static let shared = WindowManager()
    static let didShowMainNotification = Notification.Name("com.classicmac.didShowMain")

    private(set) var mainWindow: PlatinumWindow?
    private var libraryWindow: PlatinumWindow?
    var libraryWindowRef: PlatinumWindow? { libraryWindow }
    private var aboutWindow: PlatinumWindow?
    private var subscriptions: Set<AnyCancellable> = []
    private(set) var showingError = false
    private var pendingErrors: [(error: AppError, clear: () -> Void)] = []
    private weak var shutDownAlert: PlatinumWindow?
    private weak var updateWindow: PlatinumWindow?
    private var lastAutoPresentedUpdateVersion: String?

    static let mainContentSize = NSSize(width: 780, height: 500)
    static let mainMinimumContent = NSSize(width: 660, height: 440)

    var mainIsBlocked: Bool { mainWindow?.hasModalDialog ?? false }

    func start() {
        showMain()
        let store = VMStore.shared, manager = QEMUManager.shared
        store.$lastError.compactMap { $0 }.receive(on: RunLoop.main)
            .sink { [weak self] error in self?.presentError(error) { store.lastError = nil } }
            .store(in: &subscriptions)
        manager.$lastError.compactMap { $0 }.receive(on: RunLoop.main)
            .sink { [weak self] error in self?.presentError(error) { manager.lastError = nil } }
            .store(in: &subscriptions)
        manager.$pendingMissingPippinROMVM.compactMap { $0 }.receive(on: RunLoop.main)
            .sink { [weak self] vm in
                manager.pendingMissingPippinROMVM = nil
                self?.promptForMissingPippinROM(vm)
            }
            .store(in: &subscriptions)
        manager.$pendingStopID.receive(on: RunLoop.main)
            .sink { [weak self] id in
                if let id, manager.pendingStopID == id {
                    self?.confirmShutDown(id)
                } else if manager.pendingStopID == nil {
                    // Confirmed or cancelled elsewhere: take the question down.
                    self?.shutDownAlert?.close()
                }
            }
            .store(in: &subscriptions)
        store.$isPresentingNewVM.filter { $0 }.receive(on: RunLoop.main)
            .sink { [weak self] _ in
                store.isPresentingNewVM = false
                self?.presentNewMac()
            }
            .store(in: &subscriptions)
        NotificationCenter.default.publisher(for: .classicMacCheckForUpdates)
            .receive(on: RunLoop.main)
            .sink { [weak self] _ in
                self?.showUpdateChecker(interactive: true)
            }
            .store(in: &subscriptions)
        AppUpdater.shared.$state
            .receive(on: RunLoop.main)
            .sink { [weak self] state in
                guard let self, case .available(let release) = state else { return }
                guard self.updateWindow == nil,
                      !self.mainIsBlocked,
                      self.lastAutoPresentedUpdateVersion != release.version else { return }
                self.lastAutoPresentedUpdateVersion = release.version
                self.showUpdateChecker(interactive: false)
            }
            .store(in: &subscriptions)
    }

    // MARK: Windows

    func showMain() {
        if let mainWindow {
            mainWindow.makeKeyAndOrderFront(nil)
            NotificationCenter.default.post(name: Self.didShowMainNotification, object: nil)
            return
        }
        let window = PlatinumWindow(title: "ClassicMac", kind: .document, boxes: .document, growable: true,
                                    contentSize: Self.mainContentSize) {
            HomeView()
        }
        window.minSize = PlatinumWindow.frameSize(forContent: Self.mainMinimumContent, kind: .document)
        window.center()
        window.setFrameAutosaveName("ClassicMac Main Window")
        window.isExcludedFromWindowsMenu = true
        mainWindow = window
        window.makeKeyAndOrderFront(nil)
    }

    func showLibrary() {
        if let libraryWindow {
            libraryWindow.makeKeyAndOrderFront(nil)
            return
        }
        let window = PlatinumWindow(title: "Mac OS Library", kind: .document, boxes: .document, growable: true,
                                    contentSize: NSSize(width: 760, height: 520)) {
            LibraryView()
        }
        window.minSize = PlatinumWindow.frameSize(forContent: NSSize(width: 680, height: 460), kind: .document)
        window.setFrameAutosaveName("ClassicMac Library Window")
        if !window.setFrameUsingName("ClassicMac Library Window") {
            window.center()
            if let main = mainWindow {
                window.setFrameTopLeftPoint(NSPoint(x: main.frame.minX + 30, y: main.frame.maxY - 30))
            }
        }
        window.isExcludedFromWindowsMenu = true
        libraryWindow = window
        window.makeKeyAndOrderFront(nil)
    }

    func showAbout() {
        if let aboutWindow {
            aboutWindow.makeKeyAndOrderFront(nil)
            return
        }
        let window = PlatinumWindow(title: "About ClassicMac", kind: .document,
                                    boxes: Chrome.TitleBoxes(close: true, collapse: true),
                                    contentSize: NSSize(width: 460, height: 328)) {
            AboutView()
        }
        window.center()
        aboutWindow = window
        window.makeKeyAndOrderFront(nil)
    }

    // MARK: Dialogs

    /// Presents a movable modal dialog in front of `parent` (the main window
    /// by default), or on its own when there is no such window.
    @discardableResult
    func presentDialog<Content: View>(title: String, size: NSSize, parent: PlatinumWindow? = nil,
                                      @ViewBuilder content: (DialogHandle) -> Content) -> PlatinumWindow {
        let handle = DialogHandle()
        let window = PlatinumWindow(title: title, kind: .movableModal, contentSize: size) {
            content(handle)
        }
        handle.window = window
        if let host = parent ?? mainWindow, host.isVisible {
            host.presentModal(window)
        } else {
            window.presentAlone()
        }
        return window
    }

    func presentSettings(for id: UUID, initialPane: SettingsDialog.Pane? = nil) {
        guard let vm = VMStore.shared.vms.first(where: { $0.id == id }) else { return }
        showMain()
        presentDialog(title: "\(vm.name) Info", size: SettingsDialog.size) { handle in
            SettingsDialog(original: vm, handle: handle, initialPane: initialPane)
        }
    }

    func presentCheckpoints(for id: UUID) {
        guard let vm = VMStore.shared.vms.first(where: { $0.id == id }) else { return }
        showMain()
        presentDialog(title: "\(vm.name) Checkpoints", size: CheckpointsDialog.size) { handle in
            CheckpointsDialog(vm: vm, handle: handle)
        }
    }

    func presentIconPacks() {
        showMain()
        presentDialog(title: "Iconfactory Icons", size: IconPacksDialog.size) { handle in
            IconPacksDialog(handle: handle)
        }
    }

    func presentNewMac() {
        showMain()
        presentDialog(title: "New Mac", size: NewMacDialog.size) { handle in
            NewMacDialog(handle: handle)
        }
    }

    func presentPippinROMDownload(
        machineName: String? = nil,
        parent: PlatinumWindow? = nil,
        onChooseLocal: (() -> Void)? = nil,
        onInstalled: @escaping (URL) -> Void
    ) {
        if parent == nil { showMain() }
        presentDialog(title: "Download Pippin Boot ROM", size: PippinROMDownloadDialog.size, parent: parent) { handle in
            PippinROMDownloadDialog(
                machineName: machineName,
                handle: handle,
                onChooseLocal: onChooseLocal,
                onInstalled: onInstalled
            )
        }
    }

    private func promptForMissingPippinROM(_ vm: VMConfig) {
        showMain()
        presentPippinROMDownload(
            machineName: vm.name,
            parent: mainWindow,
            onChooseLocal: { [weak self] in
                self?.chooseLocalPippinROM(for: vm)
            },
            onInstalled: { _ in
                let latest = VMStore.shared.vms.first(where: { $0.id == vm.id }) ?? vm
                QEMUManager.shared.start(latest)
            }
        )
    }

    private func chooseLocalPippinROM(for vm: VMConfig) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.message = "Choose a 4 MB Apple / Bandai Pippin Kinka boot ROM"
        panel.prompt = "Choose"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            _ = try PippinMachine.validateROM(at: url)
            var updated = VMStore.shared.vms.first(where: { $0.id == vm.id }) ?? vm
            updated.pippinROMPath = url.path
            _ = VMStore.shared.save(updated)
            QEMUManager.shared.start(updated)
        } catch {
            presentError(AppError("Invalid Pippin Boot ROM", error.localizedDescription)) {}
        }
    }

    func showUpdateChecker(interactive: Bool = true) {
        if let updateWindow, updateWindow.isVisible {
            updateWindow.makeKeyAndOrderFront(nil)
            return
        }
        let updater = AppUpdater.shared
        if interactive && !updater.isBusy {
            switch updater.state {
            case .available, .readyToInstall:
                break
            default:
                updater.checkForUpdates(interactive: true)
            }
        }
        updateWindow = presentDialog(title: "Software Update", size: UpdateDialog.size) { handle in
            UpdateDialog(handle: handle)
        }
    }

    // MARK: Alerts

    /// A Platinum alert. Stop and caution alerts beep, as Mac OS's did.
    @discardableResult
    func alert(_ icon: PlatinumAlertIcon, _ message: String, informative: String? = nil,
               buttons: [PlatinumAlertButton], parent: PlatinumWindow? = nil, width: CGFloat = 420) -> PlatinumWindow {
        let handle = DialogHandle()
        let window = PlatinumWindow.dialog(kind: .modal) {
            PlatinumAlert(icon: icon, message: message, informative: informative, buttons: buttons,
                          width: width) { handle.close() }
        }
        handle.window = window
        if icon != .note { NSSound.beep() }
        if let host = parent ?? (NSApp.keyWindow as? PlatinumWindow) ?? mainWindow, host.isVisible {
            host.presentModal(window)
        } else {
            window.presentAlone()
        }
        return window
    }

    func presentError(_ error: AppError, clear: @escaping () -> Void) {
        if showingError {
            pendingErrors.append((error, clear))
            return
        }
        showingError = true
        var buttons = [PlatinumAlertButton("OK", role: .default)]
        if let log = error.logURL {
            buttons.append(PlatinumAlertButton("Show Details") {
                NSWorkspace.shared.activateFileViewerSelecting([log])
            })
        }
        let window = alert(.stop, error.title, informative: error.message, buttons: buttons)
        window.onClose = { [weak self] in
            clear()
            guard let self else { return }
            self.showingError = false
            if !self.pendingErrors.isEmpty {
                let next = self.pendingErrors.removeFirst()
                self.presentError(next.error, clear: next.clear)
            }
        }
    }

    private func confirmShutDown(_ id: UUID) {
        let manager = QEMUManager.shared
        let vm = VMStore.shared.vms.first { $0.id == id }
        let name = vm.map { "\u{201C}\($0.name)\u{201D}" } ?? "this Mac"
        if vm?.machineFamily == .powerMacOSX {
            shutDownAlert?.close()
            shutDownAlert = alert(.caution, "Shut down \(name)?",
                informative: "Mac OS X shuts down cleanly from the Apple menu so its disk stays healthy. Force Stop turns it off immediately and unsaved work may be lost.",
                buttons: [
                    PlatinumAlertButton("Shut Down", role: .default) { manager.confirmStop() },
                    PlatinumAlertButton("Force Stop") { manager.confirmForceStop() },
                    PlatinumAlertButton("Cancel", role: .cancel) { manager.cancelStop() },
                ])
            return
        }
        let informative = vm?.machineFamily == .powerMac7500
            ? "This turns Copland off at once and may lose unsaved work. For a clean shut down, choose Cancel and use Shut Down in Copland's Spaz menu."
            : "Mac OS shuts down first, as if you chose Shut Down from the Special menu, so its disk stays healthy. If Mac OS does not respond within 15 seconds, the Mac turns off and unsaved work may be lost."
        shutDownAlert?.close()
        shutDownAlert = alert(.caution, "Shut down \(name)?", informative: informative, buttons: [
            PlatinumAlertButton("Shut Down", role: .default) { manager.confirmStop() },
            PlatinumAlertButton("Cancel", role: .cancel) { manager.cancelStop() },
        ])
    }

    func confirmRemove(_ vm: VMConfig) {
        let store = VMStore.shared
        let manager = QEMUManager.shared
        let bytes = VMStore.bundleDiskUsageBytes(for: vm.folder)
        let sizeText = ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file)
        let displayPath = (vm.folder.path as NSString).abbreviatingWithTildeInPath

        if manager.isRunning(vm.id) {
            alert(.caution, "Delete running Mac \u{201C}\(vm.name)\u{201D}?",
                  informative: "\u{201C}\(vm.name)\u{201D} is currently running (\(sizeText) on disk at \(displayPath)). Shut it down safely first or force stop it, then move its files to the Trash.",
                  buttons: [
                    PlatinumAlertButton("Cancel", role: .cancel),
                    PlatinumAlertButton("Shut Down & Move to Trash") {
                        manager.stopAndWait(vm.id, force: false) { store.moveToTrash(vm) }
                    },
                    PlatinumAlertButton("Force Stop & Move to Trash") {
                        manager.stopAndWait(vm.id, force: true) { store.moveToTrash(vm) }
                    },
                  ], width: 500)
        } else {
            alert(.caution, "Delete \u{201C}\(vm.name)\u{201D}?",
                  informative: "Move to Trash deletes \u{201C}\(vm.folder.lastPathComponent)\u{201D} (\(sizeText) on disk) and frees space; you can restore it from the Trash later.\n\nRemove from List hides it from ClassicMac without deleting its files.",
                  buttons: [
                    PlatinumAlertButton("Cancel", role: .cancel),
                    PlatinumAlertButton("Remove from List") { store.removeFromLibrary(vm) },
                    PlatinumAlertButton("Move to Trash") { store.moveToTrash(vm) },
                  ], width: 460)
        }
    }
}
