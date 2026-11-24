import SwiftUI

/// A Platinum edit text field: System 12 in a white sunken frame, a
/// lavender selection and the two-pixel focus ring while editing.
public struct PlatinumTextField: View {
    @Binding var text: String
    let placeholder: String
    let width: CGFloat?
    let alignment: TextAlignment
    let onCommit: (() -> Void)?
    @State private var editing = false
    @Environment(\.isEnabled) private var isEnabled
    @Environment(\.platinumWindow) private var platinumWindow

    private var windowActive: Bool { (platinumWindow?.isActive ?? true) || PlatinumDebug.forceActive }

    public init(_ placeholder: String = "", text: Binding<String>, width: CGFloat? = nil,
                alignment: TextAlignment = .leading, onCommit: (() -> Void)? = nil) {
        self.placeholder = placeholder
        _text = text
        self.width = width
        self.alignment = alignment
        self.onCommit = onCommit
    }

    public var body: some View {
        field
            .padding(.horizontal, 4)
            .frame(width: width.map { $0 - 2 })
            .frame(maxWidth: width == nil ? .infinity : nil)
            .frame(height: Platinum.fieldHeight)
            .background {
                PixelCanvas { p, r in Chrome.field(p, r.inset(1), disabled: !isEnabled) }
            }
            .overlay {
                if editing && isEnabled && windowActive {
                    PixelCanvas { p, r in Chrome.focusRing(p, around: r.inset(2)) }
                        .padding(-1)
                        .allowsHitTesting(false)
                }
            }
            .padding(1)
            .onChange(of: isEnabled) { _, enabled in
                if !enabled { editing = false }
            }
    }

    @ViewBuilder
    private var field: some View {
        #if os(macOS)
        PlatinumNSTextField(text: $text, placeholder: placeholder, alignment: alignment,
                            enabled: isEnabled, editing: $editing, onCommit: onCommit)
        #else
        TextField(placeholder, text: $text, onEditingChanged: { editing = $0 }, onCommit: { onCommit?() })
            .textFieldStyle(.plain)
            .font(.platinumSystem)
            .multilineTextAlignment(alignment)
        #endif
    }
}

#if os(macOS)
import AppKit

struct PlatinumNSTextField: NSViewRepresentable {
    @Binding var text: String
    let placeholder: String
    let alignment: TextAlignment
    let enabled: Bool
    @Binding var editing: Bool
    let onCommit: (() -> Void)?

    final class FieldView: NSTextField {
        var onFocus: (() -> Void)?

        override func becomeFirstResponder() -> Bool {
            guard isEnabled else { return false }
            let became = super.becomeFirstResponder()
            if became {
                if let editor = currentEditor() as? NSTextView { Coordinator.platinum(editor) }
                onFocus?()
            }
            return became
        }
    }

    func makeNSView(context: Context) -> NSTextField {
        let field = FieldView(string: text)
        let coordinator = context.coordinator
        field.onFocus = { [weak coordinator] in coordinator?.parent.editing = true }
        field.isBordered = false
        field.isBezeled = false
        field.drawsBackground = false
        field.focusRingType = .none
        field.font = PlatinumFont.system.nsFont()
        field.textColor = .black
        field.lineBreakMode = .byClipping
        field.usesSingleLineMode = true
        field.cell?.isScrollable = true
        field.cell?.wraps = false
        field.delegate = context.coordinator
        field.placeholderAttributedString = NSAttributedString(string: placeholder, attributes: [
            .font: PlatinumFont.system.nsFont() as Any,
            .foregroundColor: NSColor(Platinum.disabledText.color)
        ])
        return field
    }

    func updateNSView(_ field: NSTextField, context: Context) {
        context.coordinator.parent = self
        if field.stringValue != text { field.stringValue = text }
        if !enabled {
            if field.currentEditor() != nil || field.window?.firstResponder === field {
                if field.window?.makeFirstResponder(nil) == false {
                    field.abortEditing()
                }
            }
            if editing {
                DispatchQueue.main.async { editing = false }
            }
        }
        field.isEnabled = enabled
        field.textColor = enabled ? .black : NSColor(Platinum.disabledText.color)
        switch alignment {
        case .center: field.alignment = .center
        case .trailing: field.alignment = .right
        default: field.alignment = .left
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator(parent: self) }

    final class Coordinator: NSObject, NSTextFieldDelegate {
        var parent: PlatinumNSTextField

        init(parent: PlatinumNSTextField) { self.parent = parent }

        func controlTextDidBeginEditing(_ notification: Notification) {
            style(notification)
            parent.editing = true
        }

        func controlTextDidChange(_ notification: Notification) {
            guard let field = notification.object as? NSTextField else { return }
            parent.text = field.stringValue
        }

        func controlTextDidEndEditing(_ notification: Notification) {
            parent.editing = false
            if let movement = notification.userInfo?["NSTextMovement"] as? Int,
               movement == NSTextMovement.return.rawValue {
                parent.onCommit?()
            }
        }

        // Focus arrives before the first edit; style the field editor then.
        func control(_ control: NSControl, textShouldBeginEditing fieldEditor: NSText) -> Bool {
            if let editor = fieldEditor as? NSTextView { Self.platinum(editor) }
            return true
        }

        private func style(_ notification: Notification) {
            if let editor = notification.userInfo?["NSFieldEditor"] as? NSTextView { Self.platinum(editor) }
        }

        static func platinum(_ editor: NSTextView) {
            editor.insertionPointColor = .black
            editor.selectedTextAttributes = [
                .backgroundColor: NSColor(Platinum.highlight.color),
                .foregroundColor: NSColor.black
            ]
        }
    }
}
#endif
