import SwiftUI

// MARK: - Push button

/// The Platinum push button. 20 points tall, at least 59 wide, System 12 label.
public struct PlatinumButtonStyle: ButtonStyle {
    let isDefault: Bool
    let flashing: Bool
    let minWidth: CGFloat

    public init(isDefault: Bool = false, minWidth: CGFloat = Platinum.buttonMinWidth) {
        self.isDefault = isDefault
        self.flashing = false
        self.minWidth = minWidth
    }

    init(isDefault: Bool, flashing: Bool, minWidth: CGFloat) {
        self.isDefault = isDefault
        self.flashing = flashing
        self.minWidth = minWidth
    }

    public func makeBody(configuration: Configuration) -> some View {
        PushButtonBody(configuration: configuration, isDefault: isDefault, flashing: flashing, minWidth: minWidth)
    }
}

private struct PushButtonBody: View {
    let configuration: ButtonStyleConfiguration
    let isDefault: Bool
    let flashing: Bool
    let minWidth: CGFloat
    @Environment(\.isEnabled) private var isEnabled

    var body: some View {
        let state: ControlState = !isEnabled ? .disabled : (configuration.isPressed || flashing) ? .pressed : .normal
        configuration.label
            .font(.platinumSystem)
            .foregroundStyle(Chrome.buttonText(state).color)
            .lineLimit(1)
            .fixedSize()
            .padding(.horizontal, 12)
            .frame(minWidth: minWidth)
            .frame(height: Platinum.buttonHeight)
            .background(PixelCanvas { p, r in Chrome.button(p, r, state: state) })
            .padding(isDefault ? Platinum.defaultRing : 0)
            .background {
                if isDefault {
                    PixelCanvas { p, r in Chrome.defaultRing(p, around: r.inset(3), disabled: state == .disabled) }
                }
            }
            .contentShape(Rectangle())
    }
}

/// A Platinum push button with the classic keyboard behavior: the default
/// button answers Return and Enter, a cancel button Escape and Command-period,
/// and either flashes pressed for a moment before it acts, as Mac OS did.
public struct PlatinumButton<Label: View>: View {
    public enum Role { case normal, `default`, cancel }

    private let role: Role
    private let minWidth: CGFloat
    private let action: () -> Void
    private let label: Label
    @State private var flashing = false
    @State private var flashWork: DispatchWorkItem?
    @State private var isMounted = false
    @Environment(\.isEnabled) private var isEnabled

    public init(role: Role = .normal, minWidth: CGFloat = Platinum.buttonMinWidth,
                action: @escaping () -> Void, @ViewBuilder label: () -> Label) {
        self.role = role
        self.minWidth = minWidth
        self.action = action
        self.label = label()
    }

    public var body: some View {
        Button(action: action) { label }
            .buttonStyle(PlatinumButtonStyle(isDefault: role == .default, flashing: flashing, minWidth: minWidth))
            .background {
                // Hidden keyboard equivalents that flash the button first.
                switch role {
                case .default:
                    Button("", action: flash).keyboardShortcut(.defaultAction).hidden()
                case .cancel:
                    Group {
                        Button("", action: flash).keyboardShortcut(.cancelAction).hidden()
                        Button("", action: flash).keyboardShortcut(".", modifiers: .command).hidden()
                    }
                case .normal:
                    EmptyView()
                }
            }
            .onAppear { isMounted = true }
            .onDisappear { cancelFlash() }
            .onChange(of: isEnabled) { _, enabled in
                if !enabled { cancelFlash() }
            }
    }

    private func cancelFlash() {
        isMounted = false
        flashWork?.cancel()
        flashWork = nil
        flashing = false
    }

    private func flash() {
        guard isEnabled, !flashing, isMounted else { return }
        flashing = true
        flashWork?.cancel()
        var work: DispatchWorkItem!
        work = DispatchWorkItem {
            guard !work.isCancelled, isMounted, isEnabled else {
                flashing = false
                return
            }
            flashWork = nil
            flashing = false
            action()
        }
        flashWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.13, execute: work)
    }
}

public extension PlatinumButton where Label == Text {
    init(_ title: String, role: Role = .normal, minWidth: CGFloat = Platinum.buttonMinWidth,
         action: @escaping () -> Void) {
        self.init(role: role, minWidth: minWidth, action: action) { Text(title) }
    }
}

// MARK: - Checkbox

private struct CheckboxButtonStyle: ButtonStyle {
    let isOn: Bool
    let mixed: Bool

    func makeBody(configuration: Configuration) -> some View {
        CheckmarkBody(configuration: configuration, isOn: isOn, mixed: mixed, radio: false)
    }
}

private struct RadioButtonStyle: ButtonStyle {
    let isOn: Bool

    func makeBody(configuration: Configuration) -> some View {
        CheckmarkBody(configuration: configuration, isOn: isOn, mixed: false, radio: true)
    }
}

private struct CheckmarkBody: View {
    let configuration: ButtonStyleConfiguration
    let isOn: Bool
    let mixed: Bool
    let radio: Bool
    @Environment(\.isEnabled) private var isEnabled

    var body: some View {
        let state: ControlState = !isEnabled ? .disabled : configuration.isPressed ? .pressed : .normal
        HStack(alignment: .firstTextBaseline, spacing: Platinum.checkLabelOffset - 14) {
            PixelCanvas { p, _ in
                if radio {
                    Chrome.radio(p, x: 0, y: 0, on: isOn, state: state)
                } else if mixed {
                    Chrome.checkboxMixed(p, x: 0, y: 0, state: state)
                } else {
                    Chrome.checkbox(p, x: 0, y: 0, on: isOn, state: state)
                }
            }
            .frame(width: 14, height: 12)
            // The box sits with its bottom two pixels below the baseline.
            .alignmentGuide(.firstTextBaseline) { _ in 10 }
            configuration.label
                .font(.platinumSystem)
                .foregroundStyle(isEnabled ? Platinum.black.color : Platinum.disabledText.color)
                .fixedSize(horizontal: false, vertical: true)
        }
        .contentShape(Rectangle())
    }
}

/// A Platinum checkbox.
public struct PlatinumCheckbox<Label: View>: View {
    @Binding var isOn: Bool
    let mixed: Bool
    let label: Label

    public init(isOn: Binding<Bool>, mixed: Bool = false, @ViewBuilder label: () -> Label) {
        _isOn = isOn
        self.mixed = mixed
        self.label = label()
    }

    public var body: some View {
        Button { isOn.toggle() } label: { label }
            .buttonStyle(CheckboxButtonStyle(isOn: isOn, mixed: mixed))
            .accessibilityAddTraits(isOn ? [.isSelected] : [])
            .accessibilityValue(isOn ? "on" : "off")
    }
}

public extension PlatinumCheckbox where Label == Text {
    init(_ title: String, isOn: Binding<Bool>) {
        self.init(isOn: isOn) { Text(title) }
    }
}

/// `Toggle(...).toggleStyle(.platinumCheckbox)`
public struct PlatinumCheckboxToggleStyle: ToggleStyle {
    public init() {}
    public func makeBody(configuration: Configuration) -> some View {
        PlatinumCheckbox(isOn: configuration.$isOn, mixed: configuration.isMixed) { configuration.label }
    }
}

public extension ToggleStyle where Self == PlatinumCheckboxToggleStyle {
    static var platinumCheckbox: PlatinumCheckboxToggleStyle { PlatinumCheckboxToggleStyle() }
}

// MARK: - Radio buttons

/// One Platinum radio button.
public struct PlatinumRadioButton<Label: View>: View {
    let isOn: Bool
    let action: () -> Void
    let label: Label

    public init(isOn: Bool, action: @escaping () -> Void, @ViewBuilder label: () -> Label) {
        self.isOn = isOn
        self.action = action
        self.label = label()
    }

    public var body: some View {
        Button(action: action) { label }
            .buttonStyle(RadioButtonStyle(isOn: isOn))
            .accessibilityAddTraits(isOn ? [.isSelected] : [])
    }
}

/// A vertical group of radio buttons bound to a selection.
public struct PlatinumRadioGroup<Value: Hashable>: View {
    @Binding var selection: Value
    let options: [(value: Value, title: String, enabled: Bool)]
    let spacing: CGFloat

    public init(selection: Binding<Value>, options: [(Value, String)], spacing: CGFloat = 6) {
        _selection = selection
        self.options = options.map { ($0.0, $0.1, true) }
        self.spacing = spacing
    }

    public init(selection: Binding<Value>, options: [(Value, String, Bool)], spacing: CGFloat = 6) {
        _selection = selection
        self.options = options.map { ($0.0, $0.1, $0.2) }
        self.spacing = spacing
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: spacing) {
            ForEach(Array(options.enumerated()), id: \.offset) { _, option in
                PlatinumRadioButton(isOn: option.value == selection, action: { selection = option.value }) {
                    Text(option.title)
                }
                .disabled(!option.enabled)
            }
        }
    }
}
