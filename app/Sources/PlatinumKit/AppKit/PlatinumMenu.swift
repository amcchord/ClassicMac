#if os(macOS)
import AppKit

/// A Platinum menu drawn in its own borderless panel and tracked the way the
/// Mac OS Menu Manager did: press-drag-release, or click to open a "sticky"
/// menu and click an item; the chosen item blinks once before the menu closes.
@MainActor
public enum PlatinumMenu {
    public struct Item {
        public var title: String
        public var enabled: Bool
        public var separator: Bool
        public var checked: Bool

        public init(title: String, enabled: Bool = true, separator: Bool = false, checked: Bool = false) {
            self.title = title
            self.enabled = enabled
            self.separator = separator
            self.checked = checked
        }
    }

    static let itemHeight = 16
    static let separatorHeight = 6
    static let topMargin = 3
    static let textX = 20
    static let scrollStripHeight = 14

    /// Clamps a menu's panel size and screen origin to `visible`.
    static func clampedFrame(naturalWidth: Int, naturalHeight: Int, idealOrigin: NSPoint,
                             visible: NSRect?) -> NSRect {
        let minHeight = CGFloat(itemHeight + topMargin * 2 + 1)
        let maxHeight = visible.map { max(minHeight, floor($0.height)) }
        let height = maxHeight.map { min(CGFloat(naturalHeight + 1), $0) } ?? CGFloat(naturalHeight + 1)
        let width = CGFloat(naturalWidth + 1)
        var origin = idealOrigin
        if let visible {
            origin.x = min(max(origin.x, visible.minX), max(visible.minX, visible.maxX - width))
            origin.y = min(max(origin.y, visible.minY), max(visible.minY, visible.maxY - height))
        }
        return NSRect(origin: origin, size: NSSize(width: width, height: height))
    }

    /// Shows a pop-up menu over `anchor` (screen coordinates) with the selected
    /// item lined up over the button's text, and tracks it until the user
    /// chooses an item or dismisses the menu. Returns the chosen index.
    public static func popUp(items: [Item], selected: Int?, anchor: NSRect, textInset: CGFloat,
                             event: NSEvent?, in parent: NSWindow?) -> Int? {
        guard !items.isEmpty else { return nil }
        let view = MenuView(items: items)
        let contentWidth = max(Int(anchor.width) + 11 - 2, view.naturalWidth)
        let visible = (parent?.screen ?? NSScreen.main)?.visibleFrame
        let rowTop = CGFloat(view.rowTop(selected ?? 0))
        let provisionalHeight = CGFloat(view.naturalHeight + 1)
        let idealOrigin = NSPoint(x: anchor.minX + textInset - CGFloat(textX),
                                  y: anchor.maxY - 2 + rowTop - provisionalHeight)
        var frame = clampedFrame(naturalWidth: contentWidth, naturalHeight: view.naturalHeight,
                                 idealOrigin: idealOrigin, visible: visible)
        // Re-align using the clamped panel height when not capped by screen height.
        if frame.height < provisionalHeight {
            let alignedOrigin = NSPoint(x: anchor.minX + textInset - CGFloat(textX),
                                        y: anchor.maxY - 2 + rowTop - frame.height)
            frame = clampedFrame(naturalWidth: contentWidth, naturalHeight: view.naturalHeight,
                                 idealOrigin: alignedOrigin, visible: visible)
        }
        view.frame = NSRect(origin: .zero, size: frame.size)
        if view.maxScrollY > 0 {
            let desiredScroll = Int((anchor.maxY - 2 + rowTop - (frame.origin.y + frame.height)).rounded())
            view.scrollY = min(max(0, desiredScroll), view.maxScrollY)
            view.ensureVisible(selected)
        }
        return track(view: view, frame: frame, anchor: anchor,
                     event: event, parent: parent, initial: selected)
    }

    /// Shows a menu below `anchor` (a menu title or a button), as a pull-down.
    public static func pullDown(items: [Item], anchor: NSRect, event: NSEvent?, in parent: NSWindow?) -> Int? {
        guard !items.isEmpty else { return nil }
        let view = MenuView(items: items)
        let contentWidth = max(Int(anchor.width), view.naturalWidth)
        let visible = (parent?.screen ?? NSScreen.main)?.visibleFrame
        let provisional = clampedFrame(naturalWidth: contentWidth, naturalHeight: view.naturalHeight,
                                       idealOrigin: .zero, visible: visible)
        let idealOrigin = NSPoint(x: anchor.minX, y: anchor.minY - provisional.height + 1)
        let frame = clampedFrame(naturalWidth: contentWidth, naturalHeight: view.naturalHeight,
                                 idealOrigin: idealOrigin, visible: visible)
        view.frame = NSRect(origin: .zero, size: frame.size)
        return track(view: view, frame: frame, anchor: anchor,
                     event: event, parent: parent, initial: nil)
    }

    private static func track(view: MenuView, frame: NSRect, anchor: NSRect, event: NSEvent?,
                              parent: NSWindow?, initial: Int?) -> Int? {
        let panel = NSPanel(contentRect: frame, styleMask: [.borderless, .nonactivatingPanel],
                            backing: .buffered, defer: false)
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false
        panel.level = .popUpMenu
        panel.contentView = view
        panel.acceptsMouseMovedEvents = true
        let parentMoved = parent?.acceptsMouseMovedEvents ?? false
        parent?.acceptsMouseMovedEvents = true
        defer { parent?.acceptsMouseMovedEvents = parentMoved }
        panel.orderFront(nil)

        func index(at screenPoint: NSPoint) -> Int? {
            let local = view.convert(panel.convertPoint(fromScreen: screenPoint), from: nil)
            return view.item(at: local)
        }

        func localPoint(_ screenPoint: NSPoint) -> NSPoint {
            view.convert(panel.convertPoint(fromScreen: screenPoint), from: nil)
        }

        view.highlighted = index(at: NSEvent.mouseLocation) ?? initial
        view.display()

        let opened = event?.timestamp ?? ProcessInfo.processInfo.systemUptime
        var sticky = event == nil || event?.type != .leftMouseDown
        var result: Int?
        tracking: while true {
            let autoDir = view.scrollArrowDirection(at: localPoint(NSEvent.mouseLocation))
            let deadline = autoDir != 0 ? Date(timeIntervalSinceNow: 0.06) : .distantFuture
            guard let e = NSApp.nextEvent(matching: [.leftMouseUp, .leftMouseDragged, .leftMouseDown,
                                                     .mouseMoved, .keyDown, .rightMouseDown, .scrollWheel],
                                          until: deadline, inMode: .eventTracking, dequeue: true) else {
                if autoDir != 0 {
                    view.scroll(by: autoDir * PlatinumMenu.itemHeight)
                    view.highlighted = index(at: NSEvent.mouseLocation)
                    view.display()
                }
                continue
            }
            switch e.type {
            case .leftMouseDragged, .mouseMoved:
                let hit = index(at: NSEvent.mouseLocation)
                if hit != view.highlighted {
                    view.highlighted = hit
                    view.display()
                }
            case .scrollWheel:
                if view.maxScrollY > 0 {
                    view.scroll(by: Int(-e.scrollingDeltaY.rounded()))
                    view.highlighted = index(at: NSEvent.mouseLocation)
                    view.display()
                }
            case .leftMouseUp:
                let hit = index(at: NSEvent.mouseLocation)
                if let hit, view.items[hit].enabled, !view.items[hit].separator {
                    result = hit
                    break tracking
                }
                // A quick click on the button opens the menu and leaves it up.
                if !sticky && e.timestamp - opened < 0.45 && anchor.contains(NSEvent.mouseLocation) {
                    sticky = true
                    continue
                }
                break tracking
            case .leftMouseDown, .rightMouseDown:
                if !panel.frame.contains(NSEvent.mouseLocation) { break tracking }
            case .keyDown:
                switch e.keyCode {
                case 53:                      // Escape
                    break tracking
                case 126:                     // Up
                    view.highlighted = view.step(from: view.highlighted ?? initial, by: -1)
                    view.ensureVisible(view.highlighted)
                    view.display()
                case 125:                     // Down
                    view.highlighted = view.step(from: view.highlighted ?? initial, by: 1)
                    view.ensureVisible(view.highlighted)
                    view.display()
                case 36, 76, 49:              // Return, Enter, Space
                    if let h = view.highlighted, view.items[h].enabled, !view.items[h].separator {
                        result = h
                    }
                    break tracking
                default:
                    if e.charactersIgnoringModifiers == "." && e.modifierFlags.contains(.command) {
                        break tracking
                    }
                }
            default:
                break
            }
        }

        if let result {
            // MenuFlash: the chosen item blinks once.
            for highlight in [nil, result] {
                view.highlighted = highlight
                view.display()
                Thread.sleep(forTimeInterval: 0.045)
            }
        }
        panel.orderOut(nil)
        return result
    }

    final class MenuView: NSView {
        let items: [Item]
        var highlighted: Int?
        var scrollY: Int = 0

        init(items: [Item]) {
            self.items = items
            super.init(frame: .zero)
        }

        required init?(coder: NSCoder) { fatalError() }

        override var isFlipped: Bool { true }

        var naturalWidth: Int {
            let widest = items.map { Int(PlatinumFonts.width($0.title, font: .system)) }.max() ?? 40
            return PlatinumMenu.textX + widest + 18
        }

        var naturalHeight: Int {
            items.reduce(PlatinumMenu.topMargin * 2) {
                $0 + ($1.separator ? PlatinumMenu.separatorHeight : PlatinumMenu.itemHeight)
            }
        }

        var viewportHeight: Int { max(0, Int(bounds.height) - 1) }
        var maxScrollY: Int { max(0, naturalHeight - viewportHeight) }
        var canScrollUp: Bool { scrollY > 0 }
        var canScrollDown: Bool { scrollY < maxScrollY }
        var topClip: Int { canScrollUp ? PlatinumMenu.scrollStripHeight : PlatinumMenu.topMargin }
        var bottomClip: Int {
            canScrollDown ? max(topClip, viewportHeight - PlatinumMenu.scrollStripHeight)
                          : max(topClip, viewportHeight - PlatinumMenu.topMargin)
        }

        func scroll(by delta: Int) {
            scrollY = min(max(0, scrollY + delta), maxScrollY)
        }

        func ensureVisible(_ index: Int?) {
            guard let index, items.indices.contains(index), maxScrollY > 0 else { return }
            let top = rowTop(index)
            let h = items[index].separator ? PlatinumMenu.separatorHeight : PlatinumMenu.itemHeight
            let bottom = top + h
            let minVisibleY = top <= PlatinumMenu.topMargin ? PlatinumMenu.topMargin : PlatinumMenu.scrollStripHeight
            let maxVisibleBottom = viewportHeight - (bottom >= naturalHeight - PlatinumMenu.topMargin ? PlatinumMenu.topMargin : PlatinumMenu.scrollStripHeight)
            if top - scrollY < minVisibleY {
                scrollY = max(0, top - minVisibleY)
            } else if bottom - scrollY > maxVisibleBottom {
                scrollY = min(maxScrollY, bottom - maxVisibleBottom)
            }
        }

        func scrollArrowDirection(at point: NSPoint) -> Int {
            let menuWidth = bounds.width - 1
            guard point.x >= 1, point.x < menuWidth - 1 else { return 0 }
            if canScrollUp && point.y >= 1 && point.y < CGFloat(PlatinumMenu.scrollStripHeight) { return -1 }
            if canScrollDown && point.y >= CGFloat(bottomClip) && point.y < CGFloat(viewportHeight - 1) { return 1 }
            return 0
        }

        func rowTop(_ index: Int) -> Int {
            var y = PlatinumMenu.topMargin
            for i in 0..<min(index, items.count) {
                y += items[i].separator ? PlatinumMenu.separatorHeight : PlatinumMenu.itemHeight
            }
            return y
        }

        func item(at point: NSPoint) -> Int? {
            let menuWidth = bounds.width - 1
            guard point.x >= 1, point.x < menuWidth - 1 else { return nil }
            guard point.y >= CGFloat(topClip), point.y < CGFloat(bottomClip) else { return nil }
            let contentY = point.y + CGFloat(scrollY)
            var y = PlatinumMenu.topMargin
            for (i, item) in items.enumerated() {
                let h = item.separator ? PlatinumMenu.separatorHeight : PlatinumMenu.itemHeight
                if contentY >= CGFloat(y) && contentY < CGFloat(y + h) {
                    return item.separator || !item.enabled ? nil : i
                }
                y += h
            }
            return nil
        }

        func step(from index: Int?, by delta: Int) -> Int? {
            var i = index ?? (delta > 0 ? -1 : items.count)
            for _ in 0..<items.count {
                i += delta
                guard items.indices.contains(i) else { return index }
                if items[i].enabled && !items[i].separator { return i }
            }
            return index
        }

        override func draw(_ dirtyRect: NSRect) {
            guard let cg = NSGraphicsContext.current?.cgContext else { return }
            cg.clear(bounds)
            let p = PixelPainter(cg)
            let w = Int(bounds.width) - 1, h = Int(bounds.height) - 1
            Chrome.menu(p, PixelRect(0, 0, w, h))

            cg.saveGState()
            cg.clip(to: CGRect(x: 1, y: topClip, width: max(0, w - 2), height: max(0, bottomClip - topClip)))
            var y = PlatinumMenu.topMargin - scrollY
            for (i, item) in items.enumerated() {
                if item.separator {
                    p.hline(1, y + 2, w - 2, Platinum.gray94)
                    p.hline(1, y + 3, w - 2, Platinum.white)
                    y += PlatinumMenu.separatorHeight
                    continue
                }
                let hot = i == highlighted
                if hot {
                    p.fill(1, y, w - 2, PlatinumMenu.itemHeight, Platinum.menuHighlight)
                    p.vline(1, y, PlatinumMenu.itemHeight, RGB(0x6666CC))
                    p.vline(w - 2, y, PlatinumMenu.itemHeight, RGB(0x000088))
                }
                let ink: RGB = hot ? Platinum.white : item.enabled ? Platinum.black : Platinum.disabledText
                if item.checked { Chrome.checkmark(p, x: 6, y: y + 4, color: ink) }
                p.text(item.title, font: .system, x: PlatinumMenu.textX, baseline: y + 12, color: ink)
                y += PlatinumMenu.itemHeight
            }
            cg.restoreGState()

            if canScrollUp {
                p.fill(1, 2, w - 2, PlatinumMenu.scrollStripHeight - 2, Platinum.face)
                Chrome.arrow(p, cx: w / 2, y: 5, down: false, color: Platinum.black)
            }
            if canScrollDown {
                p.fill(1, bottomClip, w - 2, max(0, h - 1 - bottomClip), Platinum.face)
                Chrome.arrow(p, cx: w / 2, y: h - 8, down: true, color: Platinum.black)
            }
        }

        // MARK: NSAccessibility

        override func isAccessibilityElement() -> Bool { true }
        override func accessibilityRole() -> NSAccessibility.Role? { .menu }
        override func accessibilityTitle() -> String? { "Menu" }
        override func accessibilityPerformPress() -> Bool { highlighted != nil }
        override func accessibilityChildren() -> [Any]? {
            let w = Int(bounds.width) - 1
            var y = PlatinumMenu.topMargin - scrollY
            var children: [NSAccessibilityElement] = []
            for (i, item) in items.enumerated() {
                let h = item.separator ? PlatinumMenu.separatorHeight : PlatinumMenu.itemHeight
                let element = NSAccessibilityElement()
                element.setAccessibilityParent(self)
                element.setAccessibilityRole(item.separator ? .splitter : .menuItem)
                element.setAccessibilityTitle(item.title)
                element.setAccessibilityEnabled(item.enabled && !item.separator)
                element.setAccessibilitySelected(i == highlighted)
                let localRect = NSRect(x: 1, y: y, width: max(0, w - 2), height: h)
                let screenRect = window.map { $0.convertToScreen(convert(localRect, to: nil)) } ?? localRect
                element.setAccessibilityFrame(screenRect)
                children.append(element)
                y += h
            }
            return children
        }
    }
}
#endif
