import Carbon.HIToolbox
import Foundation

/// Logical buttons on the Apple / Bandai Pippin AppleJack controller, mapped
/// to their exact 18-bit active-high bit positions in QEMU (`adb-applejack`).
enum PippinButton: String, Codable, CaseIterable, Identifiable, Hashable {
    case dpadUp
    case dpadDown
    case dpadLeft
    case dpadRight
    case blue
    case yellow
    case green
    case red
    case square
    case circle
    case triangle
    case leftTrigger
    case rightTrigger

    var id: String { rawValue }

    /// Active-high bit matching `adb-applejack.c` (`AJ_BTN_*`, valid mask `0x3FF07`).
    var bitMask: UInt32 {
        switch self {
        case .square:       return 1 << 0
        case .circle:       return 1 << 1
        case .triangle:     return 1 << 2
        case .blue:         return 1 << 8
        case .yellow:       return 1 << 9
        case .dpadUp:       return 1 << 10
        case .dpadLeft:     return 1 << 11
        case .dpadRight:    return 1 << 12
        case .dpadDown:     return 1 << 13
        case .red:          return 1 << 14
        case .green:        return 1 << 15
        case .rightTrigger: return 1 << 16
        case .leftTrigger:  return 1 << 17
        }
    }

    var group: String {
        switch self {
        case .dpadUp, .dpadDown, .dpadLeft, .dpadRight:
            return "D-Pad"
        case .blue, .yellow, .green, .red:
            return "Action"
        case .square, .circle, .triangle:
            return "System"
        case .leftTrigger, .rightTrigger:
            return "Triggers"
        }
    }

    var label: String {
        switch self {
        case .dpadUp:       return "D-Pad Up"
        case .dpadDown:     return "D-Pad Down"
        case .dpadLeft:     return "D-Pad Left"
        case .dpadRight:    return "D-Pad Right"
        case .blue:         return "Blue (Top)"
        case .yellow:       return "Yellow (Left)"
        case .green:        return "Green (Right)"
        case .red:          return "Red (Bottom)"
        case .square:       return "Square (Left)"
        case .circle:       return "Circle (Center)"
        case .triangle:     return "Triangle (Right)"
        case .leftTrigger:  return "Left Trigger"
        case .rightTrigger: return "Right Trigger"
        }
    }

    /// Optional badge RGB color (0xRRGGBB) for the colored AppleJack face buttons.
    var accentRGB: UInt32? {
        switch self {
        case .blue:   return 0x1E6AE5
        case .yellow: return 0xE5B800
        case .green:  return 0x169C38
        case .red:    return 0xD92626
        default:      return nil
        }
    }
}

/// Physical controller inputs supported by Apple's `GameController` framework.
enum PippinGamepadInput: String, Codable, CaseIterable, Identifiable, Hashable {
    case dpadUp
    case dpadDown
    case dpadLeft
    case dpadRight
    case buttonY
    case buttonX
    case buttonB
    case buttonA
    case leftShoulder
    case rightShoulder
    case leftTrigger
    case rightTrigger
    case leftThumbstickButton
    case rightThumbstickButton
    case buttonOptions
    case buttonMenu
    case buttonHome

    var id: String { rawValue }

    var label: String {
        switch self {
        case .dpadUp:                return "D-Pad Up"
        case .dpadDown:              return "D-Pad Down"
        case .dpadLeft:              return "D-Pad Left"
        case .dpadRight:             return "D-Pad Right"
        case .buttonY:               return "Y / Triangle"
        case .buttonX:               return "X / Square"
        case .buttonB:               return "B / Circle"
        case .buttonA:               return "A / Cross"
        case .leftShoulder:          return "LB / L1"
        case .rightShoulder:         return "RB / R1"
        case .leftTrigger:           return "LT / L2"
        case .rightTrigger:          return "RT / R2"
        case .leftThumbstickButton:  return "L3 (Left Stick)"
        case .rightThumbstickButton: return "R3 (Right Stick)"
        case .buttonOptions:         return "View / Options"
        case .buttonMenu:            return "Menu / Start"
        case .buttonHome:            return "Home / Guide"
        }
    }
}

/// How physical controller thumbsticks drive the AppleJack trackball and D-pad.
enum PippinStickMode: String, Codable, CaseIterable, Identifiable, Hashable {
    case bothTrackball
    case leftTrackball
    case rightTrackball
    case leftDpadRightTrackball
    case disabled

    var id: String { rawValue }

    var label: String {
        switch self {
        case .bothTrackball:          return "Both Sticks → Trackball"
        case .leftTrackball:          return "Left Stick → Trackball"
        case .rightTrackball:         return "Right Stick → Trackball"
        case .leftDpadRightTrackball: return "Left Stick → D-Pad, Right → Trackball"
        case .disabled:               return "Sticks Disabled"
        }
    }
}

/// Per-VM keyboard-to-AppleJack and physical game controller configuration.
struct PippinControlConfig: Codable, Equatable, Hashable {
    var keyboardMappingEnabled: Bool
    /// Keyed by `PippinButton.rawValue` so JSON persists cleanly as an object.
    var keyboardBindings: [String: UInt16]
    var gamepadEnabled: Bool
    /// Keyed by `PippinButton.rawValue` so JSON persists cleanly as an object.
    var gamepadBindings: [String: PippinGamepadInput]
    var stickMode: PippinStickMode
    var stickSensitivity: Double
    var stickDeadzone: Double

    static let minSensitivity = 0.25
    static let maxSensitivity = 3.0
    static let minDeadzone = 0.05
    static let maxDeadzone = 0.50

    static let defaultKeyboardBindings: [String: UInt16] = [
        PippinButton.dpadUp.rawValue:       UInt16(kVK_UpArrow),
        PippinButton.dpadDown.rawValue:     UInt16(kVK_DownArrow),
        PippinButton.dpadLeft.rawValue:     UInt16(kVK_LeftArrow),
        PippinButton.dpadRight.rawValue:    UInt16(kVK_RightArrow),
        PippinButton.blue.rawValue:         UInt16(kVK_ANSI_W),
        PippinButton.yellow.rawValue:       UInt16(kVK_ANSI_A),
        PippinButton.green.rawValue:        UInt16(kVK_ANSI_D),
        PippinButton.red.rawValue:          UInt16(kVK_ANSI_S),
        PippinButton.square.rawValue:       UInt16(kVK_ANSI_Z),
        PippinButton.circle.rawValue:       UInt16(kVK_ANSI_X),
        PippinButton.triangle.rawValue:     UInt16(kVK_ANSI_C),
        PippinButton.leftTrigger.rawValue:  UInt16(kVK_ANSI_Q),
        PippinButton.rightTrigger.rawValue: UInt16(kVK_ANSI_E),
    ]

    static let defaultGamepadBindings: [String: PippinGamepadInput] = [
        PippinButton.dpadUp.rawValue:       .dpadUp,
        PippinButton.dpadDown.rawValue:     .dpadDown,
        PippinButton.dpadLeft.rawValue:     .dpadLeft,
        PippinButton.dpadRight.rawValue:    .dpadRight,
        PippinButton.blue.rawValue:         .buttonY,
        PippinButton.yellow.rawValue:       .buttonX,
        PippinButton.green.rawValue:        .buttonB,
        PippinButton.red.rawValue:          .buttonA,
        PippinButton.square.rawValue:       .buttonOptions,
        PippinButton.circle.rawValue:       .leftShoulder,
        PippinButton.triangle.rawValue:     .buttonMenu,
        PippinButton.leftTrigger.rawValue:  .leftTrigger,
        PippinButton.rightTrigger.rawValue: .rightTrigger,
    ]

    static let defaultConfig = PippinControlConfig(
        keyboardMappingEnabled: true,
        keyboardBindings: defaultKeyboardBindings,
        gamepadEnabled: true,
        gamepadBindings: defaultGamepadBindings,
        stickMode: .bothTrackball,
        stickSensitivity: 1.0,
        stickDeadzone: 0.15
    )

    init(
        keyboardMappingEnabled: Bool = true,
        keyboardBindings: [String: UInt16] = Self.defaultKeyboardBindings,
        gamepadEnabled: Bool = true,
        gamepadBindings: [String: PippinGamepadInput] = Self.defaultGamepadBindings,
        stickMode: PippinStickMode = .bothTrackball,
        stickSensitivity: Double = 1.0,
        stickDeadzone: Double = 0.15
    ) {
        self.keyboardMappingEnabled = keyboardMappingEnabled
        self.keyboardBindings = keyboardBindings
        self.gamepadEnabled = gamepadEnabled
        self.gamepadBindings = gamepadBindings
        self.stickMode = stickMode
        self.stickSensitivity = Self.clampedSensitivity(stickSensitivity)
        self.stickDeadzone = Self.clampedDeadzone(stickDeadzone)
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let def = Self.defaultConfig
        keyboardMappingEnabled = try c.decodeIfPresent(Bool.self, forKey: .keyboardMappingEnabled) ?? def.keyboardMappingEnabled
        keyboardBindings = try c.decodeIfPresent([String: UInt16].self, forKey: .keyboardBindings) ?? def.keyboardBindings
        gamepadEnabled = try c.decodeIfPresent(Bool.self, forKey: .gamepadEnabled) ?? def.gamepadEnabled
        gamepadBindings = try c.decodeIfPresent([String: PippinGamepadInput].self, forKey: .gamepadBindings) ?? def.gamepadBindings
        stickMode = (try? c.decodeIfPresent(PippinStickMode.self, forKey: .stickMode)) ?? def.stickMode
        stickSensitivity = Self.clampedSensitivity(try c.decodeIfPresent(Double.self, forKey: .stickSensitivity) ?? def.stickSensitivity)
        stickDeadzone = Self.clampedDeadzone(try c.decodeIfPresent(Double.self, forKey: .stickDeadzone) ?? def.stickDeadzone)
    }

    static func clampedSensitivity(_ value: Double) -> Double {
        guard value.isFinite else { return 1.0 }
        return min(max(value, minSensitivity), maxSensitivity)
    }

    static func clampedDeadzone(_ value: Double) -> Double {
        guard value.isFinite else { return 0.15 }
        return min(max(value, minDeadzone), maxDeadzone)
    }

    func keyCode(for button: PippinButton) -> UInt16? {
        keyboardBindings[button.rawValue]
    }

    func button(forKeyCode keyCode: UInt16) -> PippinButton? {
        for button in PippinButton.allCases where keyboardBindings[button.rawValue] == keyCode {
            return button
        }
        return nil
    }

    mutating func setKeyCode(_ keyCode: UInt16?, for button: PippinButton) {
        if let keyCode {
            for (k, v) in keyboardBindings where v == keyCode && k != button.rawValue {
                keyboardBindings.removeValue(forKey: k)
            }
            keyboardBindings[button.rawValue] = keyCode
        } else {
            keyboardBindings.removeValue(forKey: button.rawValue)
        }
    }

    func gamepadInput(for button: PippinButton) -> PippinGamepadInput? {
        gamepadBindings[button.rawValue]
    }

    mutating func setGamepadInput(_ input: PippinGamepadInput?, for button: PippinButton) {
        if let input {
            for (k, v) in gamepadBindings where v == input && k != button.rawValue {
                gamepadBindings.removeValue(forKey: k)
            }
            gamepadBindings[button.rawValue] = input
        } else {
            gamepadBindings.removeValue(forKey: button.rawValue)
        }
    }

    static func keyLabel(for keyCode: UInt16) -> String {
        switch Int(keyCode) {
        case kVK_ANSI_A: return "A"
        case kVK_ANSI_B: return "B"
        case kVK_ANSI_C: return "C"
        case kVK_ANSI_D: return "D"
        case kVK_ANSI_E: return "E"
        case kVK_ANSI_F: return "F"
        case kVK_ANSI_G: return "G"
        case kVK_ANSI_H: return "H"
        case kVK_ANSI_I: return "I"
        case kVK_ANSI_J: return "J"
        case kVK_ANSI_K: return "K"
        case kVK_ANSI_L: return "L"
        case kVK_ANSI_M: return "M"
        case kVK_ANSI_N: return "N"
        case kVK_ANSI_O: return "O"
        case kVK_ANSI_P: return "P"
        case kVK_ANSI_Q: return "Q"
        case kVK_ANSI_R: return "R"
        case kVK_ANSI_S: return "S"
        case kVK_ANSI_T: return "T"
        case kVK_ANSI_U: return "U"
        case kVK_ANSI_V: return "V"
        case kVK_ANSI_W: return "W"
        case kVK_ANSI_X: return "X"
        case kVK_ANSI_Y: return "Y"
        case kVK_ANSI_Z: return "Z"
        case kVK_ANSI_0: return "0"
        case kVK_ANSI_1: return "1"
        case kVK_ANSI_2: return "2"
        case kVK_ANSI_3: return "3"
        case kVK_ANSI_4: return "4"
        case kVK_ANSI_5: return "5"
        case kVK_ANSI_6: return "6"
        case kVK_ANSI_7: return "7"
        case kVK_ANSI_8: return "8"
        case kVK_ANSI_9: return "9"
        case kVK_ANSI_Minus: return "-"
        case kVK_ANSI_Equal: return "="
        case kVK_ANSI_LeftBracket: return "["
        case kVK_ANSI_RightBracket: return "]"
        case kVK_ANSI_Backslash: return "\\"
        case kVK_ANSI_Semicolon: return ";"
        case kVK_ANSI_Quote: return "'"
        case kVK_ANSI_Comma: return ","
        case kVK_ANSI_Period: return "."
        case kVK_ANSI_Slash: return "/"
        case kVK_ANSI_Grave: return "`"
        case kVK_Space: return "Space"
        case kVK_Return: return "Return"
        case kVK_Tab: return "Tab"
        case kVK_Delete: return "Delete"
        case kVK_ForwardDelete: return "Fwd Del"
        case kVK_Escape: return "Esc"
        case kVK_UpArrow: return "Up ↑"
        case kVK_DownArrow: return "Down ↓"
        case kVK_LeftArrow: return "Left ←"
        case kVK_RightArrow: return "Right →"
        case kVK_Home: return "Home"
        case kVK_End: return "End"
        case kVK_PageUp: return "Page Up"
        case kVK_PageDown: return "Page Dn"
        case kVK_ANSI_Keypad0: return "Num 0"
        case kVK_ANSI_Keypad1: return "Num 1"
        case kVK_ANSI_Keypad2: return "Num 2"
        case kVK_ANSI_Keypad3: return "Num 3"
        case kVK_ANSI_Keypad4: return "Num 4"
        case kVK_ANSI_Keypad5: return "Num 5"
        case kVK_ANSI_Keypad6: return "Num 6"
        case kVK_ANSI_Keypad7: return "Num 7"
        case kVK_ANSI_Keypad8: return "Num 8"
        case kVK_ANSI_Keypad9: return "Num 9"
        case kVK_ANSI_KeypadEnter: return "Num Enter"
        case kVK_ANSI_KeypadPlus: return "Num +"
        case kVK_ANSI_KeypadMinus: return "Num -"
        case kVK_ANSI_KeypadMultiply: return "Num *"
        case kVK_ANSI_KeypadDivide: return "Num /"
        case kVK_ANSI_KeypadDecimal: return "Num ."
        case kVK_ANSI_KeypadEquals: return "Num ="
        case kVK_F1: return "F1"
        case kVK_F2: return "F2"
        case kVK_F3: return "F3"
        case kVK_F4: return "F4"
        case kVK_F5: return "F5"
        case kVK_F6: return "F6"
        case kVK_F7: return "F7"
        case kVK_F8: return "F8"
        case kVK_F9: return "F9"
        case kVK_F10: return "F10"
        case kVK_F11: return "F11"
        case kVK_F12: return "F12"
        default: return String(format: "Key %d", keyCode)
        }
    }
}
