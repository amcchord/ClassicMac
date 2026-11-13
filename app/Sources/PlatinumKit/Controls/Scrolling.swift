import SwiftUI

/// Scroll metrics shared between a scroll view and its Platinum scroll bar.
struct ScrollMetrics: Equatable {
    var offset: CGFloat = 0
    var content: CGFloat = 0
    var visible: CGFloat = 0

    var maxOffset: CGFloat { max(0, content - visible) }
    var canScroll: Bool { maxOffset > 0.5 }
}

/// A vertical Platinum scroll bar: paired lower arrows, a sunken channel and
/// the lavender thumb. Arrows and the channel auto-repeat while held.
struct PlatinumScrollBar: View {
    let metrics: ScrollMetrics
    let line: CGFloat
    let scrollTo: (CGFloat) -> Void

    @State private var pressedArrow: Int?
    @State private var dragStart: (offset: CGFloat, y: CGFloat)?
    @State private var repeater: Timer?

    var body: some View {
        GeometryReader { geo in
            let h = Int(geo.size.height)
            let thumbY = thumbTop(height: h)
            PixelCanvas { p, r in
                Chrome.scrollBar(p, PixelRect(0, 0, 16, r.h), thumbY: thumbY, pressedArrow: pressedArrow)
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in changed(value, height: h) }
                    .onEnded { _ in ended() }
            )
        }
        .frame(width: Platinum.scrollbarWidth)
        .accessibilityHidden(true)
    }

    private func thumbTop(height: Int) -> Int? {
        guard metrics.canScroll else { return nil }
        let trackY = 0, trackH = height - 31
        let span = trackH - Chrome.thumbHeight
        guard span > 0 else { return trackY }
        return trackY + Int((metrics.offset / metrics.maxOffset * CGFloat(span)).rounded())
    }

    private func changed(_ value: DragGesture.Value, height: Int) {
        guard metrics.canScroll else { return }
        if let start = dragStart {
            // Dragging the thumb: map pointer motion onto the scroll range.
            let span = CGFloat(height - 31 - Chrome.thumbHeight)
            guard span > 0 else { return }
            let delta = (value.location.y - start.y) / span * metrics.maxOffset
            scrollTo(min(max(0, start.offset + delta), metrics.maxOffset))
            return
        }
        guard repeater == nil, pressedArrow == nil else {
            // Track whether the pointer is still over the pressed arrow.
            if let arrow = pressedArrow, arrow != 0 {
                let y = value.location.y
                let inside = arrow < 0 ? y >= CGFloat(height - 31) && y < CGFloat(height - 16)
                                      : y >= CGFloat(height - 16)
                if !inside { stopRepeat() }
            }
            return
        }
        let y = value.startLocation.y
        let thumb = CGFloat(thumbTop(height: height) ?? 0)
        if y >= CGFloat(height - 31) && y < CGFloat(height - 16) {
            begin(arrow: -1) { step(-line) }
        } else if y >= CGFloat(height - 16) {
            begin(arrow: 1) { step(line) }
        } else if y < thumb {
            begin(arrow: 0) { step(-metrics.visible + line) }
        } else if y >= thumb + CGFloat(Chrome.thumbHeight) {
            begin(arrow: 0) { step(metrics.visible - line) }
        } else {
            dragStart = (metrics.offset, value.startLocation.y)
        }
    }

    private func begin(arrow: Int, _ action: @escaping () -> Void) {
        pressedArrow = arrow == 0 ? nil : arrow
        action()
        // Mac OS waits a moment, then repeats about every 50 ms.
        let first = Timer(timeInterval: 0.3, repeats: false) { _ in
            let repeating = Timer(timeInterval: 0.05, repeats: true) { _ in
                MainActor.assumeIsolated { action() }
            }
            RunLoop.main.add(repeating, forMode: .common)
            MainActor.assumeIsolated { repeater = repeating }
        }
        RunLoop.main.add(first, forMode: .common)
        repeater = first
    }

    private func step(_ delta: CGFloat) {
        scrollTo(min(max(0, metrics.offset + delta), metrics.maxOffset))
    }

    private func stopRepeat() {
        repeater?.invalidate()
        repeater = nil
    }

    private func ended() {
        stopRepeat()
        pressedArrow = nil
        dragStart = nil
    }
}

/// A scroll view with a Platinum scroll bar on the right. Content keeps the
/// native wheel and trackpad scrolling.
public struct PlatinumScrollView<Content: View>: View {
    let line: CGFloat
    let contentInsets: EdgeInsets
    let content: Content
    @State private var position = ScrollPosition(edge: .top)
    @State private var metrics = ScrollMetrics()
    @Environment(\.platinumStaticRender) private var staticRender

    /// - Parameter contentInsets: padding around the scrolling content only;
    ///   the scroll bar always spans the full height.
    public init(line: CGFloat = Platinum.listRowHeight, contentInsets: EdgeInsets = EdgeInsets(),
                @ViewBuilder content: () -> Content) {
        self.line = line
        self.contentInsets = contentInsets
        self.content = content()
    }

    public var body: some View {
        HStack(spacing: 0) {
            if staticRender {
                // Image renders cannot draw a live scroll view: show the top.
                content
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                    .clipped()
                    .padding(contentInsets)
            } else {
                ScrollView(.vertical) {
                    content
                }
                .padding(contentInsets)
            .scrollIndicators(.never)
            .scrollPosition($position)
            .scrollBounceBehavior(.basedOnSize)
            .onScrollGeometryChange(for: ScrollMetrics.self) { geometry in
                ScrollMetrics(offset: geometry.contentOffset.y + geometry.contentInsets.top,
                              content: geometry.contentSize.height,
                              visible: geometry.containerSize.height)
            } action: { _, new in
                metrics = new
            }
            }
            PlatinumScrollBar(metrics: metrics, line: line) { y in
                position.scrollTo(y: y)
            }
        }
    }
}

// MARK: - List box

/// A Platinum list box: white rows in a sunken frame with a scroll bar,
/// 18-point rows and a lavender selection. Up and Down arrows move the
/// selection while the list has keyboard focus.
public struct PlatinumList<Item: Identifiable, Row: View>: View {
    let items: [Item]
    @Binding var selection: Item.ID?
    let rowHeight: CGFloat
    let onDoubleClick: ((Item) -> Void)?
    let row: (Item, Bool) -> Row
    @FocusState private var focused: Bool
    @Environment(\.isEnabled) private var isEnabled

    public init(_ items: [Item], selection: Binding<Item.ID?>, rowHeight: CGFloat = Platinum.listRowHeight,
                onDoubleClick: ((Item) -> Void)? = nil,
                @ViewBuilder row: @escaping (Item, Bool) -> Row) {
        self.items = items
        _selection = selection
        self.rowHeight = rowHeight
        self.onDoubleClick = onDoubleClick
        self.row = row
    }

    public var body: some View {
        ScrollViewReader { proxy in
            // The scroll bar shares the list frame's top, right and bottom lines.
            PlatinumScrollView(line: rowHeight, contentInsets: EdgeInsets(top: 1, leading: 1, bottom: 1, trailing: 0)) {
                    LazyVStack(alignment: .leading, spacing: 0) {
                        ForEach(items) { item in
                            let selected = item.id == selection
                            row(item, selected)
                                .frame(maxWidth: .infinity, minHeight: rowHeight, maxHeight: rowHeight, alignment: .leading)
                                .background(selected ? Platinum.highlight.color : Color.clear)
                                .contentShape(Rectangle())
                                .id(item.id)
                                .onTapGesture(count: 2) {
                                    selection = item.id
                                    onDoubleClick?(item)
                                }
                                .simultaneousGesture(TapGesture().onEnded {
                                    selection = item.id
                                    focused = true
                                })
                        }
                    }
            }
            .padding(1)
            .background {
                PixelCanvas { p, r in Chrome.listBox(p, r.inset(1), disabled: !isEnabled) }
            }
            .overlay {
                if focused {
                    PixelCanvas { p, r in Chrome.focusRing(p, around: r.inset(2)) }
                        .padding(-1)
                        .allowsHitTesting(false)
                }
            }
            .focusable()
            .focusEffectDisabled()
            .focused($focused)
            .onKeyPress(.upArrow) { move(-1, proxy); return .handled }
            .onKeyPress(.downArrow) { move(1, proxy); return .handled }
            .onKeyPress(.return) {
                if let id = selection, let item = items.first(where: { $0.id == id }) { onDoubleClick?(item) }
                return .handled
            }
        }
    }

    private func move(_ delta: Int, _ proxy: ScrollViewProxy) {
        guard !items.isEmpty else { return }
        let current = items.firstIndex { $0.id == selection } ?? (delta > 0 ? -1 : items.count)
        let next = min(max(current + delta, 0), items.count - 1)
        selection = items[next].id
        proxy.scrollTo(items[next].id)
    }
}
