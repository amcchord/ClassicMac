import AppKit
import Combine
import SwiftUI
import PlatinumKit

/// Each running Mac's screen in a Platinum document window: title bar with
/// close (shut down), zoom and WindowShade boxes, the guest screen, and a
/// toolbar of common commands along the bottom.
@MainActor
final class GuestWindows: ObservableObject {
    static let shared = GuestWindows()

    static let toolbarHeight: CGFloat = 30

    @Published private(set) var scaleModes: [UUID: GuestDisplayScale] = [:]

    private final class Entry {
        let session: GuestDisplaySession
        var window: PlatinumWindow?
        var fullScreen: FullScreenWindow?
        var fullScreenContainer: GuestScreenContainerView?
        var started = Date()
        var lastRequestedSize: CGSize?
        var resizeWork: DispatchWorkItem?
        /// Surface sizes the window has already asked the guest to leave.
        var reassertedSizes: [CGSize: Int] = [:]
        var observers: [NSObjectProtocol] = []
        init(session: GuestDisplaySession) { self.session = session }
    }

    private var entries: [UUID: Entry] = [:]

    /// Whether ClassicMac draws the Mac's screen itself. Set the default
    /// ClassicMacUseQEMUWindow to use QEMU's own Cocoa window instead.
    static var isEnabled: Bool { !UserDefaults.standard.bool(forKey: "ClassicMacUseQEMUWindow") }

    func session(for id: UUID) -> GuestDisplaySession? { entries[id]?.session }
    func hasWindow(_ id: UUID) -> Bool { entries[id]?.window != nil }

    /// Starts (or reuses) the display session before QEMU launches, and
    /// returns its socket path.
    func prepare(_ config: VMConfig) throws -> String {
        let entry = entries[config.id] ?? Entry(session: GuestDisplaySession(vmID: config.id, family: config.machineFamily))
        entries[config.id] = entry
        entry.started = Date()
        try entry.session.start()
        return entry.session.socketPath
    }

    /// Shows the Mac's window, creating it on first use.
    func show(_ id: UUID) {
        guard let entry = entries[id], let config = VMStore.shared.vms.first(where: { $0.id == id }) else { return }
        if let window = entry.window {
            if window.state.isCollapsed { window.toggleCollapse() }
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }
        let screenSize = Self.initialScreenSize(for: config)
        var content = CGSize(width: screenSize.width, height: screenSize.height + Self.toolbarHeight)
        if let visible = NSScreen.main?.visibleFrame {
            let maxContent = PlatinumWindow.contentSize(forFrame: visible.size, kind: .document)
            content.width = min(content.width, maxContent.width)
            content.height = min(content.height, maxContent.height)
        }
        let session = entry.session
        let window = PlatinumWindow(title: config.name, kind: .document, boxes: .document, growable: true,
                                    contentSize: content) {
            GuestWindowContent(vmID: id, session: session, started: entry.started)
        }
        window.minSize = PlatinumWindow.frameSize(forContent: NSSize(width: 480, height: 320 + Self.toolbarHeight),
                                                  kind: .document)
        window.shouldClose = { [weak window] in
            // The close box asks Mac OS to shut down, like the power key.
            QEMUManager.shared.requestStop(id)
            _ = window
            return false
        }
        window.standardFrame = { [weak self] in self?.zoomFrame(for: id) }
        window.setFrameAutosaveName("ClassicMac Screen \(id.uuidString)")
        if !window.setFrameUsingName("ClassicMac Screen \(id.uuidString)") {
            window.center()
        }
        entry.window = window
        updateRefreshRate(id)
        entry.observers.append(NotificationCenter.default.addObserver(
            forName: NSWindow.didChangeScreenNotification, object: window, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated { self?.updateRefreshRate(id) }
            })
        entry.observers.append(NotificationCenter.default.addObserver(
            forName: NSWindow.didResizeNotification, object: window, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated { self?.windowResized(id) }
            })
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    /// The emulator stopped for good (not a restart): close the window.
    func machineStopped(_ id: UUID) {
        guard let entry = entries.removeValue(forKey: id) else { return }
        for observer in entry.observers { NotificationCenter.default.removeObserver(observer) }
        entry.fullScreen?.onDidExitFullScreen = nil
        entry.fullScreen?.close()
        entry.window?.shouldClose = nil
        entry.window?.close()
        entry.session.stop()
    }

    // MARK: Scale mode

    private static func scaleDefaultsKey(for id: UUID) -> String {
        "ClassicMacGuestScale.\(id.uuidString)"
    }

    func scaleMode(for id: UUID) -> GuestDisplayScale {
        if let cached = scaleModes[id] { return cached }
        if let raw = UserDefaults.standard.string(forKey: Self.scaleDefaultsKey(for: id)),
           let saved = GuestDisplayScale(rawValue: raw) {
            return saved
        }
        return followsWindow(id) ? .sharp : .fit
    }

    func setScaleMode(_ mode: GuestDisplayScale, for id: UUID) {
        scaleModes[id] = mode
        UserDefaults.standard.set(mode.rawValue, forKey: Self.scaleDefaultsKey(for: id))
        if let container = entries[id]?.fullScreenContainer {
            container.screenView.scaleMode = mode
            container.needsLayout = true
        }
        if mode == .sharp && followsWindow(id) {
            windowResized(id)
        }
    }

    /// Match QEMU's frame interval to the display (8 ms on a 120 Hz screen).
    private func updateRefreshRate(_ id: UUID) {
        guard let entry = entries[id] else { return }
        let fps = max(30, (entry.window?.screen ?? NSScreen.main)?.maximumFramesPerSecond ?? 60)
        entry.session.refreshInterval = UInt32(max(8, min(16, 1000 / fps)))
    }

    // MARK: Sizing

    private static func initialScreenSize(for config: VMConfig) -> CGSize {
        if config.machineFamily == .powerMac7500 { return CGSize(width: 640, height: 480) }
        if QEMUManager.effectiveGraphicsCard(for: config) == .atiRadeon {
            let size = ATIDisplay.size(fitting: config.width, height: config.height)
            return CGSize(width: size.width, height: size.height)
        }
        return CGSize(width: config.width, height: config.height)
    }

    /// Zoom: the largest whole-number scale of a fixed screen that fits, or
    /// for a resizable (Standard card) screen, the whole visible display.
    private func zoomFrame(for id: UUID) -> NSRect? {
        guard let entry = entries[id], let window = entry.window,
              let visible = (window.screen ?? NSScreen.main)?.visibleFrame else { return nil }
        let surface = entry.session.state.surfaceSize
        guard surface.width > 0 else { return nil }
        let mode = scaleMode(for: id)
        if followsWindow(id) && mode == .sharp {
            let rect = visible.insetBy(dx: 4, dy: 4)
            return NSRect(x: rect.minX, y: rect.minY, width: rect.width, height: rect.height)
        }
        let maxContent = PlatinumWindow.contentSize(forFrame: visible.size, kind: .document)
        let factor: CGFloat
        switch mode {
        case .oneX:
            factor = 1
        case .twoX:
            factor = 2
        case .sharp, .fit:
            factor = max(1, floor(min(maxContent.width / surface.width,
                                      (maxContent.height - Self.toolbarHeight) / surface.height)))
        }
        let content = NSSize(width: min(maxContent.width, surface.width * factor),
                             height: min(maxContent.height, surface.height * factor + Self.toolbarHeight))
        let size = PlatinumWindow.frameSize(forContent: content, kind: .document)
        return NSRect(x: visible.midX - size.width / 2, y: visible.maxY - size.height, width: size.width, height: size.height)
    }

    /// Standard-card Power Macs and enhanced-video Quadras follow the window.
    func followsWindow(_ id: UUID) -> Bool {
        guard let config = VMStore.shared.vms.first(where: { $0.id == id }) else { return false }
        switch config.machineFamily {
        case .powerMacG4: return true
        case .quadra800: return config.useEnhancedFramebuffer
        case .powerMacOSX, .powerMac7500: return false
        }
    }

    private func autoResizesGuest(_ id: UUID) -> Bool {
        followsWindow(id) && scaleMode(for: id) == .sharp
    }

    private func wantedGuestSize(for id: UUID, viewSize: CGSize) -> CGSize? {
        guard let config = VMStore.shared.vms.first(where: { $0.id == id }) else { return nil }
        var width = Int(viewSize.width)
        if config.machineFamily == .powerMacG4 { width = width / 8 * 8 }
        var height = Int(viewSize.height)
        if QEMUManager.effectiveGraphicsCard(for: config) == .atiRadeon {
            (width, height) = ATIDisplay.windowSize(width: width, height: height)
        }
        guard width >= VMConfig.minWidth, height >= VMConfig.minHeight else { return nil }
        return CGSize(width: width, height: height)
    }

    private func windowResized(_ id: UUID) {
        guard let entry = entries[id], let window = entry.window, autoResizesGuest(id),
              entry.fullScreen == nil, !window.state.isCollapsed else { return }
        entry.resizeWork?.cancel()
        let work = DispatchWorkItem { [weak self, weak window] in
            guard let self, let window, let entry = self.entries[id],
                  entry.fullScreen == nil, self.autoResizesGuest(id) else { return }
            let content = PlatinumWindow.contentSize(forFrame: window.frame.size, kind: .document)
            guard let size = self.wantedGuestSize(for: id, viewSize: CGSize(width: content.width, height: content.height - Self.toolbarHeight)),
                  size != entry.session.state.surfaceSize else { return }
            entry.lastRequestedSize = size
            entry.reassertedSizes.removeAll()
            entry.session.resize(width: Int(size.width), height: Int(size.height))
        }
        entry.resizeWork = work
        let live = window.inLiveResize || window.state.isGrowDragging
        DispatchQueue.main.asyncAfter(deadline: .now() + (live ? 0.35 : 0.1), execute: work)
    }

    /// The guest changed resolution. A screen that follows the window asks
    /// the guest to return to the window's size (Mac OS restores the size it
    /// last saved when the Finder starts), twice at most per size so a guest
    /// that cannot show it is never pestered; a fixed screen, or a size the
    /// guest insists on, gets a window that fits it pixel for pixel.
    func surfaceChanged(_ id: UUID, to size: CGSize) {
        guard let entry = entries[id], let window = entry.window, entry.fullScreen == nil,
              size.width > 0, !window.inLiveResize, !window.state.isGrowDragging,
              !window.state.isCollapsed else { return }
        let content = PlatinumWindow.contentSize(forFrame: window.frame.size, kind: .document)
        let rawContent = CGSize(width: content.width, height: content.height - Self.toolbarHeight)
        if let wanted = wantedGuestSize(for: id, viewSize: rawContent) {
            if size == wanted { return }
            if autoResizesGuest(id), entry.reassertedSizes[size, default: 0] < 2 {
                entry.reassertedSizes[size, default: 0] += 1
                entry.lastRequestedSize = wanted
                entry.session.resize(width: Int(wanted.width), height: Int(wanted.height))
                return
            }
        }
        guard size.width > rawContent.width || size.height > rawContent.height || autoResizesGuest(id) else { return }
        var frame = window.frame
        let newSize = PlatinumWindow.frameSize(forContent: NSSize(width: size.width, height: size.height + Self.toolbarHeight),
                                               kind: .document)
        if let visible = window.screen?.visibleFrame, newSize.width > visible.width || newSize.height > visible.height {
            return
        }
        frame.origin.y += frame.height - newSize.height
        frame.size = newSize
        window.setFrame(frame, display: true, animate: false)
    }

    // MARK: Full screen

    func toggleFullScreen(_ id: UUID) {
        guard let entry = entries[id] else { return }
        if let full = entry.fullScreen {
            if full.styleMask.contains(.fullScreen) {
                full.toggleFullScreen(nil)
            } else {
                entry.fullScreen = nil
                entry.fullScreenContainer = nil
                full.close()
                entry.window?.makeKeyAndOrderFront(nil)
            }
            return
        }
        let targetScreen = entry.window?.screen ?? NSScreen.main
        let window = FullScreenWindow(contentRect: targetScreen?.frame ?? .zero, styleMask: [.titled, .closable],
                                      backing: .buffered, defer: false)
        if let targetScreen {
            window.setFrame(targetScreen.frame, display: false)
        }
        window.isReleasedWhenClosed = false
        window.collectionBehavior = [.fullScreenPrimary]
        window.backgroundColor = .black
        let config = VMStore.shared.vms.first { $0.id == id }
        let container = GuestScreenContainerView(session: entry.session)
        container.backgroundColor = .black
        let screen = container.screenView
        screen.useBlackBackground = true
        screen.scaleMode = scaleMode(for: id)
        screen.inputHelpers = config?.classicInputHelpers ?? true
        screen.legacyMouseTracking = config?.legacyMouseTracking ?? true
        screen.mouseDPI = config?.mouseDPI
        screen.isPaused = QEMUManager.shared.isPaused(id)
        screen.onToggleFullScreen = { [weak self] in self?.toggleFullScreen(id) }

        let host = FullScreenHostView(container: container, session: entry.session)
        screen.onCaptureChanged = { [weak host] captured in
            host?.updateBanner(captured: captured)
        }
        window.contentView = host
        window.onFullScreenResize = { [weak self] screenSize in
            guard let self, let entry = self.entries[id], self.followsWindow(id) else { return }
            if let wanted = self.wantedGuestSize(for: id, viewSize: screenSize),
               wanted != entry.session.state.surfaceSize {
                entry.session.resize(width: Int(wanted.width), height: Int(wanted.height))
            }
        }
        window.onDidExitFullScreen = { [weak self] in
            guard let self, let entry = self.entries[id], entry.fullScreen === window else { return }
            entry.fullScreen = nil
            entry.fullScreenContainer = nil
            window.close()
            entry.window?.makeKeyAndOrderFront(nil)
            self.windowResized(id)
        }
        entry.fullScreen = window
        entry.fullScreenContainer = container
        window.makeKeyAndOrderFront(nil)
        window.makeFirstResponder(screen)
        window.toggleFullScreen(nil)
    }
}

private final class FullScreenWindow: NSWindow, NSWindowDelegate {
    var onDidExitFullScreen: (() -> Void)?
    var onFullScreenResize: ((CGSize) -> Void)?
    private var didExit = false

    override init(contentRect: NSRect, styleMask style: NSWindow.StyleMask,
                  backing backingStoreType: NSWindow.BackingStoreType, defer flag: Bool) {
        super.init(contentRect: contentRect, styleMask: style, backing: backingStoreType, defer: flag)
        delegate = self
    }

    func windowDidEnterFullScreen(_ notification: Notification) {
        if let screenSize = (screen ?? NSScreen.main)?.frame.size {
            onFullScreenResize?(screenSize)
        }
    }

    func windowDidResize(_ notification: Notification) {
        guard styleMask.contains(.fullScreen) else { return }
        onFullScreenResize?(frame.size)
    }

    func windowDidExitFullScreen(_ notification: Notification) {
        finishExit()
    }

    func windowWillClose(_ notification: Notification) {
        finishExit()
    }

    private func finishExit() {
        guard !didExit else { return }
        didExit = true
        onDidExitFullScreen?()
    }
}

/// Hosts the full-screen guest screen alongside a non-interactive status pill
/// showing mouse capture state and the Control-Command-F exit hint.
@MainActor
private final class FullScreenHostView: NSView {
    private let container: GuestScreenContainerView
    private let session: GuestDisplaySession
    private let banner = NSTextField(labelWithString: "")
    private var hideWork: DispatchWorkItem?
    private var subscriptions: Set<AnyCancellable> = []

    init(container: GuestScreenContainerView, session: GuestDisplaySession) {
        self.container = container
        self.session = session
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor

        container.translatesAutoresizingMaskIntoConstraints = false
        addSubview(container)

        banner.font = PlatinumFont.small.nsFont()
        banner.textColor = .white
        banner.backgroundColor = NSColor.black.withAlphaComponent(0.75)
        banner.drawsBackground = true
        banner.alignment = .center
        banner.isBezeled = false
        banner.isEditable = false
        banner.isSelectable = false
        banner.wantsLayer = true
        banner.layer?.cornerRadius = 4
        banner.layer?.borderWidth = 1
        banner.layer?.borderColor = NSColor.white.withAlphaComponent(0.3).cgColor
        banner.translatesAutoresizingMaskIntoConstraints = false
        addSubview(banner)

        NSLayoutConstraint.activate([
            container.leadingAnchor.constraint(equalTo: leadingAnchor),
            container.trailingAnchor.constraint(equalTo: trailingAnchor),
            container.topAnchor.constraint(equalTo: topAnchor),
            container.bottomAnchor.constraint(equalTo: bottomAnchor),
            banner.centerXAnchor.constraint(equalTo: centerXAnchor),
            banner.topAnchor.constraint(equalTo: topAnchor, constant: 18),
            banner.widthAnchor.constraint(greaterThanOrEqualToConstant: 240),
        ])

        QEMUManager.shared.$pausedIDs.sink { [weak self] pausedIDs in
            guard let self else { return }
            self.container.screenView.isPaused = pausedIDs.contains(self.session.vmID)
        }.store(in: &subscriptions)
        session.state.$pointerRelative.sink { [weak self] _ in
            guard let self else { return }
            self.updateBanner(captured: self.container.screenView.isMouseCaptured)
        }.store(in: &subscriptions)
        updateBanner(captured: false, showExitHint: true)
    }

    required init?(coder: NSCoder) { fatalError() }

    deinit {
        hideWork?.cancel()
    }

    override func hitTest(_ point: NSPoint) -> NSView? {
        let hit = super.hitTest(point)
        return hit === banner ? container.screenView : hit
    }

    func updateBanner(captured: Bool, showExitHint: Bool = false) {
        hideWork?.cancel()
        if captured {
            banner.stringValue = "  The mouse is captured. Press Control-Option to release it. (Control-Command-F exits full screen)  "
            banner.isHidden = false
            return
        }
        if session.state.pointerRelative {
            banner.stringValue = "  Click the screen to capture the mouse. Press Control-Command-F to exit full screen.  "
            banner.isHidden = false
        } else if showExitHint {
            banner.stringValue = "  Press Control-Command-F to exit full screen.  "
            banner.isHidden = false
            let work = DispatchWorkItem { [weak self] in
                self?.banner.isHidden = true
            }
            hideWork = work
            DispatchQueue.main.asyncAfter(deadline: .now() + 3.0, execute: work)
        } else {
            banner.isHidden = true
        }
    }
}

/// The window's content: the screen, the startup curtain, and the toolbar.
struct GuestWindowContent: View {
    let vmID: UUID
    let session: GuestDisplaySession
    let started: Date
    @ObservedObject private var state: GuestDisplayState
    @ObservedObject private var manager = QEMUManager.shared
    @ObservedObject private var store = VMStore.shared
    @ObservedObject private var windows = GuestWindows.shared
    @State private var captured = false
    @State private var curtainStart: Date

    init(vmID: UUID, session: GuestDisplaySession, started: Date) {
        self.vmID = vmID
        self.session = session
        self.started = started
        _state = ObservedObject(wrappedValue: MainActor.assumeIsolated { session.state })
        _curtainStart = State(initialValue: started)
    }

    private var config: VMConfig? { store.vms.first { $0.id == vmID } }
    private var paused: Bool { manager.isPaused(vmID) }
    private var scaleBinding: Binding<GuestDisplayScale> {
        Binding(
            get: { windows.scaleMode(for: vmID) },
            set: { windows.setScaleMode($0, for: vmID) }
        )
    }

    var body: some View {
        VStack(spacing: 0) {
            ZStack {
                GuestScreen(session: session,
                            inputHelpers: config?.classicInputHelpers ?? true,
                            legacyMouseTracking: config?.legacyMouseTracking ?? true,
                            mouseDPI: config?.mouseDPI,
                            scaleMode: windows.scaleMode(for: vmID),
                            isPaused: paused,
                            onToggleFullScreen: { GuestWindows.shared.toggleFullScreen(vmID) },
                            onCaptureChanged: { captured = $0 })
                if !state.revealed {
                    StartupCurtain(started: curtainStart, screenSize: state.surfaceSize)
                        .transition(.opacity)
                        .allowsHitTesting(false)
                }
                if paused {
                    Text("Paused")
                        .platinumText(.emphasis)
                        .padding(.horizontal, 12)
                        .frame(height: 22)
                        .background(Platinum.face.color)
                        .overlay(Rectangle().stroke(Color.black, lineWidth: 1))
                        .allowsHitTesting(false)
                }
            }
            .animation(.easeOut(duration: 0.35), value: state.revealed)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            toolbar
        }
        .onChange(of: state.bootCount) { _, _ in curtainStart = Date() }
        .onChange(of: state.surfaceSize) { _, size in GuestWindows.shared.surfaceChanged(vmID, to: size) }
    }

    private var toolbar: some View {
        PlatinumPlacardBar {
            ViewThatFits(in: .horizontal) {
                toolbarButtons(compact: false)
                toolbarButtons(compact: true)
            }
        }
    }

    private func toolbarButtons(compact: Bool) -> some View {
        HStack(spacing: 6) {
            PlatinumBevelButton(compact ? nil : "Shut Down…", icon: "power") { manager.requestStop(vmID) }
                .help("Shut Down…")
                .accessibilityLabel("Shut Down…")
            PlatinumBevelButton("Restart") { manager.reboot(vmID) }
            PlatinumBevelButton(paused ? "Resume" : "Pause") {
                if paused { manager.resume(vmID) } else { manager.pause(vmID) }
            }
            if config?.machineFamily != .powerMac7500 {
                PlatinumBevelButton(compact ? nil : "Media…", icon: "cd") { MediaController.shared.present(for: vmID) }
                    .help("Media…")
                    .accessibilityLabel("Media…")
                PlatinumBevelButton(compact ? nil : "Paste Text…", icon: "clipboard") { PasteTextController.shared.present(for: vmID) }
                    .disabled(paused || !state.revealed)
                    .help("Paste Text…")
                    .accessibilityLabel("Paste Text…")
            }
            Spacer(minLength: 6)
            if !compact {
                Text(status)
                    .platinumText(.detail)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
            Spacer(minLength: 6)
            PlatinumPopUp(selection: scaleBinding,
                          options: GuestDisplayScale.allCases.map { ($0, $0.label) },
                          width: compact ? 88 : 102)
                .help("Screen Scale")
            PlatinumBevelButton(compact ? nil : "Full Screen", icon: "display") { GuestWindows.shared.toggleFullScreen(vmID) }
                .padding(.trailing, 14)
                .help("Full Screen")
                .accessibilityLabel("Full Screen")
        }
    }

    private var status: String {
        if captured { return "The mouse is captured. Press Control-Option to release it." }
        if state.revealed && state.pointerRelative { return "Click the screen to capture the mouse for this game." }
        if !state.revealed { return "Starting up…" }
        var parts: [String] = []
        if let config, QEMUManager.effectiveGraphicsCard(for: config) == .atiRadeon {
            if let renderer = manager.atiRenderers[vmID], !renderer.isEmpty {
                parts.append("ATI Radeon, 3D on \(renderer)")
            } else {
                parts.append("ATI Radeon")
            }
        }
        if state.surfaceSize.width > 0 {
            parts.append("\(Int(state.surfaceSize.width)) × \(Int(state.surfaceSize.height))")
        }
        return parts.joined(separator: " · ")
    }
}
