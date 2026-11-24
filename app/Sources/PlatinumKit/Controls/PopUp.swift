import SwiftUI

/// One item of a Platinum pop-up menu.
public struct PlatinumMenuItem<Value: Hashable> {
    public let value: Value?
    public let title: String
    public let enabled: Bool

    public init(_ title: String, value: Value, enabled: Bool = true) {
        self.title = title
        self.value = value
        self.enabled = enabled
    }

    private init(separator: Void) {
        self.title = ""
        self.value = nil
        self.enabled = false
    }

    public static var separator: PlatinumMenuItem { PlatinumMenuItem(separator: ()) }
    public var isSeparator: Bool { value == nil }
}

/// A Platinum pop-up menu button. Pressing it opens a Platinum menu with the
/// current item over the button; drag and release, or click and click again.
public struct PlatinumPopUp<Value: Hashable>: View {
    @Binding var selection: Value
    let items: [PlatinumMenuItem<Value>]
    let width: CGFloat?
    @State private var tracking = false
    @Environment(\.isEnabled) private var isEnabled
    @Environment(\.platinumStaticRender) private var staticRender

    public init(selection: Binding<Value>, items: [PlatinumMenuItem<Value>], width: CGFloat? = nil) {
        _selection = selection
        self.items = items
        self.width = width
    }

    /// Convenience for simple (value, title) pairs.
    public init(selection: Binding<Value>, options: [(Value, String)], width: CGFloat? = nil) {
        self.init(selection: selection, items: options.map { PlatinumMenuItem($0.1, value: $0.0) }, width: width)
    }

    private var title: String {
        items.first { $0.value == selection }?.title ?? ""
    }

    private var naturalWidth: CGFloat {
        let widest = items.map { PlatinumFonts.width($0.title, font: .system) }.max() ?? 40
        return widest + 9 + 4 + 19 + 4
    }

    public var body: some View {
        let state: ControlState = !isEnabled ? .disabled : tracking ? .pressed : .normal
        ZStack(alignment: .leading) {
            PixelCanvas { p, r in Chrome.popup(p, r, state: state) }
            Text(title)
                .font(.platinumSystem)
                .foregroundStyle(Chrome.buttonText(state).color)
                .lineLimit(1)
                .truncationMode(.tail)
                .padding(.leading, 9)
                .padding(.trailing, 24)
        }
        .frame(width: width ?? naturalWidth, height: Platinum.popupHeight)
        .overlay { tracker }
        .accessibilityRepresentation {
            Picker(selection: $selection) {
                ForEach(Array(items.enumerated()), id: \.offset) { _, item in
                    if let value = item.value { Text(item.title).tag(value) }
                }
            } label: {
                Text(title)
            }
        }
    }

    @ViewBuilder
    private var tracker: some View {
        #if os(macOS)
        if isEnabled && !staticRender {
            PopUpTrackingArea(
                titles: items.map(\.title),
                separators: items.map(\.isSeparator),
                enabled: items.map(\.enabled),
                selected: items.firstIndex { $0.value == selection },
                pressed: $tracking
            ) { index in
                if let value = items[index].value { selection = value }
            }
        }
        #else
        Menu {
            ForEach(Array(items.enumerated()), id: \.offset) { _, item in
                if let value = item.value {
                    Button(item.title) { selection = value }.disabled(!item.enabled)
                } else {
                    Divider()
                }
            }
        } label: {
            Color.clear
        }
        #endif
    }
}

#if os(macOS)
import AppKit

struct PopUpTrackingArea: NSViewRepresentable {
    let titles: [String]
    let separators: [Bool]
    let enabled: [Bool]
    let selected: Int?
    @Binding var pressed: Bool
    let onSelect: (Int) -> Void

    func makeNSView(context: Context) -> TrackingView { TrackingView() }

    func updateNSView(_ view: TrackingView, context: Context) {
        view.items = titles.indices.map {
            PlatinumMenu.Item(title: titles[$0], enabled: enabled[$0], separator: separators[$0], checked: $0 == selected)
        }
        view.selected = selected
        view.onPress = { pressed = $0 }
        view.onSelect = onSelect
    }

    final class TrackingView: NSView {
        var items: [PlatinumMenu.Item] = []
        var selected: Int?
        var onPress: ((Bool) -> Void)?
        var onSelect: ((Int) -> Void)?

        override var acceptsFirstResponder: Bool { true }
        override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

        override func mouseDown(with event: NSEvent) {
            window?.makeFirstResponder(self)
            openMenu(with: event)
        }

        override func keyDown(with event: NSEvent) {
            switch event.keyCode {
            case 49, 36, 76, 125, 126: // Space, Return, Enter, Down, Up
                openMenu(with: nil)
            default:
                super.keyDown(with: event)
            }
        }

        private func openMenu(with event: NSEvent?) {
            guard let window, !items.isEmpty else { return }
            let anchor = window.convertToScreen(convert(bounds, to: nil))
            onPress?(true)
            let choice = PlatinumMenu.popUp(items: items, selected: selected, anchor: anchor,
                                            textInset: 9, event: event, in: window)
            onPress?(false)
            if let choice { onSelect?(choice) }
        }

        override func isAccessibilityElement() -> Bool { true }
        override func accessibilityRole() -> NSAccessibility.Role? { .popUpButton }
        override func accessibilityTitle() -> String? {
            selected.flatMap { items.indices.contains($0) ? items[$0].title : nil } ?? ""
        }
        override func accessibilityValue() -> Any? { accessibilityTitle() }
        override func accessibilityChildren() -> [Any]? { nil }
        override func accessibilityPerformPress() -> Bool {
            guard window != nil, !items.isEmpty else { return false }
            openMenu(with: nil)
            return true
        }
    }
}
#endif
