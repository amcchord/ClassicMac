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

    /// Shows a pop-up menu over `anchor` (screen coordinates) with the selected
    /// item lined up over the button's text, and tracks it until the user
    /// chooses an item or dismisses the menu. Returns the chosen index.
    public static func popUp(items: [Item], selected: Int?, anchor: NSRect, textInset: CGFloat,
                             event: NSEvent?, in parent: NSWindow?) -> Int? {
        guard !items.isEmpty else { return nil }
        let view = MenuView(items: items)
        let contentWidth = max(Int(anchor.width) + 11 - 2, view.naturalWidth)
        let size = NSSize(width: contentWidth + 1, height: view.naturalHeight + 1)
        view.frame = NSRect(origin: .zero, size: size)

        // Line the selected item's text up with the button's text.
        let rowTop = CGFloat(view.rowTop(selected ?? 0))
        var origin = NSPoint(x: anchor.minX + textInset - CGFloat(textX),
                             y: anchor.maxY - 2 + rowTop - size.height)
        origin.y += 0
        if let screen = parent?.screen ?? NSScreen.main {
            let visible = screen.visibleFrame
            origin.x = min(max(origin.x, visible.minX), visible.maxX - size.width)
            origin.y = min(max(origin.y, visible.minY), visible.maxY - size.height)
        }
        return track(view: view, frame: NSRect(origin: origin, size: size), anchor: anchor,
                     event: event, parent: parent, initial: selected)
    }

    /// Shows a menu below `anchor` (a menu title or a button), as a pull-down.
    public static func pullDown(items: [Item], anchor: NSRect, event: NSEvent?, in parent: NSWindow?) -> Int? {
        guard !items.isEmpty else { return nil }
        let view = MenuView(items: items)
        let size = NSSize(width: max(Int(anchor.width), view.naturalWidth) + 1, height: view.naturalHeight + 1)
        view.frame = NSRect(origin: .zero, size: size)
        var origin = NSPoint(x: anchor.minX, y: anchor.minY - size.height + 1)
        if let screen = parent?.screen ?? NSScreen.main {
            let visible = screen.visibleFrame
            origin.x = min(max(origin.x, visible.minX), visible.maxX - size.width)
            origin.y = max(origin.y, visible.minY)
        }
        return track(view: view, frame: NSRect(origin: origin, size: size), anchor: anchor,
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

        view.highlighted = index(at: NSEvent.mouseLocation)
        view.display()

        let opened = event?.timestamp ?? ProcessInfo.processInfo.systemUptime
        var sticky = event == nil
        var result: Int?
        tracking: while true {
            guard let e = NSApp.nextEvent(matching: [.leftMouseUp, .leftMouseDragged, .leftMouseDown,
                                                     .mouseMoved, .keyDown, .rightMouseDown],
                                          until: .distantFuture, inMode: .eventTracking, dequeue: true) else {
                continue
            }
            switch e.type {
            case .leftMouseDragged, .mouseMoved:
                let hit = index(at: NSEvent.mouseLocation)
                if hit != view.highlighted {
                    view.highlighted = hit
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
                    view.display()
                case 125:                     // Down
                    view.highlighted = view.step(from: view.highlighted ?? initial, by: 1)
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
            var y = PlatinumMenu.topMargin
            for (i, item) in items.enumerated() {
                let h = item.separator ? PlatinumMenu.separatorHeight : PlatinumMenu.itemHeight
                if point.y >= CGFloat(y) && point.y < CGFloat(y + h) {
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
            var y = PlatinumMenu.topMargin
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
        }
    }
}
#endif
