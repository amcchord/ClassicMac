import CoreGraphics

/// Interaction state of a control, as Platinum draws it.
public enum ControlState: Sendable {
    case normal, pressed, disabled
}

/// Platinum control drawing, with measured regression coverage documented in
/// platinum/reference/README.md. Some window/menu/animation details remain
/// approximations; the individual control gate is not a whole-app guarantee.
public enum Chrome {

    // MARK: - Push buttons

    /// A push button body filling r (normally 20 pixels tall).
    public static func button(_ p: PixelPainter, _ r: PixelRect, state: ControlState) {
        let rows = state == .pressed ? MeasuredChrome.BUTTON_PRESSED : state == .disabled ? MeasuredChrome.BUTTON_DISABLED : MeasuredChrome.BUTTON
        MeasuredChrome.stretch(p, r, rows, repeatAt: state == .disabled ? 2 : 4)
    }

    /// The default button's ring, drawn around a button rect r (3 pixels outside it).
    public static func defaultRing(_ p: PixelPainter, around r: PixelRect, disabled: Bool) {
        MeasuredChrome.stretch(p, r.outset(3), disabled ? MeasuredChrome.DEFAULT_DISABLED : MeasuredChrome.BUTTON_DEFAULT,
                               repeatAt: disabled ? 13 : 7)
    }

    /// Label color for a button in a state.
    public static func buttonText(_ state: ControlState) -> RGB {
        switch state {
        case .normal: return Platinum.black
        case .pressed: return Platinum.white
        case .disabled: return Platinum.disabledText
        }
    }

    // MARK: - Checkboxes and radio buttons

    /// A 12x12 checkbox at (x, y); a check mark overhangs 2 pixels to the right.
    public static func checkbox(_ p: PixelPainter, x: Int, y: Int, on: Bool, state: ControlState) {
        let key = (on ? "on" : "off") + (state == .pressed ? "-pressed" : state == .disabled ? "-disabled" : "")
        MeasuredChrome.sprite(p, MeasuredChrome.CHECKBOX[key]!, x: x, y: y)
    }

    /// A mixed-state checkbox (a dash), drawn on the off box.
    public static func checkboxMixed(_ p: PixelPainter, x: Int, y: Int, state: ControlState) {
        let rows = state == .disabled ? MeasuredChrome.CHECKBOX_MIXED_DISABLED : state == .pressed ? MeasuredChrome.CHECKBOX_MIXED_PRESSED : MeasuredChrome.CHECKBOX_MIXED
        MeasuredChrome.sprite(p, rows, x: x, y: y)
    }

    public static func radio(_ p: PixelPainter, x: Int, y: Int, on: Bool, state: ControlState) {
        if state == .disabled {
            MeasuredChrome.sprite(p, on ? MeasuredChrome.RADIO_ON_DISABLED : MeasuredChrome.RADIO_OFF_DISABLED, x: x, y: y)
        } else {
            let key = (on ? "on" : "off") + (state == .pressed ? "-pressed" : "")
            MeasuredChrome.sprite(p, MeasuredChrome.RADIO[key]!, x: x, y: y)
        }
    }

    // MARK: - Frames

    /// Etched group box. `labelWidth` (0 for none) reserves a gap for a title
    /// whose text starts 11 pixels in; the frame runs through the title's
    /// capitals (7 pixels below the top).
    public static func groupBox(_ p: PixelPainter, _ r: PixelRect, labelWidth: Int) {
        let top = labelWidth > 0 ? r.y + 7 : r.y
        MeasuredChrome.stretch(p, PixelRect(r.x, top, r.w, r.y + r.h - top), MeasuredChrome.GROUP, repeatAt: 2)
        if labelWidth > 0 { p.fill(r.x + 8, top, labelWidth + 6, 2, Platinum.face) }
    }

    /// An etched horizontal separator (2 rows).
    public static func separator(_ p: PixelPainter, x: Int, y: Int, w: Int) {
        p.hline(x, y, w - 1, Platinum.grayA5)
        p.hline(x + 1, y + 1, w - 1, Platinum.white)
    }

    /// An etched vertical separator (2 columns).
    public static func verticalSeparator(_ p: PixelPainter, x: Int, y: Int, h: Int) {
        p.vline(x, y, h - 1, Platinum.grayA5)
        p.vline(x + 1, y + 1, h - 1, Platinum.white)
    }

    /// A sunken well: gray above/left and white below/right, outside a black
    /// frame, filled inside. r is the black frame's rectangle.
    public static func sunken(_ p: PixelPainter, _ r: PixelRect, fill: RGB?) {
        p.hline(r.x - 1, r.y - 1, r.w + 1, Platinum.grayA5)
        p.vline(r.x - 1, r.y - 1, r.h + 1, Platinum.grayA5)
        p.hline(r.x, r.y + r.h, r.w + 1, Platinum.white)
        p.vline(r.x + r.w, r.y, r.h + 1, Platinum.white)
        p.frame(r, Platinum.black)
        if let fill {
            p.fill(r.inset(1), fill)
        }
    }

    /// An edit field whose black frame is r.
    public static func field(_ p: PixelPainter, _ r: PixelRect, disabled: Bool) {
        sunken(p, r, fill: disabled ? Platinum.grayF7 : Platinum.white)
        if disabled { p.frame(r, Platinum.grayA5) }
    }

    /// A list box whose black frame is r.
    public static func listBox(_ p: PixelPainter, _ r: PixelRect, disabled: Bool) {
        sunken(p, r, fill: disabled ? Platinum.grayF7 : Platinum.white)
        if disabled { p.frame(r, Platinum.grayA5) }
    }

    /// Two-pixel keyboard focus ring just outside r, with rounded outer corners.
    public static func focusRing(_ p: PixelPainter, around r: PixelRect) {
        let outer = r.outset(2)
        p.cg.saveGState()
        let path = CGMutablePath()
        path.addRect(CGRect(x: outer.x, y: outer.y, width: outer.w, height: outer.h))
        path.addRect(CGRect(x: r.x, y: r.y, width: r.w, height: r.h))
        p.cg.addPath(path); p.cg.clip(using: .evenOdd)
        MeasuredChrome.stretch(p, outer, MeasuredChrome.FIELD_FOCUS, repeatAt: 3)
        p.cg.restoreGState()
    }

    // MARK: - Pop-up button

    /// A small solid triangle, 7 pixels wide and 4 tall, centered on cx.
    public static func arrow(_ p: PixelPainter, cx: Int, y: Int, down: Bool, color: RGB) {
        for i in 0..<4 {
            let row = down ? i : 3 - i
            p.hline(cx - 3 + i, y + row, 7 - 2 * i, color)
        }
    }

    /// The pop-up button body; returns the text rect's x and right limit.
    @discardableResult
    public static func popup(_ p: PixelPainter, _ r: PixelRect, state: ControlState) -> (textX: Int, textMaxX: Int) {
        if state == .pressed {
            MeasuredChrome.stretch(p, r, MeasuredChrome.POPUP_PRESSED, repeatAt: 10)
            return (r.x + 9, r.x + r.w - 26)
        }
        let disabled = state == .disabled
        let left = disabled ? MeasuredChrome.POPUP_DIS_LEFT : MeasuredChrome.POPUP_LEFT
        let fill = disabled ? MeasuredChrome.POPUP_DIS_FILL : MeasuredChrome.POPUP_FILL
        let right = disabled ? MeasuredChrome.POPUP_DIS_RIGHT : MeasuredChrome.POPUP_RIGHT
        let rows = (0..<20).map { (left[$0], fill[$0], right[$0]) }
        MeasuredChrome.stretch(p, r, rows, repeatAt: 10)
        return (r.x + 9, r.x + r.w - 26)
    }

    // MARK: - Progress bar

    /// A progress bar whose black frame is r. `value` is 0...1, or nil for the
    /// indeterminate barber pole at animation `phase`.
    public static func progress(_ p: PixelPainter, _ r: PixelRect, value: Double?, phase: Int, disabled: Bool) {
        p.hline(r.x - 1, r.y - 1, r.w + 1, Platinum.grayA5)
        p.vline(r.x - 1, r.y - 1, r.h + 1, Platinum.grayA5)
        p.hline(r.x, r.y + r.h, r.w + 1, Platinum.white)
        p.vline(r.x + r.w, r.y, r.h + 1, Platinum.white)
        p.frame(r, Platinum.black)
        let inner = r.inset(1)
        guard !inner.isEmpty else { return }
        let n = inner.h - 1
        guard let value else {
            for y in 0..<inner.h {
                var dark = RGB(0x8C8AEF).mix(RGB(0x4A4DA5), y, n)
                var light = RGB(0xF2F1FF).mix(RGB(0xB5B2FF), y, n)
                if y == 0 { dark = RGB(0xB5B2FF); light = Platinum.white }
                // Runs of 8 pixels alternate along the row, shifted one pixel per row.
                var x = 0
                while x < inner.w {
                    let s = (((x - y + phase) % 16) + 16) % 16
                    let run = min(s < 8 ? 8 - s : 16 - s, inner.w - x)
                    let c = s < 8 ? dark : light
                    p.hline(inner.x + x, inner.y + y, run, disabled ? c.dimmed : c)
                    x += run
                }
            }
            return
        }
        // Native determinate bar: 10 scanlines, with a six-pixel bevel at
        // the advancing edge. The public rect is measured on the black line.
        let outer = r.outset(1), c: (Character) -> RGB = { MeasuredChrome.color($0)! }
        p.hline(outer.x, outer.y, outer.w - 1, c("a"))
        p.vline(outer.x, outer.y, outer.h - 1, c("a"))
        p.pixel(outer.x, outer.y + outer.h - 1, c("d"))
        p.pixel(outer.x + outer.w - 1, outer.y, c("d"))
        let track = Array("8bbbbbbbbd"), trackEnd = Array("bddddddddd")
        let second = Array("DCBAAABCDE"), body = Array("EDCBABCDEG")
        let end = ["EEEK5", "DDGK5", "CEGK5", "BEGK5", "BEGK5", "BEGK5", "CEGK5", "DEGK5", "EEGK5", "GGGK5"]
        let filled = value <= 0 ? 0 : max(5, min(inner.w, Int((Double(inner.w) * min(1, value)).rounded())))
        for y in 0..<inner.h {
            let row = min(9, y)
            p.hline(inner.x, inner.y + y, inner.w, c(track[row]))
            p.pixel(inner.x + inner.w - 1, inner.y + y, c(trackEnd[row]))
            p.pixel(inner.x, inner.y + y, c("8"))
            if filled > 0 {
                p.pixel(inner.x, inner.y + y, c("D"))
                p.pixel(inner.x + 1, inner.y + y, c(second[row]))
                p.hline(inner.x + 2, inner.y + y, max(0, filled - 2), c(body[row]))
                if filled < inner.w { MeasuredChrome.sprite(p, [end[row] + "8"], x: inner.x + filled - 3, y: inner.y + y) }
            }
        }
    }

    // MARK: - Scroll bar

    static let scrollUp = [
        "KKKKKKKKKKKKKKKK",
        "KWWWWWWWWWWWWWEK",
        "KWEEEEEEEEEEEEcK",
        "KWEEEEEEEEEEEEcK",
        "KWEEEEEEEEEEEEcK",
        "KWEEEEEEEEEEEEcK",
        "KWEEEEEKKEEEEEcK",
        "KWEEEEKKKKEEEEcK",
        "KWEEEKKKKKKEEEcK",
        "KWEEKKKKKKKKEEcK",
        "KWEEEEEEEEEEEEcK",
        "KWEEEEEEEEEEEEcK",
        "KWEEEEEEEEEEEEcK",
        "KWEEEEEEEEEEEEcK",
        "KEcccccccccccccK",
        "KKKKKKKKKKKKKKKK",
    ]
    static let thumb = [
        "KKKKKKKKKKKKKKKK",
        "KFllllllllllllaK",
        "KlaaaaaaaaaaaasK",
        "KlaaaaaaaaaaaasK",
        "KlaaFllllllaaasK",
        "KlaaadddddddaasK",
        "KlaaFllllllaaasK",
        "KlaaadddddddaasK",
        "KlaaFllllllaaasK",
        "KlaaadddddddaasK",
        "KlaaFllllllaaasK",
        "KlaaadddddddaasK",
        "KlaaaaaaaaaaaasK",
        "KlaaaaaaaaaaaasK",
        "KlaaaaaaaaaaaasK",
        "KasssssssssssssK",
        "KKKKKKKKKKKKKKKK",
    ]
    public static let thumbHeight = 17

    /// One 16x16 arrow box of a vertical scroll bar.
    public static func scrollArrow(_ p: PixelPainter, x: Int, y: Int, up: Bool, active: Bool, pressed: Bool) {
        for row in 0..<16 {
            let src = (!up && row >= 6 && row <= 9) ? 15 - row : row
            for (col, ch) in scrollUp[src].enumerated() {
                guard var c = PlatinumCode.color(ch) else { continue }
                if row >= 6 && row <= 9 && c == Platinum.black && col > 1 && col < 14 {
                    if !active { c = Platinum.grayA5 } else if pressed { c = Platinum.white }
                } else if pressed && c != Platinum.black {
                    c = c == Platinum.white ? Platinum.gray63 : Platinum.gray73
                } else if !active && c != Platinum.black {
                    c = Platinum.grayF7
                } else if !active && col > 0 && col < 15 && row == (up ? 15 : 0) {
                    c = Platinum.gray73
                }
                p.pixel(x + col, y + row, c)
            }
        }
    }

    /// A vertical scroll bar filling r (16 wide). `thumbY` is the thumb's
    /// top within r, or nil when there is nothing to scroll.
    public static func scrollBar(_ p: PixelPainter, _ r: PixelRect, thumbY: Int?, pressedArrow: Int?) {
        let active = thumbY != nil
        scrollArrow(p, x: r.x, y: r.y, up: true, active: active, pressed: pressedArrow == -1)
        scrollArrow(p, x: r.x, y: r.y + r.h - 16, up: false, active: active, pressed: pressedArrow == 1)
        let trackY = r.y + 16, trackH = r.h - 32
        guard trackH > 0 else { return }
        p.vline(r.x, trackY, trackH, Platinum.black)
        p.vline(r.x + 15, trackY, trackH, Platinum.black)
        guard let thumbY else {
            p.fill(r.x + 1, trackY, 14, trackH, Platinum.grayF7)
            return
        }
        for y in trackY..<(trackY + trackH) {
            p.pixel(r.x + 1, y, Platinum.gray94)
            p.pixel(r.x + 2, y, Platinum.grayA5)
            p.hline(r.x + 3, y, 10, Platinum.grayC5)
            p.pixel(r.x + 13, y, Platinum.grayCE)
            p.pixel(r.x + 14, y, Platinum.grayDE)
        }
        p.hline(r.x + 1, trackY, 13, Platinum.gray94)
        p.hline(r.x + 2, trackY + 1, 11, Platinum.grayA5)
        guard trackH >= thumbHeight else { return }
        let ty = min(max(thumbY, trackY), trackY + trackH - thumbHeight)
        p.coded(thumb, x: r.x, y: ty)
    }

    // MARK: - Menus

    static let checkMark = [
        ".......##",
        "......##.",
        "......##.",
        ".....##..",
        "##..##...",
        ".####....",
        "..##.....",
        "..#......",
    ]

    /// A menu's frame and face, with a soft two-pixel shadow outside r.
    public static func menu(_ p: PixelPainter, _ r: PixelRect) {
        for i in 1...2 {
            let a: CGFloat = i == 1 ? 0.5 : 0.25
            p.shade(r.x + r.w - 1 + i, r.y + i + 1, 1, r.h - 1, alpha: a)
            p.shade(r.x + i + 1, r.y + r.h - 1 + i, r.w - 2, 1, alpha: a)
        }
        p.frame(r, Platinum.black)
        p.fill(r.inset(1), Platinum.face)
        p.hline(r.x + 1, r.y + 1, r.w - 2, Platinum.white)
        p.vline(r.x + 1, r.y + 1, r.h - 2, Platinum.white)
        p.hline(r.x + 2, r.y + r.h - 2, r.w - 3, Platinum.grayB5)
        p.vline(r.x + r.w - 2, r.y + 2, r.h - 3, Platinum.grayB5)
    }

    public static func checkmark(_ p: PixelPainter, x: Int, y: Int, color: RGB) {
        for (r, row) in checkMark.enumerated() {
            for (c, ch) in row.enumerated() where ch == "#" { p.pixel(x + c, y + r, color) }
        }
    }

    // MARK: - Windows

    /// Six raised pinstripes from x0 to x1 starting at y.
    public static func pinstripes(_ p: PixelPainter, x0: Int, x1: Int, y: Int) {
        guard x1 - x0 >= 2 else { return }
        for i in 0..<6 {
            p.hline(x0 - 1, y + 2 * i, x1 - x0, Platinum.white)
            p.hline(x0, y + 2 * i + 1, x1 - x0, Platinum.gray94)
        }
    }

    /// The kinds of Platinum window frame.
    public enum WindowKind: Sendable {
        /// A document window: title bar with close, zoom and collapse boxes.
        case document
        /// A movable modal dialog: title bar, no boxes.
        case movableModal
        /// A modal dialog or alert: no title bar, thick frame.
        case modal
        /// A floating utility window with a short title bar (Mac OS 8 "floating").
        case utility
    }

    /// Content insets from the window frame (shadow excluded) for a window
    /// kind: rbui's PT_TITLE_H + 1 at the top and PT_FRAME at the sides.
    public static func contentInsets(_ kind: WindowKind) -> (top: Int, left: Int, bottom: Int, right: Int) {
        switch kind {
        case .document, .movableModal: return (22, 6, 6, 6)
        case .modal: return (6, 6, 6, 6)
        case .utility: return (14, 4, 4, 4)
        }
    }

    /// Which title-bar boxes a window shows.
    public struct TitleBoxes: Sendable, Equatable {
        public var close: Bool
        public var zoom: Bool
        public var collapse: Bool

        public init(close: Bool = false, zoom: Bool = false, collapse: Bool = false) {
            self.close = close
            self.zoom = zoom
            self.collapse = collapse
        }

        public static let none = TitleBoxes()
        public static let document = TitleBoxes(close: true, zoom: true, collapse: true)
    }

    /// Box positions within a window of width w (frame, shadow excluded).
    public static func closeBoxOrigin() -> (x: Int, y: Int) { (4, 4) }
    public static func zoomBoxOrigin(width w: Int, collapse: Bool) -> (x: Int, y: Int) {
        (w - (collapse ? 33 : 17), 4)
    }
    public static func collapseBoxOrigin(width w: Int) -> (x: Int, y: Int) { (w - 17, 4) }

    /// The window frame filling `full`, whose last row and column hold the
    /// one-pixel drop shadow. Draws the frame, the inset content well and,
    /// for titled kinds, the pinstripes around a title of `titleWidth`.
    /// Returns the title's text origin (x, baseline).
    @discardableResult
    public static func window(_ p: PixelPainter, _ full: PixelRect, kind: WindowKind, active: Bool,
                              titleWidth: Int, boxes: TitleBoxes = .none,
                              collapsed: Bool = false, growable: Bool = false) -> (x: Int, baseline: Int) {
        let f = PixelRect(full.x, full.y, full.w - 1, full.h - 1)
        let x0 = f.x, y0 = f.y, x1 = f.x + f.w - 1, y1 = f.y + f.h - 1
        // Outline plus a one-pixel drop shadow on the right and bottom.
        p.frame(f, active ? Platinum.black : Platinum.gray73)
        let shadow = active ? Platinum.black : Platinum.gray73
        p.vline(x1 + 1, y0 + 2, f.h - 1, shadow)
        p.hline(x0 + 2, y1 + 1, f.w - 1, shadow)
        p.fill(f.inset(1), active ? Platinum.frame : Platinum.face)
        if active {
            // Raised outer bevel: white top/left, B5 bottom/right.
            p.hline(x0 + 1, y0 + 1, f.w - 2, Platinum.white)
            p.vline(x0 + 1, y0 + 1, f.h - 2, Platinum.white)
            p.hline(x0 + 1, y1 - 1, f.w - 2, Platinum.grayB5)
            p.vline(x1 - 1, y0 + 1, f.h - 2, Platinum.grayB5)
            p.pixel(x1 - 1, y0 + 1, Platinum.frame)
            p.pixel(x0 + 1, y1 - 1, Platinum.frame)
        }
        if !collapsed {
            let insets = contentInsets(kind)
            let c = PixelRect(x0 + insets.left, y0 + insets.top, f.w - insets.left - insets.right,
                              f.h - insets.top - insets.bottom)
            if active {
                // Inset content: B5 above/left, white below/right, then a black line.
                p.hline(c.x - 2, c.y - 2, c.w + 3, Platinum.grayB5)
                p.vline(c.x - 2, c.y - 2, c.h + 3, Platinum.grayB5)
                p.hline(c.x - 1, c.y + c.h + 1, c.w + 3, Platinum.white)
                p.vline(c.x + c.w + 1, c.y - 1, c.h + 3, Platinum.white)
            }
            p.frame(c.outset(1), active ? Platinum.black : Platinum.gray73)
            p.fill(c, Platinum.face)
            if growable { growBox(p, x: c.x + c.w - 15, y: c.y + c.h - 15, active: active) }
        }

        guard kind != .modal else { return (0, 0) }
        let rows = kind == .utility ? 4 : 6
        let stripeTop = y0 + 4
        let tx = x0 + (f.w - titleWidth) / 2
        if active {
            var left = x0 + 9
            var right = x1 - 8
            if boxes.close { left = x0 + 4 + 13 + 5 }
            if boxes.collapse { right = x0 + f.w - 17 - 4 }
            if boxes.zoom { right = x0 + f.w - (boxes.collapse ? 33 : 17) - 4 }
            func stripes(_ a: Int, _ b: Int) {
                guard b - a >= 2 else { return }
                for i in 0..<rows {
                    p.hline(a - 1, stripeTop + 2 * i, b - a, Platinum.white)
                    p.hline(a, stripeTop + 2 * i + 1, b - a, Platinum.gray94)
                }
            }
            if titleWidth > 0 {
                stripes(left, tx - 3)
                stripes(tx + titleWidth + 4, right)
            } else {
                stripes(left, right)
            }
        }
        return (tx, kind == .utility ? y0 + 11 : y0 + 16)
    }

    /// One 13x13 title-bar box (close, zoom or collapse) with its top-left at (x, y).
    public enum TitleBox: Sendable { case close, zoom, collapse }

    public static func titleBox(_ p: PixelPainter, x: Int, y: Int, kind: TitleBox, pressed: Bool) {
        if pressed {
            let rows = kind == .close ? MeasuredChrome.WINDOW_CLOSE_PRESSED :
                kind == .zoom ? MeasuredChrome.WINDOW_ZOOM_PRESSED : MeasuredChrome.WINDOW_COLLAPSE_PRESSED
            MeasuredChrome.sprite(p, rows, x: x, y: y)
            return
        }
        let c: (Character) -> RGB = { MeasuredChrome.color($0)! }
        p.fill(x, y, 13, 13, c("c"))
        p.hline(x, y, 12, c("8")); p.vline(x, y, 12, c("8"))
        p.hline(x + 1, y + 12, 12, c("W")); p.vline(x + 12, y + 1, 12, c("W"))
        p.frame(PixelRect(x + 1, y + 1, 11, 11), c("2"))
        p.hline(x + 2, y + 2, 8, c("c")); p.vline(x + 2, y + 2, 8, c("c"))
        p.hline(x + 3, y + 10, 8, c("8")); p.vline(x + 10, y + 3, 8, c("8"))
        p.pixel(x + 2, y + 2, c("W"))
        let ramp = Array("99aabbccddeeW")
        for j in 0..<7 { for i in 0..<7 { p.pixel(x + 3 + i, y + 3 + j, c(ramp[i+j])) } }
        if kind == .zoom {
            p.vline(x + 7, y + 2, 6, c("2")); p.hline(x + 2, y + 7, 6, c("2"))
        } else if kind == .collapse {
            p.hline(x + 1, y + 5, 11, c("2")); p.hline(x + 1, y + 7, 11, c("2"))
        }
    }

    /// The 15-pixel grow area plus its two-pixel inset border. Mac OS 9 uses
    /// diagonal grips; the inactive well retains the notch without grips.
    public static func growBox(_ p: PixelPainter, x: Int, y: Int, active: Bool) {
        MeasuredChrome.sprite(p, active ? MeasuredChrome.WINDOW_GROW : MeasuredChrome.WINDOW_GROW_INACTIVE, x: x, y: y)
    }

    /// Appearance Manager small bevel button used by the VM placard bar.
    public static func smallBevel(_ p: PixelPainter, _ r: PixelRect, state: ControlState) {
        let rows = state == .pressed ? MeasuredChrome.SMALL_BEVEL_PRESSED :
            state == .disabled ? MeasuredChrome.SMALL_BEVEL_DISABLED : MeasuredChrome.SMALL_BEVEL_NORMAL
        MeasuredChrome.stretch(p, r, rows, repeatAt: 2)
    }
}
