import SwiftUI

// A stylized processor badge for each machine family: "68k" for the Quadra
// (Motorola 68040) and "G4" for the Power Mac (PowerPC G4). Drawn natively so
// it scales cleanly and sits naturally next to system controls in both light
// and dark appearances.
struct MachineBadgeView: View {
    let family: MachineFamily
    var size: CGFloat

    var body: some View {
        RoundedRectangle(cornerRadius: size * 0.24, style: .continuous)
            .fill(backgroundGradient)
            .frame(width: size, height: size)
            .overlay(
                RoundedRectangle(cornerRadius: size * 0.24, style: .continuous)
                    .strokeBorder(.white.opacity(0.25), lineWidth: 1)
                    .blendMode(.plusLighter)
            )
            .overlay(
                Text(label)
                    .font(.system(size: size * 0.38, weight: .bold, design: .rounded))
                    .foregroundStyle(textColor)
                    .minimumScaleFactor(0.5)
            )
            .shadow(color: .black.opacity(0.18), radius: size * 0.03, y: size * 0.02)
            .accessibilityLabel(family.label)
    }

    private var label: String {
        switch family {
        case .quadra800: return "68k"
        case .powerMacG4: return "G4"
        }
    }

    // Quadra: the warm platinum beige of early-90s Apple plastic.
    // Power Mac: graphite, like the G4 tower.
    private var backgroundGradient: LinearGradient {
        switch family {
        case .quadra800:
            return LinearGradient(
                colors: [
                    Color(red: 0.94, green: 0.91, blue: 0.83),
                    Color(red: 0.80, green: 0.75, blue: 0.64)
                ],
                startPoint: .top,
                endPoint: .bottom
            )
        case .powerMacG4:
            return LinearGradient(
                colors: [
                    Color(red: 0.64, green: 0.66, blue: 0.71),
                    Color(red: 0.33, green: 0.35, blue: 0.40)
                ],
                startPoint: .top,
                endPoint: .bottom
            )
        }
    }

    private var textColor: Color {
        switch family {
        case .quadra800:
            return Color(red: 0.38, green: 0.33, blue: 0.24)
        case .powerMacG4:
            return .white
        }
    }
}
