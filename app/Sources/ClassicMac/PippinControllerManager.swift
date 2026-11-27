import AppKit
import Combine
import Foundation
import GameController

/// Manages connected physical game controllers (`GCController`) and mapped
/// keyboard input for the active Pippin virtual machine, merging both sources
/// into one AppleJack button bitmask (`CMD_MSG_GAMEPAD`) and translating analog
/// thumbsticks into relative trackball deltas (`POINTER_REL`).
@MainActor
final class PippinControllerManager: ObservableObject {
    static let shared = PippinControllerManager()

    @Published private(set) var connectedControllerNames: [String] = []
    @Published private(set) var isCapturingGamepadButton = false

    private var activeVMID: UUID?
    private weak var activeSession: GuestDisplaySession?
    private var activeControls: PippinControlConfig = .defaultConfig
    private var isActivePaused = false

    private var keyboardMask: UInt32 = 0
    private var gamepadMask: UInt32 = 0
    private var lastSentMask: UInt32 = 0
    private var stickRemainderX: Double = 0
    private var stickRemainderY: Double = 0

    private var pollTimer: Timer?
    private var connectObserver: NSObjectProtocol?
    private var disconnectObserver: NSObjectProtocol?

    private var captureCallback: ((PippinGamepadInput) -> Void)?
    private var captureBaseline: Set<PippinGamepadInput> = []

    private init() {
        connectObserver = NotificationCenter.default.addObserver(
            forName: .GCControllerDidConnect, object: nil, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.controllersChanged() }
        }
        disconnectObserver = NotificationCenter.default.addObserver(
            forName: .GCControllerDidDisconnect, object: nil, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.controllersChanged() }
        }
        GCController.startWirelessControllerDiscovery {}
        controllersChanged()
    }

    // MARK: Session lifecycle

    func attach(
        vmID: UUID,
        session: GuestDisplaySession,
        controls: PippinControlConfig,
        isPaused: Bool
    ) {
        if activeSession !== session {
            keyboardMask = 0
            gamepadMask = 0
            lastSentMask = 0
            stickRemainderX = 0
            stickRemainderY = 0
        }
        activeVMID = vmID
        activeSession = session
        activeControls = controls
        isActivePaused = isPaused
        if isPaused {
            releaseAllActiveInputs()
        }
        updateTimerState()
    }

    func updateControls(_ controls: PippinControlConfig, for vmID: UUID) {
        guard activeVMID == vmID else { return }
        activeControls = controls
        if !controls.keyboardMappingEnabled && keyboardMask != 0 {
            keyboardMask = 0
        }
        if !controls.gamepadEnabled && gamepadMask != 0 {
            gamepadMask = 0
        }
        stickRemainderX = 0
        stickRemainderY = 0
        pushMergedButtonsIfNeeded()
        updateTimerState()
    }

    func setPaused(_ paused: Bool, for vmID: UUID) {
        guard activeVMID == vmID else { return }
        isActivePaused = paused
        if paused {
            releaseAllActiveInputs()
        }
        updateTimerState()
    }

    func detach(session: GuestDisplaySession) {
        guard activeSession === session else { return }
        releaseAllActiveInputs()
        activeSession = nil
        activeVMID = nil
        updateTimerState()
    }

    func setKeyboardButton(_ button: PippinButton, down: Bool, for session: GuestDisplaySession) {
        guard activeSession === session, !isActivePaused else { return }
        if down {
            keyboardMask |= button.bitMask
        } else {
            keyboardMask &= ~button.bitMask
        }
        pushMergedButtonsIfNeeded()
    }

    func clearKeyboardButtons(for session: GuestDisplaySession) {
        guard activeSession === session else { return }
        if keyboardMask != 0 {
            keyboardMask = 0
            pushMergedButtonsIfNeeded()
        }
    }

    // MARK: Interactive gamepad capture

    func beginGamepadCapture(onCapture: @escaping (PippinGamepadInput) -> Void) {
        cancelGamepadCapture()
        captureCallback = onCapture
        captureBaseline = currentlyPressedInputs()
        isCapturingGamepadButton = true
        updateTimerState()
    }

    func cancelGamepadCapture() {
        captureCallback = nil
        captureBaseline.removeAll()
        if isCapturingGamepadButton {
            isCapturingGamepadButton = false
        }
        updateTimerState()
    }

    // MARK: Controller discovery & polling

    private func controllersChanged() {
        let names = GCController.controllers().map { controller in
            let name = controller.vendorName?.trimmingCharacters(in: .whitespacesAndNewlines)
            return (name?.isEmpty == false) ? name! : "Game Controller"
        }
        connectedControllerNames = names
        if names.isEmpty && gamepadMask != 0 {
            gamepadMask = 0
            pushMergedButtonsIfNeeded()
        }
        updateTimerState()
    }

    private func updateTimerState() {
        let needsGameplayPolling = activeSession != nil && !isActivePaused &&
            activeControls.gamepadEnabled && !GCController.controllers().isEmpty
        let needsCapturePolling = isCapturingGamepadButton && !GCController.controllers().isEmpty
        if needsGameplayPolling || needsCapturePolling {
            if pollTimer == nil {
                let timer = Timer(timeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
                    MainActor.assumeIsolated { self?.pollControllers() }
                }
                RunLoop.main.add(timer, forMode: .common)
                pollTimer = timer
            }
        } else {
            pollTimer?.invalidate()
            pollTimer = nil
        }
    }

    private func pollControllers() {
        if isCapturingGamepadButton {
            pollCapture()
            return
        }
        guard let session = activeSession,
              !isActivePaused,
              activeControls.gamepadEnabled else {
            if gamepadMask != 0 {
                gamepadMask = 0
                pushMergedButtonsIfNeeded()
            }
            return
        }

        let controllers = GCController.controllers()
        guard !controllers.isEmpty else {
            if gamepadMask != 0 {
                gamepadMask = 0
                pushMergedButtonsIfNeeded()
            }
            return
        }

        var nextMask: UInt32 = 0
        var totalDeltaX = 0.0
        var totalDeltaY = 0.0

        for controller in controllers {
            guard let pad = controller.extendedGamepad else { continue }
            for button in PippinButton.allCases {
                if let mapped = activeControls.gamepadInput(for: button),
                   Self.isPressed(mapped, on: pad) {
                    nextMask |= button.bitMask
                }
            }

            let lx = Double(pad.leftThumbstick.xAxis.value)
            let ly = Double(pad.leftThumbstick.yAxis.value)
            let rx = Double(pad.rightThumbstick.xAxis.value)
            let ry = Double(pad.rightThumbstick.yAxis.value)

            switch activeControls.stickMode {
            case .bothTrackball:
                let l = Self.stickTrackballDelta(x: lx, y: ly, deadzone: activeControls.stickDeadzone, sensitivity: activeControls.stickSensitivity)
                let r = Self.stickTrackballDelta(x: rx, y: ry, deadzone: activeControls.stickDeadzone, sensitivity: activeControls.stickSensitivity)
                totalDeltaX += l.dx + r.dx
                totalDeltaY += l.dy + r.dy
            case .leftTrackball:
                let l = Self.stickTrackballDelta(x: lx, y: ly, deadzone: activeControls.stickDeadzone, sensitivity: activeControls.stickSensitivity)
                totalDeltaX += l.dx
                totalDeltaY += l.dy
            case .rightTrackball:
                let r = Self.stickTrackballDelta(x: rx, y: ry, deadzone: activeControls.stickDeadzone, sensitivity: activeControls.stickSensitivity)
                totalDeltaX += r.dx
                totalDeltaY += r.dy
            case .leftDpadRightTrackball:
                let dpadThreshold = max(0.35, activeControls.stickDeadzone)
                if lx < -dpadThreshold { nextMask |= PippinButton.dpadLeft.bitMask }
                if lx > dpadThreshold  { nextMask |= PippinButton.dpadRight.bitMask }
                if ly > dpadThreshold  { nextMask |= PippinButton.dpadUp.bitMask }
                if ly < -dpadThreshold { nextMask |= PippinButton.dpadDown.bitMask }
                let r = Self.stickTrackballDelta(x: rx, y: ry, deadzone: activeControls.stickDeadzone, sensitivity: activeControls.stickSensitivity)
                totalDeltaX += r.dx
                totalDeltaY += r.dy
            case .disabled:
                break
            }
        }

        if gamepadMask != nextMask {
            gamepadMask = nextMask
            pushMergedButtonsIfNeeded()
        }

        stickRemainderX += totalDeltaX
        stickRemainderY += totalDeltaY
        let emitX = Int(stickRemainderX.rounded(.towardZero))
        let emitY = Int(stickRemainderY.rounded(.towardZero))
        if emitX != 0 || emitY != 0 {
            stickRemainderX -= Double(emitX)
            stickRemainderY -= Double(emitY)
            session.pointerDelta(dx: emitX, dy: emitY)
        }
    }

    private func pollCapture() {
        let pressed = currentlyPressedInputs()
        captureBaseline.formIntersection(pressed)
        if let fresh = pressed.subtracting(captureBaseline).sorted(by: { $0.rawValue < $1.rawValue }).first {
            let cb = captureCallback
            cancelGamepadCapture()
            cb?(fresh)
        }
    }

    private func currentlyPressedInputs() -> Set<PippinGamepadInput> {
        var result: Set<PippinGamepadInput> = []
        for controller in GCController.controllers() {
            guard let pad = controller.extendedGamepad else { continue }
            for input in PippinGamepadInput.allCases where Self.isPressed(input, on: pad) {
                result.insert(input)
            }
        }
        return result
    }

    private func releaseAllActiveInputs() {
        keyboardMask = 0
        gamepadMask = 0
        stickRemainderX = 0
        stickRemainderY = 0
        pushMergedButtonsIfNeeded()
    }

    private func pushMergedButtonsIfNeeded() {
        let merged = Self.mergedMask(keyboardMask: keyboardMask, gamepadMask: gamepadMask)
        guard merged != lastSentMask else { return }
        lastSentMask = merged
        activeSession?.gamepad(buttons: merged)
    }

    // MARK: Pure testable helpers

    static func mergedMask(keyboardMask: UInt32, gamepadMask: UInt32) -> UInt32 {
        (keyboardMask | gamepadMask) & 0x3FF07
    }

    /// Converts normalized controller thumbstick axes (`x` right +, `y` up +)
    /// into screen-space trackball deltas (`dx` right +, `dy` down +) with
    /// radial deadzone and quadratic precision curve.
    static func stickTrackballDelta(
        x: Double,
        y: Double,
        deadzone: Double,
        sensitivity: Double
    ) -> (dx: Double, dy: Double) {
        let d = PippinControlConfig.clampedDeadzone(deadzone)
        let s = PippinControlConfig.clampedSensitivity(sensitivity)
        let length = hypot(x, y)
        guard length > d, length > 0 else { return (0, 0) }
        let clamped = min(length, 1.0)
        let normalized = (clamped - d) / max(1.0 - d, 0.001)
        let speed = normalized * normalized * s * 14.0
        let nx = x / length
        let ny = y / length
        return (nx * speed, -ny * speed)
    }

    private static func isPressed(_ input: PippinGamepadInput, on pad: GCExtendedGamepad) -> Bool {
        switch input {
        case .dpadUp:                return pad.dpad.up.isPressed
        case .dpadDown:              return pad.dpad.down.isPressed
        case .dpadLeft:              return pad.dpad.left.isPressed
        case .dpadRight:             return pad.dpad.right.isPressed
        case .buttonY:               return pad.buttonY.isPressed
        case .buttonX:               return pad.buttonX.isPressed
        case .buttonB:               return pad.buttonB.isPressed
        case .buttonA:               return pad.buttonA.isPressed
        case .leftShoulder:          return pad.leftShoulder.isPressed
        case .rightShoulder:         return pad.rightShoulder.isPressed
        case .leftTrigger:           return pad.leftTrigger.value > 0.25 || pad.leftTrigger.isPressed
        case .rightTrigger:          return pad.rightTrigger.value > 0.25 || pad.rightTrigger.isPressed
        case .leftThumbstickButton:  return pad.leftThumbstickButton?.isPressed ?? false
        case .rightThumbstickButton: return pad.rightThumbstickButton?.isPressed ?? false
        case .buttonOptions:         return pad.buttonOptions?.isPressed ?? false
        case .buttonMenu:            return pad.buttonMenu.isPressed
        case .buttonHome:            return pad.buttonHome?.isPressed ?? false
        }
    }
}
