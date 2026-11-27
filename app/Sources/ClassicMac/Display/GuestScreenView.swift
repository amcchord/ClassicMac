import AppKit
import Combine
import SwiftUI

/// How the guest screen scales inside its host viewport.
enum GuestDisplayScale: String, CaseIterable, Hashable {
    case sharp
    case oneX
    case twoX
    case fit

    var label: String {
        switch self {
        case .sharp: return "Sharp Pixels"
        case .oneX: return "1×"
        case .twoX: return "2×"
        case .fit: return "Fit Window"
        }
    }
}

/// The guest's screen: the shared framebuffer drawn at whole-number or fitted
/// scales, plus keyboard and pointer input for the Mac.
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
    var legacyMouseTracking = true
    var mouseDPI: Int? = nil
    var pippinVMID: UUID? = nil
    var pippinControls: PippinControlConfig? = nil {
        didSet {
            guard pippinControls != oldValue, let vmID = pippinVMID, let pippinControls else { return }
            MainActor.assumeIsolated {
                PippinControllerManager.shared.updateControls(pippinControls, for: vmID)
            }
        }
    }
    var scaleMode: GuestDisplayScale = .sharp {
        didSet {
            if scaleMode != oldValue {
                needsLayout = true
                (enclosingScrollView as? GuestScreenContainerView)?.needsLayout = true
            }
        }
    }
    var useBlackBackground = false {
        didSet {
            layer?.backgroundColor = (useBlackBackground ? NSColor.black : Self.checkerboard).cgColor
        }
    }
    var isPaused = false {
        didSet {
            if isPaused != oldValue {
                if let vmID = pippinVMID {
                    MainActor.assumeIsolated {
                        PippinControllerManager.shared.setPaused(isPaused, for: vmID)
                    }
                }
                if isPaused {
                    releaseCapture()
                    releaseHeldInputs()
                }
            }
        }
    }
    var isSuspended = false {
        didSet {
            if isSuspended != oldValue {
                updateDisplayLinkState()
                if isSuspended {
                    releaseCapture()
                    releaseHeldInputs()
                    MainActor.assumeIsolated {
                        PippinControllerManager.shared.detach(session: session)
                    }
                } else {
                    attachPippinControllerIfKey()
                }
            }
        }
    }
    var onToggleFullScreen: (() -> Void)?
    var onCaptureChanged: ((Bool) -> Void)?

    private let imageLayer = CALayer()
    private nonisolated(unsafe) var displayLinkRef: CADisplayLink?
    private var lastPresentedGeneration: UInt64 = 0
    private var lastCursorScale: CGFloat = 0
    private var subscriptions: Set<AnyCancellable> = []
    private var imageRect = CGRect.zero
    private var scale: CGFloat = 1
    private var surfaceSize = CGSize.zero
    private var heldModifiers: Set<UInt16> = []
    private nonisolated(unsafe) var captured = false
    private var motion = RelativePointerMotion()
    private var scrollRemainder: CGFloat = 0
    private var button0Down = false
    private var button1Down = false
    private var button2Down = false
    private var rightButtonAsControl = false
    private var relative = false
    private var guestCursor: NSCursor?
    private var cursorVisible = true

    // Exposed internally for tests and container layout.
    var currentSurfaceSize: CGSize { surfaceSize }
    var currentScale: CGFloat { scale }
    var currentImageRect: CGRect { imageRect }
    var currentGuestCursor: NSCursor? { guestCursor }
    var hasActiveMouseButtons: Bool { button0Down || button1Down || button2Down || rightButtonAsControl }
    var isMouseCaptured: Bool { captured }

    static let checkerboard: NSColor = {
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
        imageLayer.isOpaque = true
        imageLayer.contentsGravity = .resize
        imageLayer.magnificationFilter = .nearest
        imageLayer.minificationFilter = .trilinear
        imageLayer.actions = ["contents": NSNull(), "bounds": NSNull(), "position": NSNull()]
        layer?.addSublayer(imageLayer)
        bindState()
    }

    required init?(coder: NSCoder) { fatalError() }

    deinit {
        displayLinkRef?.invalidate()
        NotificationCenter.default.removeObserver(self)
        if captured {
            CGAssociateMouseAndMouseCursorPosition(1)
            NSCursor.unhide()
        }
    }

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    // MARK: State

    @MainActor
    private func bindState() {
        let state = session.state
        state.$surfaceSize.sink { [weak self] size in
            guard let self else { return }
            self.surfaceSize = size
            self.needsLayout = true
            (self.enclosingScrollView as? GuestScreenContainerView)?.needsLayout = true
            self.window?.invalidateCursorRects(for: self)
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
            self?.lastPresentedGeneration = 0
            self?.imageLayer.contents = nil
        }.store(in: &subscriptions)
    }

    var isDisplayActive: Bool {
        guard let window, !isSuspended else { return false }
        return window.occlusionState.contains(.visible)
    }

    func updateDisplayLinkState() {
        displayLinkRef?.isPaused = !isDisplayActive
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        displayLinkRef?.invalidate()
        displayLinkRef = nil
        NotificationCenter.default.removeObserver(self)
        guard let window else {
            releaseCapture()
            releaseHeldInputs()
            MainActor.assumeIsolated {
                PippinControllerManager.shared.detach(session: session)
            }
            return
        }
        let link = displayLink(target: self, selector: #selector(tick))
        link.add(to: .main, forMode: .common)
        displayLinkRef = link
        updateDisplayLinkState()
        if isDisplayActive { drawLatestFrame() }
        attachPippinControllerIfKey()
        NotificationCenter.default.addObserver(self, selector: #selector(windowBecameKey),
                                               name: NSWindow.didBecomeKeyNotification, object: window)
        NotificationCenter.default.addObserver(self, selector: #selector(windowResigned),
                                               name: NSWindow.didResignKeyNotification, object: window)
        // Pause display updates when the window is occluded or suspended by
        // full screen, and draw the latest frame the moment it is visible.
        NotificationCenter.default.addObserver(self, selector: #selector(occlusionChanged),
                                               name: NSWindow.didChangeOcclusionStateNotification, object: window)
        NotificationCenter.default.addObserver(self, selector: #selector(drawLatestFrame),
                                               name: Self.refreshNotification, object: nil)
    }

    /// Posted to make every guest screen draw its latest frame now.
    static let refreshNotification = Notification.Name("com.classicmac.guest-screen.refresh")

    @objc private func occlusionChanged() {
        updateDisplayLinkState()
        if isDisplayActive { drawLatestFrame() }
    }

    @objc func drawLatestFrame() {
        guard let contents = session.presentFrame(lastGeneration: &lastPresentedGeneration, force: true) else { return }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        imageLayer.contents = contents
        CATransaction.commit()
    }

    @objc private func tick() {
        guard isDisplayActive else { return }
        if let contents = session.presentFrame(lastGeneration: &lastPresentedGeneration) {
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            imageLayer.contents = contents
            CATransaction.commit()
        }
    }

    private func attachPippinControllerIfKey() {
        guard !isSuspended, let window, window.isKeyWindow,
              let vmID = pippinVMID, let controls = pippinControls else { return }
        MainActor.assumeIsolated {
            PippinControllerManager.shared.attach(vmID: vmID, session: session, controls: controls, isPaused: isPaused)
        }
    }

    @objc private func windowBecameKey() {
        attachPippinControllerIfKey()
    }

    @objc private func windowResigned() {
        releaseCapture()
        releaseHeldInputs()
        MainActor.assumeIsolated {
            PippinControllerManager.shared.detach(session: session)
        }
    }

    // MARK: Layout

    static func scaleFactor(mode: GuestDisplayScale, surface: CGSize, viewport: CGSize) -> CGFloat {
        guard surface.width > 0, surface.height > 0,
              viewport.width > 0, viewport.height > 0 else { return 1 }
        let fit = min(viewport.width / surface.width, viewport.height / surface.height)
        switch mode {
        case .sharp: return fit >= 1 ? floor(fit) : 1
        case .oneX: return 1
        case .twoX: return 2
        case .fit: return fit
        }
    }

    func resolvedScale(forViewport viewport: CGSize) -> CGFloat {
        Self.scaleFactor(mode: scaleMode, surface: surfaceSize, viewport: viewport)
    }

    func scaledSurfaceSize(forScale scale: CGFloat) -> CGSize {
        guard surfaceSize.width > 0, surfaceSize.height > 0 else { return .zero }
        return CGSize(width: (surfaceSize.width * scale).rounded(),
                      height: (surfaceSize.height * scale).rounded())
    }

    override func layout() {
        super.layout()
        guard surfaceSize.width > 0, surfaceSize.height > 0 else {
            imageRect = .zero
            imageLayer.frame = bounds
            return
        }
        let viewport = enclosingScrollView?.contentView.bounds.size ?? bounds.size
        scale = resolvedScale(forViewport: viewport)
        imageLayer.magnificationFilter = (scaleMode == .fit && scale != floor(scale)) ? .linear : .nearest
        let size = scaledSurfaceSize(forScale: scale)
        imageRect = CGRect(x: ((bounds.width - size.width) / 2).rounded(.down),
                           y: ((bounds.height - size.height) / 2).rounded(.down),
                           width: size.width, height: size.height)
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        imageLayer.frame = imageRect
        CATransaction.commit()
        window?.invalidateCursorRects(for: self)
        if scale != lastCursorScale {
            updateGuestCursor(session.state.cursorImage, hotSpot: session.state.cursorHotSpot)
        }
    }

    /// Size the Mac's screen asks for, in points, at whole-number scale `n`.
    var preferredContentSize: CGSize { surfaceSize }

    // MARK: Cursor

    func updateGuestCursor(_ image: CGImage?, hotSpot: CGPoint) {
        let factor = scale > 0 ? scale : 1
        lastCursorScale = factor
        guard let image else {
            guestCursor = nil
            window?.invalidateCursorRects(for: self)
            return
        }
        // Scale the cursor with the screen, pixel for pixel (including < 1x fit).
        let size = NSSize(width: max(1, (CGFloat(image.width) * factor).rounded()),
                          height: max(1, (CGFloat(image.height) * factor).rounded()))
        let nsImage = NSImage(size: size, flipped: false) { rect in
            guard let ctx = NSGraphicsContext.current?.cgContext else { return false }
            ctx.interpolationQuality = factor < 1 ? .high : .none
            ctx.draw(image, in: rect)
            return true
        }
        let hx = min(max(0, hotSpot.x * factor), size.width)
        let hy = min(max(0, hotSpot.y * factor), size.height)
        guestCursor = NSCursor(image: nsImage, hotSpot: NSPoint(x: hx, y: hy))
        window?.invalidateCursorRects(for: self)
    }

    override func resetCursorRects() {
        guard !imageRect.isEmpty else { return }
        // ATI draws its own cursor into the screen: hide the Mac's arrow then.
        let cursor = cursorVisible ? (guestCursor ?? Self.invisibleCursor) : Self.invisibleCursor
        addCursorRect(imageRect, cursor: cursor)
    }

    // MARK: Pointer

    func guestPoint(forViewPoint p: CGPoint) -> (Int, Int)? {
        guard scale > 0, surfaceSize.width > 0, surfaceSize.height > 0,
              imageRect.contains(p) else { return nil }
        let x = Int(((p.x - imageRect.minX) / scale).rounded(.down))
        let y = Int(((p.y - imageRect.minY) / scale).rounded(.down))
        return (min(max(x, 0), Int(surfaceSize.width) - 1), min(max(y, 0), Int(surfaceSize.height) - 1))
    }

    private func guestPoint(_ event: NSEvent) -> (Int, Int)? {
        guestPoint(forViewPoint: convert(event.locationInWindow, from: nil))
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        for area in trackingAreas { removeTrackingArea(area) }
        addTrackingArea(NSTrackingArea(rect: bounds, options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect],
                                       owner: self, userInfo: nil))
    }

    private func move(_ event: NSEvent) {
        guard !isPaused else { return }
        if captured {
            let delta = motion.take(
                dx: event.deltaX,
                dy: event.deltaY,
                scale: scale,
                legacyNative: legacyMouseTracking,
                dpi: mouseDPI
            )
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
        guard !isPaused else { return }
        if relative && !captured {
            guard guestPoint(event) != nil else { return }
            capture()
            return
        }
        if !captured {
            guard let (x, y) = guestPoint(event) else { return }
            session.pointer(x: x, y: y)
        }
        button0Down = true
        session.button(0, down: true)
    }

    override func mouseUp(with event: NSEvent) {
        guard button0Down else { return }
        button0Down = false
        session.button(0, down: false)
    }

    override func rightMouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        guard !isPaused else { return }
        if !captured {
            guard let (x, y) = guestPoint(event) else { return }
            session.pointer(x: x, y: y)
        }
        if inputHelpers && !relative {
            rightButtonAsControl = true
            session.key(GuestKeymap.ctrl, down: true)
            session.button(0, down: true)
        } else {
            button2Down = true
            session.button(2, down: true)
        }
    }

    override func rightMouseUp(with event: NSEvent) {
        if rightButtonAsControl {
            rightButtonAsControl = false
            session.button(0, down: false)
            session.key(GuestKeymap.ctrl, down: false)
        } else if button2Down {
            button2Down = false
            session.button(2, down: false)
        }
    }

    override func otherMouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        guard !isPaused else { return }
        if !captured {
            guard guestPoint(event) != nil else { return }
        }
        button1Down = true
        session.button(1, down: true)
    }

    override func otherMouseUp(with event: NSEvent) {
        guard button1Down else { return }
        button1Down = false
        session.button(1, down: false)
    }

    override func scrollWheel(with event: NSEvent) {
        guard !isPaused else { return }
        if !captured, guestPoint(event) == nil { return }
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
        guard !captured, !isPaused else { return }
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
        if !event.isARepeat,
           event.modifierFlags.contains([.control, .command]),
           event.keyCode == 0x03 { // kVK_ANSI_F
            onToggleFullScreen?()
            return
        }
        guard !isPaused, !event.isARepeat else { return }
        if let vmID = pippinVMID,
           let controls = pippinControls,
           controls.keyboardMappingEnabled,
           let mappedButton = controls.button(forKeyCode: event.keyCode) {
            MainActor.assumeIsolated {
                PippinControllerManager.shared.attach(vmID: vmID, session: session, controls: controls, isPaused: isPaused)
                PippinControllerManager.shared.setKeyboardButton(mappedButton, down: true, for: session)
            }
            return
        }
        guard let code = GuestKeymap.qcodes[event.keyCode] else { return }
        session.key(code, down: true)
    }

    override func keyUp(with event: NSEvent) {
        guard !isPaused else { return }
        if let controls = pippinControls,
           controls.keyboardMappingEnabled,
           let mappedButton = controls.button(forKeyCode: event.keyCode) {
            MainActor.assumeIsolated {
                PippinControllerManager.shared.setKeyboardButton(mappedButton, down: false, for: session)
            }
            return
        }
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
        guard !isPaused else { return }
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

    private func releaseHeldInputs() {
        if button0Down {
            button0Down = false
            session.button(0, down: false)
        }
        if button1Down {
            button1Down = false
            session.button(1, down: false)
        }
        if button2Down {
            button2Down = false
            session.button(2, down: false)
        }
        if rightButtonAsControl {
            rightButtonAsControl = false
            session.button(0, down: false)
            session.key(GuestKeymap.ctrl, down: false)
        }
        heldModifiers.removeAll()
        MainActor.assumeIsolated {
            PippinControllerManager.shared.clearKeyboardButtons(for: session)
        }
        session.releaseAll()
    }

    override func becomeFirstResponder() -> Bool { true }

    override func resignFirstResponder() -> Bool {
        releaseHeldInputs()
        return true
    }

    // Test hooks
    func setTestGeometry(surfaceSize: CGSize, imageRect: CGRect, scale: CGFloat) {
        self.surfaceSize = surfaceSize
        self.imageRect = imageRect
        self.scale = scale
    }

    func setTestButtonState(button0: Bool = false, rightAsControl: Bool = false) {
        self.button0Down = button0
        self.rightButtonAsControl = rightAsControl
    }
}

/// Hosts `GuestScreenNSView` inside an `NSScrollView` so oversized sharp/1×/2×
/// surfaces scroll smoothly instead of clipping or shrinking.
final class GuestScreenContainerView: NSScrollView {
    let screenView: GuestScreenNSView

    init(session: GuestDisplaySession) {
        self.screenView = GuestScreenNSView(session: session)
        super.init(frame: .zero)
        drawsBackground = true
        backgroundColor = GuestScreenNSView.checkerboard
        borderType = .noBorder
        hasVerticalScroller = false
        hasHorizontalScroller = false
        autohidesScrollers = true
        documentView = screenView
    }

    required init?(coder: NSCoder) { fatalError() }

    override func layout() {
        updateScrollbarsAndDocumentFrame()
        super.layout()
    }

    func updateScrollbarsAndDocumentFrame() {
        let surface = screenView.currentSurfaceSize
        let available = bounds.size
        guard surface.width > 0, surface.height > 0, available.width > 0, available.height > 0 else {
            hasHorizontalScroller = false
            hasVerticalScroller = false
            screenView.frame = NSRect(origin: .zero, size: available)
            return
        }
        let scale = screenView.resolvedScale(forViewport: available)
        let scaled = screenView.scaledSurfaceSize(forScale: scale)
        let needH = screenView.scaleMode != .fit && scaled.width > available.width + 0.5
        let needV = screenView.scaleMode != .fit && scaled.height > available.height + 0.5
        if hasHorizontalScroller != needH { hasHorizontalScroller = needH }
        if hasVerticalScroller != needV { hasVerticalScroller = needV }
        super.layout()
        let clipSize = contentView.bounds.size
        if screenView.scaleMode != .fit {
            let scale2 = screenView.resolvedScale(forViewport: clipSize)
            let scaled2 = screenView.scaledSurfaceSize(forScale: scale2)
            let needH2 = scaled2.width > clipSize.width + 0.5
            let needV2 = scaled2.height > clipSize.height + 0.5
            if hasHorizontalScroller != needH2 || hasVerticalScroller != needV2 {
                hasHorizontalScroller = needH2
                hasVerticalScroller = needV2
                super.layout()
            }
        }
        let finalClip = contentView.bounds.size
        let finalScale = screenView.resolvedScale(forViewport: finalClip)
        let finalScaled = screenView.scaledSurfaceSize(forScale: finalScale)
        let docSize = NSSize(width: max(finalClip.width, finalScaled.width),
                             height: max(finalClip.height, finalScaled.height))
        if screenView.frame.size != docSize {
            screenView.frame = NSRect(origin: .zero, size: docSize)
        }
        screenView.needsLayout = true
    }
}

/// SwiftUI wrapper for the guest screen.
struct GuestScreen: NSViewRepresentable {
    let session: GuestDisplaySession
    let inputHelpers: Bool
    var legacyMouseTracking: Bool = true
    var mouseDPI: Int? = nil
    var pippinVMID: UUID? = nil
    var pippinControls: PippinControlConfig? = nil
    let scaleMode: GuestDisplayScale
    let isPaused: Bool
    var isSuspended: Bool = false
    let onToggleFullScreen: () -> Void
    let onCaptureChanged: (Bool) -> Void

    func makeNSView(context: Context) -> GuestScreenContainerView {
        let container = GuestScreenContainerView(session: session)
        let view = container.screenView
        view.inputHelpers = inputHelpers
        view.legacyMouseTracking = legacyMouseTracking
        view.mouseDPI = mouseDPI
        view.pippinVMID = pippinVMID
        view.pippinControls = pippinControls
        view.scaleMode = scaleMode
        view.isPaused = isPaused
        view.isSuspended = isSuspended
        view.onToggleFullScreen = onToggleFullScreen
        view.onCaptureChanged = onCaptureChanged
        DispatchQueue.main.async { container.window?.makeFirstResponder(view) }
        return container
    }

    func updateNSView(_ container: GuestScreenContainerView, context: Context) {
        let view = container.screenView
        view.inputHelpers = inputHelpers
        view.legacyMouseTracking = legacyMouseTracking
        view.mouseDPI = mouseDPI
        view.pippinVMID = pippinVMID
        view.pippinControls = pippinControls
        view.scaleMode = scaleMode
        view.isPaused = isPaused
        view.isSuspended = isSuspended
        view.onToggleFullScreen = onToggleFullScreen
        view.onCaptureChanged = onCaptureChanged
    }
}
