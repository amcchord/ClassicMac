import AppKit
import Combine
import SwiftUI

/// The guest's screen: the shared framebuffer drawn at whole-number scales
/// with sharp pixels, plus keyboard and pointer input for the Mac.
///
/// - Keyboard: every key, including Command combinations, goes to the Mac
///   while its screen has focus (Control-Command-F toggles full screen).
/// - Seamless mouse: the pointer moves freely and the guest's own cursor is
///   shown as the Mac's cursor. When a game switches the Mac to relative
///   motion, a click captures the mouse; Control-Option releases it.
/// - Classic input helpers: a secondary click is a Control-click and the
///   scroll wheel presses the arrow keys, as ClassicMac always did.
final class GuestScreenNSView: NSView {
    let session: GuestDisplaySession
    var inputHelpers = true
    var onToggleFullScreen: (() -> Void)?
    var onCaptureChanged: ((Bool) -> Void)?

    private let imageLayer = CALayer()
    private var displayLinkRef: CADisplayLink?
    private var subscriptions: Set<AnyCancellable> = []
    private var imageRect = CGRect.zero
    private var scale: CGFloat = 1
    private var surfaceSize = CGSize.zero
    private var heldModifiers: Set<UInt16> = []
    private var captured = false
    private var motion = RelativePointerMotion()
    private var scrollRemainder: CGFloat = 0
    private var rightButtonAsControl = false
    private var relative = false
    private var guestCursor: NSCursor?
    private var cursorVisible = true
    private static let checkerboard: NSColor = {
        let image = NSImage(size: NSSize(width: 2, height: 2), flipped: false) { _ in
            NSColor.white.setFill()
            NSRect(x: 0, y: 0, width: 2, height: 2).fill()
            NSColor.black.setFill()
            NSRect(x: 0, y: 0, width: 1, height: 1).fill()
            NSRect(x: 1, y: 1, width: 1, height: 1).fill()
            return true
        }
        return NSColor(patternImage: image)
    }()

    private static let invisibleCursor = NSCursor(image: NSImage(size: NSSize(width: 1, height: 1)), hotSpot: .zero)

    init(session: GuestDisplaySession) {
        self.session = session
        super.init(frame: .zero)
        wantsLayer = true
        // Around a screen smaller than the window: the classic gray dither,
        // never black.
        layer?.backgroundColor = Self.checkerboard.cgColor
        imageLayer.contentsGravity = .resize
        imageLayer.magnificationFilter = .nearest
        imageLayer.minificationFilter = .trilinear
        imageLayer.actions = ["contents": NSNull(), "bounds": NSNull(), "position": NSNull()]
        layer?.addSublayer(imageLayer)
        bindState()
    }

    required init?(coder: NSCoder) { fatalError() }

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    // MARK: State

    @MainActor
    private func bindState() {
        let state = session.state
        state.$surfaceSize.sink { [weak self] size in
            self?.surfaceSize = size
            self?.needsLayout = true
            self?.window?.invalidateCursorRects(for: self!)
        }.store(in: &subscriptions)
        state.$pointerRelative.sink { [weak self] relative in
            self?.relative = relative
            if !relative { self?.releaseCapture() }
        }.store(in: &subscriptions)
        Publishers.CombineLatest3(state.$cursorImage, state.$cursorHotSpot, state.$cursorVisible)
            .sink { [weak self] image, hot, visible in
                self?.cursorVisible = visible
                self?.updateGuestCursor(image, hotSpot: hot)
            }.store(in: &subscriptions)
        state.$bootCount.sink { [weak self] _ in
            self?.imageLayer.contents = nil
        }.store(in: &subscriptions)
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        displayLinkRef?.invalidate()
        displayLinkRef = nil
        guard window != nil else { return }
        let link = displayLink(target: self, selector: #selector(tick))
        link.add(to: .main, forMode: .common)
        displayLinkRef = link
        if let image = session.takeFrame(force: true) { imageLayer.contents = image }
        NotificationCenter.default.addObserver(self, selector: #selector(windowResigned),
                                               name: NSWindow.didResignKeyNotification, object: window)
        // The display link pauses while the window is hidden or the display
        // sleeps; draw the latest frame the moment it can be seen again.
        NotificationCenter.default.addObserver(self, selector: #selector(drawLatestFrame),
                                               name: NSWindow.didChangeOcclusionStateNotification, object: window)
        NotificationCenter.default.addObserver(self, selector: #selector(drawLatestFrame),
                                               name: Self.refreshNotification, object: nil)
    }

    /// Posted to make every guest screen draw its latest frame now.
    static let refreshNotification = Notification.Name("com.classicmac.guest-screen.refresh")

    @objc func drawLatestFrame() {
        guard let image = session.takeFrame(force: true) else { return }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        imageLayer.contents = image
        CATransaction.commit()
    }

    @objc private func tick() {
        if let image = session.takeFrame() {
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            imageLayer.contents = image
            CATransaction.commit()
        }
    }

    @objc private func windowResigned() {
        releaseCapture()
        releaseHeldKeys()
    }

    // MARK: Layout

    override func layout() {
        super.layout()
        guard surfaceSize.width > 0, surfaceSize.height > 0 else {
            imageLayer.frame = bounds
            return
        }
        let fit = min(bounds.width / surfaceSize.width, bounds.height / surfaceSize.height)
        // Whole-number scales keep every Mac pixel square and sharp; only a
        // screen too large for the window is reduced smoothly.
        scale = fit >= 1 ? floor(fit) : fit
        imageLayer.magnificationFilter = .nearest
        let size = CGSize(width: (surfaceSize.width * scale).rounded(), height: (surfaceSize.height * scale).rounded())
        imageRect = CGRect(x: ((bounds.width - size.width) / 2).rounded(.down),
                           y: ((bounds.height - size.height) / 2).rounded(.down),
                           width: size.width, height: size.height)
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        imageLayer.frame = imageRect
        CATransaction.commit()
        window?.invalidateCursorRects(for: self)
        updateGuestCursor(session.state.cursorImage, hotSpot: session.state.cursorHotSpot)
    }

    /// Size the Mac's screen asks for, in points, at whole-number scale `n`.
    var preferredContentSize: CGSize { surfaceSize }

    // MARK: Cursor

    private func updateGuestCursor(_ image: CGImage?, hotSpot: CGPoint) {
        guard let image else {
            guestCursor = nil
            window?.invalidateCursorRects(for: self)
            return
        }
        // Scale the cursor with the screen, pixel for pixel.
        let factor = max(1, scale)
        let size = NSSize(width: CGFloat(image.width) * factor, height: CGFloat(image.height) * factor)
        let nsImage = NSImage(size: size, flipped: false) { rect in
            guard let ctx = NSGraphicsContext.current?.cgContext else { return false }
            ctx.interpolationQuality = .none
            ctx.draw(image, in: rect)
            return true
        }
        guestCursor = NSCursor(image: nsImage, hotSpot: NSPoint(x: hotSpot.x * factor, y: hotSpot.y * factor))
        window?.invalidateCursorRects(for: self)
    }

    override func resetCursorRects() {
        guard !imageRect.isEmpty else { return }
        // ATI draws its own cursor into the screen: hide the Mac's arrow then.
        let cursor = cursorVisible ? (guestCursor ?? Self.invisibleCursor) : Self.invisibleCursor
        addCursorRect(imageRect, cursor: cursor)
    }

    // MARK: Pointer

    private func guestPoint(_ event: NSEvent) -> (Int, Int)? {
        let p = convert(event.locationInWindow, from: nil)
        guard scale > 0, surfaceSize.width > 0 else { return nil }
        let x = Int(((p.x - imageRect.minX) / scale).rounded(.down))
        let y = Int(((p.y - imageRect.minY) / scale).rounded(.down))
        return (min(max(x, 0), Int(surfaceSize.width) - 1), min(max(y, 0), Int(surfaceSize.height) - 1))
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        for area in trackingAreas { removeTrackingArea(area) }
        addTrackingArea(NSTrackingArea(rect: bounds, options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect],
                                       owner: self, userInfo: nil))
    }

    private func move(_ event: NSEvent) {
        if captured {
            let delta = motion.take(dx: event.deltaX, dy: event.deltaY, scale: scale)
            session.pointerDelta(dx: delta.x, dy: delta.y)
        } else if !relative, let (x, y) = guestPoint(event) {
            session.pointer(x: x, y: y)
        }
    }

    override func mouseMoved(with event: NSEvent) { move(event) }
    override func mouseDragged(with event: NSEvent) { move(event) }
    override func rightMouseDragged(with event: NSEvent) { move(event) }
    override func otherMouseDragged(with event: NSEvent) { move(event) }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        if relative && !captured {
            capture()
            return
        }
        move(event)
        session.button(0, down: true)
    }

    override func mouseUp(with event: NSEvent) { session.button(0, down: false) }

    override func rightMouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        move(event)
        if inputHelpers && !relative {
            rightButtonAsControl = true
            session.key(GuestKeymap.ctrl, down: true)
            session.button(0, down: true)
        } else {
            session.button(2, down: true)
        }
    }

    override func rightMouseUp(with event: NSEvent) {
        if rightButtonAsControl {
            session.button(0, down: false)
            session.key(GuestKeymap.ctrl, down: false)
            rightButtonAsControl = false
        } else {
            session.button(2, down: false)
        }
    }

    override func otherMouseDown(with event: NSEvent) { session.button(1, down: true) }
    override func otherMouseUp(with event: NSEvent) { session.button(1, down: false) }

    override func scrollWheel(with event: NSEvent) {
        let lineHeight: CGFloat = event.hasPreciseScrollingDeltas ? 12 : 1
        scrollRemainder += event.scrollingDeltaY / lineHeight
        while abs(scrollRemainder) >= 1 {
            let up = scrollRemainder > 0
            scrollRemainder += up ? -1 : 1
            if inputHelpers && !relative {
                let key = up ? GuestKeymap.up : GuestKeymap.down
                session.key(key, down: true)
                session.key(key, down: false)
            } else {
                session.button(up ? 3 : 4, down: true)
                session.button(up ? 3 : 4, down: false)
            }
        }
    }

    // MARK: Capture (games)

    private func capture() {
        guard !captured else { return }
        captured = true
        motion = RelativePointerMotion()
        NSCursor.hide()
        CGAssociateMouseAndMouseCursorPosition(0)
        onCaptureChanged?(true)
    }

    func releaseCapture() {
        guard captured else { return }
        captured = false
        motion = RelativePointerMotion()
        CGAssociateMouseAndMouseCursorPosition(1)
        NSCursor.unhide()
        onCaptureChanged?(false)
    }

    // MARK: Keyboard

    private static let modifierKeys: [UInt16: NSEvent.ModifierFlags] = [
        55: .command, 54: .command, 56: .shift, 60: .shift, 58: .option, 61: .option,
        59: .control, 62: .control, 63: .function,
    ]

    override func keyDown(with event: NSEvent) {
        if event.modifierFlags.contains([.control, .command]) && event.charactersIgnoringModifiers == "f" {
            onToggleFullScreen?()
            return
        }
        guard !event.isARepeat, let code = GuestKeymap.qcodes[event.keyCode] else { return }
        session.key(code, down: true)
    }

    override func keyUp(with event: NSEvent) {
        guard let code = GuestKeymap.qcodes[event.keyCode] else { return }
        session.key(code, down: false)
    }

    // Command combinations belong to the Mac, not ClassicMac's menus.
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        guard window?.firstResponder === self, event.type == .keyDown else { return false }
        keyDown(with: event)
        return true
    }

    override func flagsChanged(with event: NSEvent) {
        let code = event.keyCode
        if code == 57 {
            // Caps Lock is a locking key on Apple keyboards: down while engaged.
            if let caps = GuestKeymap.qcodes[57] {
                session.key(caps, down: event.modifierFlags.contains(.capsLock))
            }
            return
        }
        guard let flag = Self.modifierKeys[code], let qcode = GuestKeymap.qcodes[code] else { return }
        let down = event.modifierFlags.contains(flag) && !heldModifiers.contains(code)
        if down {
            heldModifiers.insert(code)
        } else {
            heldModifiers.remove(code)
        }
        session.key(qcode, down: down)
        // Control-Option releases a captured mouse.
        if captured && event.modifierFlags.contains([.control, .option]) {
            releaseCapture()
        }
    }

    private func releaseHeldKeys() {
        heldModifiers.removeAll()
        session.releaseAll()
    }

    override func becomeFirstResponder() -> Bool { true }

    override func resignFirstResponder() -> Bool {
        releaseHeldKeys()
        return true
    }
}

/// SwiftUI wrapper for the guest screen.
struct GuestScreen: NSViewRepresentable {
    let session: GuestDisplaySession
    let inputHelpers: Bool
    let onToggleFullScreen: () -> Void
    let onCaptureChanged: (Bool) -> Void

    func makeNSView(context: Context) -> GuestScreenNSView {
        let view = GuestScreenNSView(session: session)
        view.inputHelpers = inputHelpers
        view.onToggleFullScreen = onToggleFullScreen
        view.onCaptureChanged = onCaptureChanged
        DispatchQueue.main.async { view.window?.makeFirstResponder(view) }
        return view
    }

    func updateNSView(_ view: GuestScreenNSView, context: Context) {
        view.inputHelpers = inputHelpers
        view.onToggleFullScreen = onToggleFullScreen
        view.onCaptureChanged = onCaptureChanged
    }
}
