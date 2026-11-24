import SwiftUI

// MARK: - Text styles

public enum PlatinumTextStyle {
    /// System 12: emphasis, labels, headings.
    case emphasis
    /// Regular 12: body text.
    case body
    /// Small 10: secondary text.
    case small
    /// Small 10 in gray: details.
    case detail
    /// Gray disabled text in the system font.
    case disabled
    /// System 12 at 2x, for a dialog's one large heading.
    case large
}

public extension View {
    func platinumText(_ style: PlatinumTextStyle) -> some View {
        modifier(PlatinumTextModifier(style: style))
    }
}

private struct PlatinumTextModifier: ViewModifier {
    let style: PlatinumTextStyle
    @Environment(\.isEnabled) private var isEnabled

    func body(content: Content) -> some View {
        switch style {
        case .emphasis:
            content.font(.platinumSystem).foregroundStyle(ink(Platinum.black))
        case .body:
            content.font(.platinumBody).foregroundStyle(ink(Platinum.black))
        case .small:
            content.font(.platinumSmall).foregroundStyle(ink(Platinum.black))
        case .detail:
            content.font(.platinumSmall).foregroundStyle(ink(Platinum.dimText))
        case .disabled:
            content.font(.platinumSystem).foregroundStyle(Platinum.disabledText.color)
        case .large:
            content.font(.platinumLarge).foregroundStyle(ink(Platinum.black))
        }
    }

    private func ink(_ color: RGB) -> Color {
        isEnabled ? color.color : Platinum.disabledText.color
    }
}

// MARK: - Group box

/// An etched Platinum group box with an optional System 12 title.
public struct PlatinumGroupBox<Content: View>: View {
    let title: String?
    let content: Content
    @Environment(\.isEnabled) private var isEnabled

    public init(_ title: String? = nil, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    public var body: some View {
        let labelWidth = title.map { Int(PlatinumFonts.width($0, font: .system)) } ?? 0
        content
            .padding(.horizontal, 12)
            .padding(.top, title == nil ? 12 : 22)
            .padding(.bottom, 12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background {
                PixelCanvas { p, r in Chrome.groupBox(p, r, labelWidth: labelWidth) }
            }
            .overlay(alignment: .topLeading) {
                if let title {
                    Text(title)
                        .font(.platinumSystem)
                        .foregroundStyle(isEnabled ? Platinum.black.color : Platinum.disabledText.color)
                        .fixedSize()
                        // Baseline 11 below the box top: the frame runs through the capitals.
                        .offset(x: 11, y: -1)
                }
            }
    }
}

// MARK: - Separator

public struct PlatinumSeparator: View {
    let vertical: Bool

    public init(vertical: Bool = false) { self.vertical = vertical }

    public var body: some View {
        if vertical {
            PixelCanvas { p, r in Chrome.verticalSeparator(p, x: 0, y: 0, h: r.h - 1) }
                .frame(width: 2)
                .frame(maxHeight: .infinity)
        } else {
            PixelCanvas { p, r in Chrome.separator(p, x: 0, y: 0, w: r.w - 1) }
                .frame(height: 2)
                .frame(maxWidth: .infinity)
        }
    }
}

// MARK: - Progress bar

/// A Platinum progress bar: a lavender fill, or the moving barber pole when
/// the amount of work is unknown.
public struct PlatinumProgressBar: View {
    let value: Double?
    @Environment(\.isEnabled) private var isEnabled

    /// - Parameter value: 0...1, or nil for indeterminate.
    public init(value: Double?) { self.value = value }

    public var body: some View {
        Group {
            if value == nil {
                TimelineView(.animation(minimumInterval: 1.0 / 20.0)) { context in
                    let phase = Int(context.date.timeIntervalSinceReferenceDate * 20) % 16
                    bar(phase: phase)
                }
            } else {
                bar(phase: 0)
            }
        }
        .frame(height: Platinum.progressHeight + 2)
        .frame(maxWidth: .infinity)
        .accessibilityElement()
        .accessibilityAddTraits(.updatesFrequently)
        .accessibilityValue(value.map { "\(Int($0 * 100)) percent" } ?? "in progress")
    }

    private func bar(phase: Int) -> some View {
        PixelCanvas { p, r in
            Chrome.progress(p, r.inset(1), value: value, phase: phase, disabled: !isEnabled)
        }
    }
}

// MARK: - Sunken well

/// White (or custom) content inside a sunken black frame, the way Platinum
/// draws list boxes and picture frames.
public struct PlatinumWell<Content: View>: View {
    let fill: RGB?
    let preserveFillWhenDisabled: Bool
    let content: Content
    @Environment(\.isEnabled) private var isEnabled

    public init(fill: RGB? = Platinum.white, preserveFillWhenDisabled: Bool = false,
                @ViewBuilder content: () -> Content) {
        self.fill = fill
        self.preserveFillWhenDisabled = preserveFillWhenDisabled
        self.content = content()
    }

    public var body: some View {
        content
            .padding(2)
            .background {
                PixelCanvas { p, r in
                    Chrome.sunken(p, r.inset(1), fill: fill.map { (isEnabled || preserveFillWhenDisabled) ? $0 : Platinum.grayF7 })
                }
            }
    }
}
