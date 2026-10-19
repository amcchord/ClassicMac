#if os(macOS)
import AppKit
import SwiftUI

/// A borderless window that draws its own Platinum frame. Square corners, a
/// one-pixel hard shadow, WindowShade collapsing, zoom, a grow box and
/// movable modal dialogs that block their parent window.
open class PlatinumWindow: NSWindow {
    public let kind: Chrome.WindowKind
    public let boxes: Chrome.TitleBoxes
    public let state: PlatinumWindowState

    /// The frame the zoom box toggles to; defaults to the screen's visible frame.
    public var standardFrame: (() -> NSRect?)?
    /// Called before the window closes from its close box or Command-W.
    /// Return false to keep it open.
    public var shouldClose: (() -> Bool)?
    public var onClose: (() -> Void)?

    private var expandedHeight: CGFloat?
    private var userFrame: NSRect?
    private weak var modalChild: PlatinumWindow?
    private weak var modalParent: PlatinumWindow?
    private var activationObservers: [NSObjectProtocol] = []

    public static func frameSize(forContent size: NSSize, kind: Chrome.WindowKind) -> NSSize {
        let i = Chrome.contentInsets(kind)
        return NSSize(width: size.width + CGFloat(i.left + i.right + 1),
                      height: size.height + CGFloat(i.top + i.bottom + 1))
    }

    public static func contentSize(forFrame size: NSSize, kind: Chrome.WindowKind) -> NSSize {
        let i = Chrome.contentInsets(kind)
        return NSSize(width: size.width - CGFloat(i.left + i.right + 1),
                      height: size.height - CGFloat(i.top + i.bottom + 1))
    }

    /// - Parameters:
    ///   - contentSize: size of the content area inside the frame.
    ///   - growable: shows the grow box and allows resizing.
    public init<Content: View>(title: String, kind: Chrome.WindowKind, boxes: Chrome.TitleBoxes = .none,
                               growable: Bool = false, contentSize: NSSize,
                               @ViewBuilder content: () -> Content) {
        self.kind = kind
        self.boxes = boxes
        self.state = PlatinumWindowState(title: title)
        let size = Self.frameSize(forContent: contentSize, kind: kind)
        var mask: NSWindow.StyleMask = [.borderless, .miniaturizable]
        if growable { mask.insert(.resizable) }
        super.init(contentRect: NSRect(origin: .zero, size: size), styleMask: mask,
                   backing: .buffered, defer: false)
        PlatinumFonts.registerIfNeeded()
        self.title = title
        isOpaque = false
        backgroundColor = .clear
        hasShadow = false
        isReleasedWhenClosed = false
        isMovableByWindowBackground = false
        acceptsMouseMovedEvents = true
        collectionBehavior = [.fullScreenNone, .managed]
        if kind == .utility { level = .floating }

        let root = PlatinumWindowChrome(state: state, kind: kind, boxes: boxes, growable: growable, content: content)
        let host = NSHostingView(rootView: root)
        host.sizingOptions = []
        host.autoresizingMask = [.width, .height]
        host.frame = NSRect(origin: .zero, size: size)
        contentView = host

        state.close = { [weak self] in self?.performClose(nil) }
        state.zoom = { [weak self] in self?.toggleZoom() }
        state.collapse = { [weak self] in self?.toggleCollapse() }
        state.blockedClick = { [weak self] in
            NSSound.beep()
            self?.modalChild?.makeKeyAndOrderFront(nil)
        }
        for name in [NSApplication.didBecomeActiveNotification, NSApplication.didResignActiveNotification] {
            activationObservers.append(NotificationCenter.default.addObserver(forName: name, object: NSApp, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated {
                    guard let self else { return }
                    self.state.isActive = self.isKeyWindow && NSApp.isActive
                }
            })
        }
    }

    deinit { activationObservers.forEach(NotificationCenter.default.removeObserver) }

    open override var canBecomeKey: Bool { true }
    open override var canBecomeMain: Bool { kind == .document }

    open override func becomeKey() {
        super.becomeKey()
        state.isActive = NSApp.isActive
    }

    open override func resignKey() {
        super.resignKey()
        state.isActive = false
    }

    open override var title: String {
        didSet { if state.title != title { state.title = title } }
    }

    // MARK: Close

    open override func performClose(_ sender: Any?) {
        guard modalChild == nil else { NSSound.beep(); return }
        if let shouldClose, !shouldClose() { return }
        close()
    }

    private var didClose = false

    open override func order(_ place: NSWindow.OrderingMode, relativeTo otherWin: Int) {
        if place != .out { didClose = false }
        super.order(place, relativeTo: otherWin)
    }

    open override func close() {
        guard !didClose else { return }
        didClose = true
        if let parent = modalParent {
            modalParent = nil
            if parent.modalChild === self {
                parent.modalChild = nil
                parent.state.isBlockedByModal = false
            }
            parent.removeChildWindow(self)
            parent.makeKeyAndOrderFront(nil)
        }
        super.close()
        onClose?()
    }

    // Command-W in a borderless window needs the menu validation to agree.
    open override func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        switch menuItem.action {
        case #selector(performClose(_:)):
            return boxes.close || kind != .document
        case #selector(performZoom(_:)):
            return boxes.zoom
        case #selector(performMiniaturize(_:)):
            return kind == .document
        default:
            return super.validateMenuItem(menuItem)
        }
    }

    open override func performZoom(_ sender: Any?) { toggleZoom() }

    open override func performMiniaturize(_ sender: Any?) { miniaturize(sender) }

    // MARK: WindowShade

    public func toggleCollapse() {
        let collapsedHeight = CGFloat(Chrome.contentInsets(kind).top + 1)
        var frame = self.frame
        if state.isCollapsed {
            let height = expandedHeight ?? max(collapsedHeight + 100, minSize.height)
            frame.origin.y -= height - frame.height
            frame.size.height = height
            state.isCollapsed = false
            setFrame(frame, display: true, animate: true)
        } else {
            expandedHeight = frame.height
            frame.origin.y += frame.height - collapsedHeight
            frame.size.height = collapsedHeight
            let savedMin = minSize
            minSize = NSSize(width: savedMin.width, height: collapsedHeight)
            setFrame(frame, display: true, animate: true)
            minSize = savedMin
            state.isCollapsed = true
        }
    }

    open override func setFrame(_ frameRect: NSRect, display flag: Bool) {
        // Collapsed windows stay one title bar tall.
        if state.isCollapsed {
            var rect = frameRect
            let collapsedHeight = CGFloat(Chrome.contentInsets(kind).top + 1)
            rect.origin.y += rect.height - collapsedHeight
            rect.size.height = collapsedHeight
            super.setFrame(rect, display: flag)
        } else {
            super.setFrame(frameRect, display: flag)
        }
    }

    // MARK: Zoom

    public func toggleZoom() {
        if state.isCollapsed { toggleCollapse() }
        let target = standardFrame?() ?? defaultStandardFrame()
        if let userFrame, abs(frame.width - target.width) < 2 && abs(frame.height - target.height) < 2 {
            setFrame(userFrame, display: true, animate: true)
            self.userFrame = nil
        } else {
            userFrame = frame
            setFrame(target, display: true, animate: true)
        }
    }

    private func defaultStandardFrame() -> NSRect {
        let visible = (screen ?? NSScreen.main)?.visibleFrame ?? frame
        let rect = visible.insetBy(dx: 4, dy: 4)
        let width = min(rect.width, maxSize.width), height = min(rect.height, maxSize.height)
        return NSRect(x: rect.minX, y: rect.maxY - height, width: width, height: height)
    }

    // MARK: Modal dialogs

    /// Shows `dialog` as a movable modal (or modal) dialog in front of this
    /// window: centered over it in its upper third, blocking its content.
    public func presentModal(_ dialog: PlatinumWindow) {
        if let existing = modalChild {
            existing.presentModal(dialog)
            return
        }
        let size = dialog.frame.size
        let x = (frame.midX - size.width / 2).rounded()
        let room = max(0, frame.height - size.height)
        let y = (frame.maxY - room / 3 - size.height).rounded()
        dialog.setFrameOrigin(NSPoint(x: x, y: y))
        if let visible = (screen ?? NSScreen.main)?.visibleFrame {
            var origin = dialog.frame.origin
            origin.x = min(max(origin.x, visible.minX), visible.maxX - size.width)
            origin.y = min(max(origin.y, visible.minY), visible.maxY - size.height)
            dialog.setFrameOrigin(origin)
        }
        modalChild = dialog
        dialog.modalParent = self
        state.isBlockedByModal = true
        addChildWindow(dialog, ordered: .above)
        dialog.makeKeyAndOrderFront(nil)
    }

    /// Shows a dialog on its own, centered on the main screen in its upper third.
    public func presentAlone() {
        if let visible = (NSScreen.main ?? screen)?.visibleFrame {
            let size = frame.size
            let x = (visible.midX - size.width / 2).rounded()
            let y = (visible.maxY - (visible.height - size.height) / 3 - size.height).rounded()
            setFrameOrigin(NSPoint(x: x, y: y))
        }
        makeKeyAndOrderFront(nil)
    }

    public var hasModalDialog: Bool { modalChild != nil }

    /// A dialog window sized to fit its content's ideal size.
    public static func dialog<Content: View>(title: String = "", kind: Chrome.WindowKind = .movableModal,
                                             @ViewBuilder content: () -> Content) -> PlatinumWindow {
        let body = content()
        PlatinumFonts.registerIfNeeded()
        let probe = NSHostingView(rootView: body.fixedSize(horizontal: false, vertical: true))
        let fitting = probe.fittingSize
        let size = NSSize(width: fitting.width.rounded(.up), height: fitting.height.rounded(.up))
        return PlatinumWindow(title: title, kind: kind, contentSize: size) { body }
    }
}
#endif
