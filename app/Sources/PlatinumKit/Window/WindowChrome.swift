import SwiftUI

/// Development switches read from the environment.
public enum PlatinumDebug {
    /// PLATINUM_FORCE_ACTIVE=1 draws every window active (for screenshots).
    public static let forceActive = ProcessInfo.processInfo.environment["PLATINUM_FORCE_ACTIVE"] != nil
}

/// Observable state of a Platinum window, owned by its host (an AppKit
/// `PlatinumWindow` on the Mac).
@MainActor
public final class PlatinumWindowState: ObservableObject {
    @Published public var title: String
    @Published public var isCollapsed = false
    @Published public var isActive = true
    /// A modal dialog is in front of this window; its content ignores clicks.
    @Published public var isBlockedByModal = false

    public var close: () -> Void = {}
    public var zoom: () -> Void = {}
    public var collapse: () -> Void = {}
    /// Called when the blocked window is clicked (beep, bring the dialog forward).
    public var blockedClick: () -> Void = {}

    public init(title: String) { self.title = title }
}

private struct PlatinumWindowStateKey: EnvironmentKey {
    static let defaultValue: PlatinumWindowState? = nil
}

private struct PlatinumStaticRenderKey: EnvironmentKey {
    static let defaultValue = false
}

public extension EnvironmentValues {
    /// True when views are rendered to an image (screenshots, tests): AppKit
    /// event areas are left out and lists draw without a live scroll view.
    var platinumStaticRender: Bool {
        get { self[PlatinumStaticRenderKey.self] }
        set { self[PlatinumStaticRenderKey.self] = newValue }
    }

    /// The Platinum window that hosts this view, if any.
    var platinumWindow: PlatinumWindowState? {
        get { self[PlatinumWindowStateKey.self] }
        set { self[PlatinumWindowStateKey.self] = newValue }
    }
}

/// A Platinum window frame around content: title bar with pinstripes and the
/// close, zoom and collapse boxes, the inset content well and the grow box.
public struct PlatinumWindowChrome<Content: View>: View {
    @ObservedObject var state: PlatinumWindowState
    let kind: Chrome.WindowKind
    let boxes: Chrome.TitleBoxes
    let growable: Bool
    let content: Content
    @Environment(\.platinumStaticRender) private var staticRender

    public init(state: PlatinumWindowState, kind: Chrome.WindowKind, boxes: Chrome.TitleBoxes = .none,
                growable: Bool = false, @ViewBuilder content: () -> Content) {
        self.state = state
        self.kind = kind
        self.boxes = boxes
        self.growable = growable
        self.content = content()
    }

    public var body: some View {
        GeometryReader { geo in
            let w = Int(geo.size.width), h = Int(geo.size.height)
            let active = state.isActive || kind == .modal || PlatinumDebug.forceActive
            let titled = kind != .modal
            let maxTitle = max(0, w - 1 - 60 - (boxes.zoom ? 24 : 0))
            let fullTitle = titled ? Int(PlatinumFonts.width(state.title, font: .system)) : 0
            let titleWidth = min(fullTitle, maxTitle)
            let insets = Chrome.contentInsets(kind)
            let titleX = (w - 1 - titleWidth) / 2
            let baseline = kind == .utility ? 11 : 16

            ZStack(alignment: .topLeading) {
                PixelCanvas { p, r in
                    Chrome.window(p, r, kind: kind, active: active, titleWidth: titleWidth,
                                  boxes: active ? boxes : .none, collapsed: state.isCollapsed, growable: growable)
                }

                if !state.isCollapsed {
                    content
                        .frame(width: CGFloat(w - 1 - insets.left - insets.right),
                               height: CGFloat(h - 1 - insets.top - insets.bottom))
                        .clipped()
                        .offset(x: CGFloat(insets.left), y: CGFloat(insets.top))
                        .disabled(state.isBlockedByModal)
                        .overlay {
                            if state.isBlockedByModal {
                                Color.white.opacity(0.001)
                                    .onTapGesture { state.blockedClick() }
                            }
                        }
                }

                // The placard/content would otherwise paint over the grow box.
                if growable && !state.isCollapsed {
                    PixelCanvas { p, _ in Chrome.growBox(p, x: 0, y: 0, active: active) }
                        .frame(width: 17, height: 17)
                        .offset(x: CGFloat(w - 1 - insets.right - 15), y: CGFloat(h - 1 - insets.bottom - 15))
                        .allowsHitTesting(false)
                }

                #if os(macOS)
                if titled && !staticRender {
                    // The whole title bar moves the window; the boxes sit on top.
                    WindowDragArea(onDoubleClick: boxes.collapse ? { state.collapse() } : nil)
                        .frame(width: CGFloat(w - 2), height: CGFloat(insets.top - 2))
                        .offset(x: 1, y: 1)
                }
                if growable && !state.isCollapsed && !staticRender {
                    WindowGrowArea()
                        .frame(width: 15, height: 15)
                        .offset(x: CGFloat(w - 1 - insets.right - 15), y: CGFloat(h - 1 - insets.bottom - 15))
                }
                #endif

                if titled && titleWidth > 0 {
                    PixelCanvas { p, _ in
                        p.text(state.title, font: .system, x: 0, baseline: 12,
                               color: active ? Platinum.black : Platinum.disabledText, maxWidth: titleWidth)
                    }
                    .frame(width: CGFloat(titleWidth), height: 16)
                    .offset(x: CGFloat(titleX), y: CGFloat(baseline - 12))
                    .accessibilityLabel(state.title)
                    .allowsHitTesting(false)
                }

                if active && titled {
                    if boxes.close {
                        let o = Chrome.closeBoxOrigin()
                        TitleBoxButton(kind: .close, action: state.close)
                            .offset(x: CGFloat(o.x), y: CGFloat(o.y))
                            .accessibilityLabel("Close")
                    }
                    if boxes.zoom {
                        let o = Chrome.zoomBoxOrigin(width: w - 1, collapse: boxes.collapse)
                        TitleBoxButton(kind: .zoom, action: state.zoom)
                            .offset(x: CGFloat(o.x), y: CGFloat(o.y))
                            .accessibilityLabel("Zoom")
                    }
                    if boxes.collapse {
                        let o = Chrome.collapseBoxOrigin(width: w - 1)
                        TitleBoxButton(kind: .collapse, action: state.collapse)
                            .offset(x: CGFloat(o.x), y: CGFloat(o.y))
                            .accessibilityLabel(state.isCollapsed ? "Expand" : "Collapse")
                    }
                }
            }
            .frame(width: CGFloat(w), height: CGFloat(h), alignment: .topLeading)
        }
        .environment(\.platinumWindow, state)
        .ignoresSafeArea()
    }
}

private struct TitleBoxButton: View {
    let kind: Chrome.TitleBox
    let action: () -> Void

    var body: some View {
        Button(action: action) { Color.clear }
            .buttonStyle(TitleBoxStyle(kind: kind))
            .frame(width: 13, height: 13)
    }
}

private struct TitleBoxStyle: ButtonStyle {
    let kind: Chrome.TitleBox

    func makeBody(configuration: Configuration) -> some View {
        PixelCanvas { p, _ in Chrome.titleBox(p, x: 0, y: 0, kind: kind, pressed: configuration.isPressed) }
            .frame(width: 13, height: 13)
            .contentShape(Rectangle())
    }
}

#if os(macOS)
import AppKit

/// Moves the window from its title bar; a double-click collapses it
/// (WindowShade), as in Mac OS 8.
struct WindowDragArea: NSViewRepresentable {
    let onDoubleClick: (() -> Void)?

    func makeNSView(context: Context) -> DragView { DragView() }

    func updateNSView(_ view: DragView, context: Context) {
        view.onDoubleClick = onDoubleClick
    }

    final class DragView: NSView {
        var onDoubleClick: (() -> Void)?

        override func mouseDown(with event: NSEvent) {
            if event.clickCount == 2, let onDoubleClick {
                onDoubleClick()
                return
            }
            window?.performDrag(with: event)
        }

        override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
        override var mouseDownCanMoveWindow: Bool { false }
    }
}

/// The grow box: drag to resize the window, keeping its top-left corner fixed.
struct WindowGrowArea: NSViewRepresentable {
    func makeNSView(context: Context) -> GrowView { GrowView() }
    func updateNSView(_ view: GrowView, context: Context) {}

    final class GrowView: NSView {
        override func mouseDown(with event: NSEvent) {
            guard let window else { return }
            let start = NSEvent.mouseLocation
            let startFrame = window.frame
            while let e = NSApp.nextEvent(matching: [.leftMouseDragged, .leftMouseUp],
                                          until: .distantFuture, inMode: .eventTracking, dequeue: true) {
                if e.type == .leftMouseUp { break }
                let now = NSEvent.mouseLocation
                var width = (startFrame.width + now.x - start.x).rounded()
                var height = (startFrame.height - (now.y - start.y)).rounded()
                width = min(max(width, window.minSize.width), window.maxSize.width)
                height = min(max(height, window.minSize.height), window.maxSize.height)
                let frame = NSRect(x: startFrame.minX, y: startFrame.maxY - height, width: width, height: height)
                window.setFrame(frame, display: true)
            }
        }

        override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
        override func resetCursorRects() { addCursorRect(bounds, cursor: .arrow) }
    }
}
#endif
