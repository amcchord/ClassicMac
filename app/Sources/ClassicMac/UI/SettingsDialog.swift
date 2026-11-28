import AppKit
import SwiftUI
import PlatinumKit

/// A Mac's settings, in the shape of the Finder's Get Info window: a "Show:"
/// pop-up for General, Display and Sharing, then Cancel and OK. Edits apply
/// when you click OK.
struct SettingsDialog: View {
    static let size = NSSize(width: 520, height: 475)

    enum Pane: Hashable { case general, display, controls, sharing, printing }

    let original: VMConfig
    let handle: DialogHandle
    @State private var config: VMConfig
    @State private var pane: Pane
    @State private var capturingKeyButton: PippinButton?
    @State private var capturingGamepadButton: PippinButton?
    @State private var keyMonitor: Any?
    @ObservedObject private var controllerManager = PippinControllerManager.shared
    @State private var widthText: String
    @State private var heightText: String
    @State private var mouseDPIText: String
    @State private var errorMessage: String?
    @State private var printers: [HostPrinter] = []
    @State private var defaultPrinterQueue: String?
    @State private var printingStatusMessage: String?
    @State private var printingStatusIsError = false
    @State private var isPrintingTestPage = false
    @State private var romRefreshToken = 0
    @ObservedObject private var manager = QEMUManager.shared

    init(original: VMConfig, handle: DialogHandle, initialPane: Pane? = nil) {
        self.original = original
        self.handle = handle
        _config = State(initialValue: original)
        let resolvedPane: Pane = initialPane ?? {
            switch ProcessInfo.processInfo.environment["CLASSICMAC_UI_SETTINGS_PANE"] {
            case "display": return .display
            case "controls": return original.machineFamily == .pippin ? .controls : .general
            case "sharing": return .sharing
            case "printing": return .printing
            default: return .general
            }
        }()
        _pane = State(initialValue: resolvedPane)
        _widthText = State(initialValue: String(original.width))
        _heightText = State(initialValue: String(original.height))
        _mouseDPIText = State(initialValue: original.mouseDPI.map(String.init) ?? "")
    }

    private var running: Bool { manager.isRunning(original.id) }
    private var copland: Bool { config.machineFamily == .powerMac7500 }
    private var pippin: Bool { config.machineFamily == .pippin }
    private var canEditCurrentPane: Bool { !running || (pippin && pane == .controls) }

    private var paneOptions: [(Pane, String)] {
        if pippin {
            return [
                (Pane.general, "General Information"),
                (Pane.display, "Display"),
                (Pane.controls, "Controls"),
            ]
        }
        return [
            (Pane.general, "General Information"),
            (Pane.display, "Display"),
            (Pane.sharing, "Sharing"),
            (Pane.printing, "Printing"),
        ]
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 12) {
                PlatinumIcon(config.machineFamily.iconName, size: 32)
                VStack(alignment: .leading, spacing: 2) {
                    Text(config.name).platinumText(.emphasis).lineLimit(1)
                    Text(config.machineFamily.hardwareLabel).platinumText(.detail)
                }
                Spacer()
            }
            HStack(spacing: 8) {
                Text("Show:").platinumText(.emphasis)
                PlatinumPopUp(selection: $pane, options: paneOptions, width: 190)
                Spacer()
            }
            PlatinumSeparator()
            Group {
                switch pane {
                case .general: generalPane
                case .display: displayPane
                case .controls: controlsPane
                case .sharing: sharingPane
                case .printing: printingPane
                }
            }
            .disabled(!canEditCurrentPane)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            if !canEditCurrentPane {
                HStack(spacing: 8) {
                    PlatinumIcon("lock", size: 16)
                    Text("Shut down this Mac to change its settings.").platinumText(.small)
                }
            } else if let errorMessage {
                HStack(alignment: .top, spacing: 8) {
                    PlatinumIcon("stop", size: 16)
                    Text(errorMessage).platinumText(.small)
                        .foregroundStyle(Platinum.errorText.color)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            HStack(spacing: Platinum.buttonGap) {
                Spacer()
                if !canEditCurrentPane {
                    PlatinumButton("OK", role: .default) { handle.close() }
                } else {
                    PlatinumButton("Cancel", role: .cancel) {
                        stopAllCaptures()
                        handle.close()
                    }
                    PlatinumButton("OK", role: .default, action: save)
                }
            }
        }
        .padding(12)
        .frame(width: Self.size.width, height: Self.size.height)
        .background(Platinum.face.color)
        .onAppear {
            coerceDepth()
            refreshPrinters()
        }
        .onDisappear { stopAllCaptures() }
        .onChange(of: pane) { _, _ in stopAllCaptures() }
        .onChange(of: config.name) { _, _ in errorMessage = nil }
    }

    private func save() {
        stopAllCaptures()
        if running && pippin {
            var updated = VMStore.shared.vms.first(where: { $0.id == original.id }) ?? original
            updated.pippinControls = config.pippinControls
            if updated != original {
                if !VMStore.shared.save(updated) { return }
            }
            PippinControllerManager.shared.updateControls(updated.effectivePippinControls, for: updated.id)
            handle.close()
            return
        }
        do {
            config.name = try MachineTemplateInstaller.normalizedName(config.name)
            errorMessage = nil
        } catch {
            errorMessage = error.localizedDescription
            NSSound.beep()
            return
        }
        if config.customResolution && config.graphicsCard == .standard {
            config.width = VMConfig.clampedWidth(Int(widthText) ?? config.width)
            config.height = VMConfig.clampedHeight(Int(heightText) ?? config.height)
        }
        if !copland && !pippin {
            let trimmedDPI = mouseDPIText.trimmingCharacters(in: .whitespacesAndNewlines)
            if trimmedDPI.isEmpty || trimmedDPI.caseInsensitiveCompare("auto") == .orderedSame {
                config.mouseDPI = nil
            } else if let dpiValue = Int(trimmedDPI) {
                config.mouseDPI = VMConfig.clampedMouseDPI(dpiValue)
            } else {
                errorMessage = "Mouse DPI must be Auto or a number between 100 and 100000."
                NSSound.beep()
                return
            }
        }
        coerceDepth()
        if config != original {
            if !VMStore.shared.save(config) { return }
        }
        handle.close()
    }

    // MARK: General

    private var generalPane: some View {
        VStack(alignment: .leading, spacing: 9) {
            HStack(spacing: 10) {
                Text("Name:").platinumText(.emphasis).frame(width: 82, alignment: .trailing)
                PlatinumTextField(text: $config.name, width: 280)
            }
            HStack(spacing: 10) {
                Text("Memory:").platinumText(.emphasis).frame(width: 82, alignment: .trailing)
                if copland {
                    Text("32 MB, the tested Copland configuration").platinumText(.body)
                } else {
                    PlatinumPopUp(selection: $config.ramMB,
                                  options: memoryChoices.map { ($0, "\($0) MB") }, width: 110)
                    if pippin {
                        Text("Save Flash:").platinumText(.emphasis).padding(.leading, 12)
                        Text("128 KB").platinumText(.body)
                    } else {
                        Text("Hard disk:").platinumText(.emphasis).padding(.leading, 12)
                        Text("\(config.diskSizeGB) GB").platinumText(.body)
                    }
                }
            }
            if pippin {
                VStack(alignment: .leading, spacing: 9) {
                    PlatinumCheckbox("Play the Pippin's sound", isOn: $config.sound)
                        .padding(.leading, 92)
                    HStack(spacing: 6) {
                        let _ = romRefreshToken
                        Text("Boot ROM:").platinumText(.emphasis).frame(width: 82, alignment: .trailing)
                        let resolved = PippinMachine.resolveROMURL(for: config)
                        Text(resolved?.lastPathComponent ?? "Not configured (pippin.rom)")
                            .platinumText(resolved == nil ? .detail : .body)
                            .lineLimit(1).truncationMode(.middle)
                            .frame(maxWidth: 146, alignment: .leading)
                        PlatinumButton("Choose…", minWidth: 70, action: choosePippinROM)
                        PlatinumButton("Download…", minWidth: 82) {
                            WindowManager.shared.presentPippinROMDownload(machineName: config.name, parent: handle.window) { _ in
                                errorMessage = nil
                                config.pippinROMPath = nil
                                romRefreshToken += 1
                            }
                        }
                        if config.pippinROMPath?.isEmpty == false {
                            PlatinumButton("Clear", minWidth: 52) {
                                config.pippinROMPath = nil
                                romRefreshToken += 1
                            }
                        }
                    }
                    if let romURL = PippinMachine.resolveROMURL(for: config),
                       let label = try? PippinMachine.validateROM(at: romURL) {
                        Text(label).platinumText(.detail).padding(.leading, 92)
                            .id(romRefreshToken)
                    }
                }
                .padding(.top, 4)
            } else if !copland && !pippin {
                VStack(alignment: .leading, spacing: 7) {
                    PlatinumCheckbox("Use this computer's Internet connection", isOn: $config.networking)
                    PlatinumCheckbox("Play the Mac's sound", isOn: $config.sound)
                    if config.machineFamily == .powerMacG4 {
                        PlatinumCheckbox("PowerPC G4 processor (off for Mac OS 8.5–8.6)", isOn: $config.useG4CPU)
                    }
                    if config.machineFamily != .powerMacOSX {
                        PlatinumCheckbox("Right-click and scroll wheel in classic apps", isOn: $config.classicInputHelpers)
                    }
                    PlatinumCheckbox("Seamless mouse: move in and out freely", isOn: $config.tabletInput)
                    PlatinumCheckbox("Native classic mouse tracking", isOn: $config.legacyMouseTracking)
                    HStack(spacing: 8) {
                        Text("Mouse DPI:").platinumText(.small)
                        PlatinumTextField("Auto", text: $mouseDPIText, width: 76, alignment: .trailing)
                        Text("Auto (1000) or 100–100000").platinumText(.detail)
                    }
                    .disabled(!config.legacyMouseTracking)
                    if config.machineFamily == .powerMacOSX {
                        Text("Turn off the seamless mouse to use a relative USB mouse.")
                            .platinumText(.detail)
                    } else {
                        Text("Turn off right-click and scrolling when USB Overdrive is installed in Mac OS 8.5–9.2.2 (68k / System 7 / Mac OS 8.1 use Control-click). Games can still capture the mouse.")
                            .platinumText(.detail)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                .padding(.leading, 92)
                .padding(.top, 4)
            }
        }
    }

    private var pippinVideoBinding: Binding<PippinVideoOut> {
        Binding(
            get: { config.effectivePippinVideoOut },
            set: { mode in
                config.pippinVideoOut = mode
                let dims = mode.dimensions
                config.width = dims.width
                config.height = dims.height
            }
        )
    }

    private func choosePippinROM() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.message = "Choose a 4 MB Apple / Bandai Pippin Kinka boot ROM"
        panel.prompt = "Choose"
        if panel.runModal() == .OK, let url = panel.url {
            do {
                _ = try PippinMachine.validateROM(at: url)
                errorMessage = nil
                config.pippinROMPath = url.path
            } catch {
                errorMessage = error.localizedDescription
                NSSound.beep()
            }
        }
    }

    private var memoryChoices: [Int] {
        var choices = config.machineFamily.ramPresets
        if !choices.contains(config.ramMB) { choices.append(config.ramMB); choices.sort() }
        return choices
    }

    // MARK: Display

    @ViewBuilder
    private var displayPane: some View {
        if copland {
            Text("Copland runs at 640 × 480 with 256 colors in its own window. Use Control-+ and Control-− to scale it.")
                .platinumText(.body)
                .fixedSize(horizontal: false, vertical: true)
        } else if pippin {
            Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 9) {
                GridRow {
                    Text("Video output:").platinumText(.emphasis).gridColumnAlignment(.trailing)
                    PlatinumPopUp(selection: pippinVideoBinding,
                                  options: PippinVideoOut.allCases.map { ($0, $0.label) },
                                  width: 210)
                }
                GridRow {
                    Color.clear.frame(width: 1, height: 1)
                    PlatinumCheckbox("Show the Pippin in a web browser", isOn: $config.useBrowserDisplay)
                }
            }
        } else {
            Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 9) {
                if config.machineFamily == .powerMacG4 {
                    GridRow(alignment: .top) {
                        Text("Graphics card:").platinumText(.emphasis).gridColumnAlignment(.trailing)
                        VStack(alignment: .leading, spacing: 4) {
                            PlatinumRadioGroup(selection: graphicsCard, options: [
                                (GraphicsCard.standard, "Standard"),
                                (GraphicsCard.atiRadeon, "ATI Radeon (3D acceleration)"),
                            ])
                            Text(cardExplanation).platinumText(.detail)
                                .fixedSize(horizontal: false, vertical: true)
                                .frame(width: 300, alignment: .leading)
                        }
                    }
                }
                GridRow {
                    Text("Screen size:").platinumText(.emphasis).gridColumnAlignment(.trailing)
                    sizeControl
                }
                if config.graphicsCard == .standard && config.customResolution {
                    GridRow {
                        Color.clear.frame(width: 1, height: 1)
                        HStack(spacing: 6) {
                            PlatinumTextField(text: $widthText, width: 60, alignment: .trailing)
                            Text("×").platinumText(.body)
                            PlatinumTextField(text: $heightText, width: 60, alignment: .trailing)
                            PlatinumButton("Match This Display") {
                                guard let screen = NSScreen.main else { return }
                                widthText = String(VMConfig.clampedWidth(Int(screen.frame.width)))
                                heightText = String(VMConfig.clampedHeight(Int(screen.frame.height)))
                            }
                        }
                    }
                }
                if config.graphicsCard == .standard && config.machineFamily != .powerMacOSX {
                    GridRow {
                        Text("Colors:").platinumText(.emphasis)
                        PlatinumPopUp(selection: $config.depth, options: depthChoices.map { ($0.rawValue, $0.label) },
                                      width: 130)
                    }
                }
                if config.machineFamily == .quadra800 {
                    GridRow {
                        Color.clear.frame(width: 1, height: 1)
                        PlatinumCheckbox("Enhanced Quadra video (arbitrary resolutions)", isOn: enhancedFramebuffer)
                    }
                }
                GridRow {
                    Color.clear.frame(width: 1, height: 1)
                    PlatinumCheckbox("Show the Mac in a web browser", isOn: $config.useBrowserDisplay)
                }
            }
        }
    }

    private var cardExplanation: String {
        switch config.graphicsCard {
        case .standard:
            return "Any screen size, following the window as you resize it, with Thousands or Millions of colors."
        case .atiRadeon:
            return "An emulated Radeon that uses Mac OS 9's own ATI drivers, so 3D games are drawn by this computer's GPU. Needs ATI's software in Mac OS, as in the Library's Deluxe Macs; without it the Mac starts at 640 × 480. Resizing the window changes the Mac’s resolution, up to 1920 × 1440. CD starts use the Standard card."
        }
    }

    private var graphicsCard: Binding<GraphicsCard> {
        Binding(
            get: { config.graphicsCard },
            set: { card in
                config.graphicsCard = card
                if card == .atiRadeon {
                    let size = ATIDisplay.size(fitting: config.width, height: config.height)
                    config.width = size.width
                    config.height = size.height
                    config.customResolution = false
                }
                coerceDepth()
            }
        )
    }

    private var enhancedFramebuffer: Binding<Bool> {
        Binding(
            get: { config.useEnhancedFramebuffer },
            set: { enabled in
                config.useEnhancedFramebuffer = enabled
                if !enabled {
                    config.customResolution = false
                    let standardSizes = Self.builtInQuadraSizes
                    let closest = standardSizes.min { lhs, rhs in
                        abs(lhs.width - config.width) + abs(lhs.height - config.height) <
                        abs(rhs.width - config.width) + abs(rhs.height - config.height)
                    } ?? standardSizes[0]
                    config.width = closest.width
                    config.height = closest.height
                    widthText = String(closest.width)
                    heightText = String(closest.height)
                }
                coerceDepth()
            }
        )
    }

    private enum SizeChoice: Hashable { case preset(ResolutionPreset), custom }

    private static let builtInQuadraSizes: [ResolutionPreset] = [
        ResolutionPreset(width: 640, height: 480),
        ResolutionPreset(width: 800, height: 600),
        ResolutionPreset(width: 1152, height: 870),
    ]

    private var sizeOptions: [(SizeChoice, String)] {
        if config.graphicsCard == .atiRadeon {
            var presets = ATIDisplay.fixedSizes
            if let screen = NSScreen.main,
               let exact = ATIDisplay.exactSize(width: Int(screen.frame.width), height: Int(screen.frame.height)),
               !presets.contains(where: { $0.width == exact.width && $0.height == exact.height }) {
                presets.append(ResolutionPreset(width: exact.width, height: exact.height))
            }
            let current = ResolutionPreset(width: config.width, height: config.height)
            if !presets.contains(current) { presets.append(current) }
            return presets.sorted { ($0.width, $0.height) < ($1.width, $1.height) }.map { (.preset($0), $0.label) }
        }
        if config.machineFamily == .quadra800 && !config.useEnhancedFramebuffer {
            var presets = Self.builtInQuadraSizes
            let current = ResolutionPreset(width: config.width, height: config.height)
            if !presets.contains(current) {
                presets.append(current)
                presets.sort { ($0.width, $0.height) < ($1.width, $1.height) }
            }
            return presets.map { (.preset($0), $0.label) }
        }
        var presets = ResolutionPreset.all
        if !config.customResolution {
            let current = ResolutionPreset(width: config.width, height: config.height)
            if !presets.contains(current) {
                presets.append(current)
                presets.sort { ($0.width, $0.height) < ($1.width, $1.height) }
            }
        }
        return presets.map { (SizeChoice.preset($0), $0.label) } + [(.custom, "Custom…")]
    }

    private var sizeControl: some View {
        let selection = Binding<SizeChoice>(
            get: {
                if config.graphicsCard == .standard && config.customResolution &&
                    !(config.machineFamily == .quadra800 && !config.useEnhancedFramebuffer) {
                    return .custom
                }
                return .preset(ResolutionPreset(width: config.width, height: config.height))
            },
            set: { choice in
                switch choice {
                case .preset(let preset):
                    config.customResolution = false
                    config.width = preset.width
                    config.height = preset.height
                    widthText = String(preset.width)
                    heightText = String(preset.height)
                case .custom:
                    config.customResolution = true
                }
                coerceDepth()
            }
        )
        return PlatinumPopUp(selection: selection, options: sizeOptions, width: 150)
    }

    private var depthChoices: [ColorDepth] {
        if config.machineFamily == .powerMacG4 { return [.thousands, .millions] }
        if config.useEnhancedFramebuffer || config.width < 1152 { return ColorDepth.allCases }
        return [.greys256]
    }

    private func coerceDepth() {
        let allowed = depthChoices
        guard let fallback = allowed.first else { return }
        if !allowed.contains(where: { $0.rawValue == config.depth }) {
            config.depth = fallback.rawValue
        }
    }

    // MARK: Controls (Pippin)

    private var controlsBinding: Binding<PippinControlConfig> {
        Binding(
            get: { config.effectivePippinControls },
            set: { config.pippinControls = $0 }
        )
    }

    private static let sensitivityOptions: [(Double, String)] = [
        (0.5,  "0.5× (Slow)"),
        (0.75, "0.75×"),
        (1.0,  "1.0× (Default)"),
        (1.5,  "1.5×"),
        (2.0,  "2.0× (Fast)"),
        (2.5,  "2.5×"),
    ]

    private var stickSensitivityBinding: Binding<Double> {
        Binding(
            get: {
                let current = config.effectivePippinControls.stickSensitivity
                return Self.sensitivityOptions.min(by: { abs($0.0 - current) < abs($1.0 - current) })?.0 ?? 1.0
            },
            set: { value in
                var updated = config.effectivePippinControls
                updated.stickSensitivity = value
                config.pippinControls = updated
            }
        )
    }

    @ViewBuilder
    private var controlsPane: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 12) {
                PlatinumCheckbox("Map keyboard keys", isOn: controlsBinding.keyboardMappingEnabled)
                PlatinumCheckbox("Enable game controller", isOn: controlsBinding.gamepadEnabled)
            }
            HStack(spacing: 8) {
                Text("Controller:").platinumText(.small)
                Text(controllerManager.connectedControllerNames.isEmpty
                     ? "No game controller connected (pair via Bluetooth or USB)"
                     : controllerManager.connectedControllerNames.joined(separator: ", "))
                    .platinumText(.detail)
                    .lineLimit(1)
                    .truncationMode(.tail)
            }
            HStack(spacing: 8) {
                Text("Sticks:").platinumText(.small)
                PlatinumPopUp(selection: controlsBinding.stickMode,
                              options: PippinStickMode.allCases.map { ($0, $0.label) },
                              width: 230)
                Text("Speed:").platinumText(.small)
                PlatinumPopUp(selection: stickSensitivityBinding,
                              options: Self.sensitivityOptions,
                              width: 118)
            }

            HStack(spacing: 8) {
                Text("AppleJack Button").platinumText(.small).frame(width: 128, alignment: .leading)
                Text("Keyboard Key").platinumText(.small).frame(width: 126, alignment: .leading)
                Text("Game Controller").platinumText(.small)
                Spacer()
            }
            .foregroundStyle(Platinum.dimText.color)
            .padding(.top, 2)

            ScrollView {
                VStack(alignment: .leading, spacing: 5) {
                    ForEach(PippinButton.allCases) { button in
                        controlMappingRow(for: button)
                    }
                }
                .padding(.vertical, 2)
            }
            .frame(maxHeight: .infinity)
            .padding(6)
            .background(Color.white)
            .overlay(Rectangle().stroke(Platinum.gray94.color, lineWidth: 1))

            HStack {
                Text("Mapped keys send AppleJack controller buttons; unmapped keys type normally.")
                    .platinumText(.detail)
                    .lineLimit(1)
                Spacer()
                PlatinumButton("Reset Defaults") {
                    stopAllCaptures()
                    config.pippinControls = .defaultConfig
                }
            }
        }
    }

    private func controlMappingRow(for button: PippinButton) -> some View {
        let controls = config.effectivePippinControls
        let isCapturingKey = capturingKeyButton == button
        let isCapturingGamepad = capturingGamepadButton == button
        let currentKeyLabel = controls.keyCode(for: button).map(PippinControlConfig.keyLabel(for:)) ?? "None"
        let gamepadBinding = Binding<PippinGamepadInput?>(
            get: { config.effectivePippinControls.gamepadInput(for: button) },
            set: { next in
                var updated = config.effectivePippinControls
                updated.setGamepadInput(next, for: button)
                config.pippinControls = updated
            }
        )
        let gamepadOptions: [(PippinGamepadInput?, String)] =
            [(nil, "None")] + PippinGamepadInput.allCases.map { ($0, $0.label) }

        return HStack(spacing: 6) {
            HStack(spacing: 5) {
                if let rgb = button.accentRGB {
                    Circle()
                        .fill(Color(red: Double((rgb >> 16) & 0xFF) / 255.0,
                                    green: Double((rgb >> 8) & 0xFF) / 255.0,
                                    blue: Double(rgb & 0xFF) / 255.0))
                        .frame(width: 8, height: 8)
                        .overlay(Circle().stroke(Color.black.opacity(0.45), lineWidth: 0.75))
                } else {
                    Color.clear.frame(width: 8, height: 8)
                }
                Text(button.label).platinumText(.small).lineLimit(1)
            }
            .frame(width: 124, alignment: .leading)

            HStack(spacing: 4) {
                PlatinumButton(isCapturingKey ? "Press key…" : currentKeyLabel, minWidth: 94) {
                    if isCapturingKey {
                        stopKeyCapture()
                    } else {
                        startKeyCapture(for: button)
                    }
                }
                .frame(width: 94, alignment: .leading)
                if controls.keyCode(for: button) != nil {
                    PlatinumButton("×", minWidth: 24) {
                        var updated = config.effectivePippinControls
                        updated.setKeyCode(nil, for: button)
                        config.pippinControls = updated
                    }
                }
            }
            .frame(width: 126, alignment: .leading)

            PlatinumPopUp(selection: gamepadBinding, options: gamepadOptions, width: 150)

            if !controllerManager.connectedControllerNames.isEmpty {
                PlatinumButton(isCapturingGamepad ? "Press…" : "Set", minWidth: 44) {
                    if isCapturingGamepad {
                        stopGamepadCapture()
                    } else {
                        startGamepadCapture(for: button)
                    }
                }
            }
            Spacer(minLength: 0)
        }
    }

    private func startKeyCapture(for button: PippinButton) {
        stopAllCaptures()
        capturingKeyButton = button
        keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            if event.keyCode == 53 { // Esc cancels without binding
                stopKeyCapture()
                return nil
            }
            var updated = config.effectivePippinControls
            updated.setKeyCode(event.keyCode, for: button)
            config.pippinControls = updated
            stopKeyCapture()
            return nil
        }
    }

    private func stopKeyCapture() {
        capturingKeyButton = nil
        if let monitor = keyMonitor {
            NSEvent.removeMonitor(monitor)
            keyMonitor = nil
        }
    }

    private func startGamepadCapture(for button: PippinButton) {
        stopAllCaptures()
        capturingGamepadButton = button
        controllerManager.beginGamepadCapture { capturedInput in
            var updated = config.effectivePippinControls
            updated.setGamepadInput(capturedInput, for: button)
            config.pippinControls = updated
            capturingGamepadButton = nil
        }
    }

    private func stopGamepadCapture() {
        capturingGamepadButton = nil
        controllerManager.cancelGamepadCapture()
    }

    private func stopAllCaptures() {
        stopKeyCapture()
        stopGamepadCapture()
    }

    // MARK: Sharing

    @ViewBuilder
    private var sharingPane: some View {
        if !config.machineFamily.supportsSharedFolder {
            let message = config.machineFamily == .powerMacOSX
                ? "Shared folders are unavailable for Mac OS X."
                : (pippin ? "Shared folders are unavailable for Apple Pippin."
                          : "Shared folders are unavailable for Copland.")
            Text(message).platinumText(.body)
        } else {
            VStack(alignment: .leading, spacing: 10) {
                Text("Share a folder from this computer. It appears on the classic Mac's desktop as a disk.")
                    .platinumText(.body)
                    .fixedSize(horizontal: false, vertical: true)
                HStack(spacing: 10) {
                    PlatinumIcon("folder", size: 32)
                    VStack(alignment: .leading, spacing: 2) {
                        if let path = config.sharedFolderPath, !path.isEmpty {
                            Text(URL(fileURLWithPath: path).lastPathComponent).platinumText(.emphasis)
                            Text(path).platinumText(.detail).lineLimit(1).truncationMode(.middle)
                        } else {
                            Text("No folder shared").platinumText(.emphasis)
                        }
                    }
                    Spacer()
                }
                HStack(spacing: Platinum.buttonGap) {
                    PlatinumButton(config.hasSharedFolder ? "Choose Another…" : "Choose Folder…", action: chooseFolder)
                    if config.hasSharedFolder {
                        PlatinumButton("Stop Sharing") { config.sharedFolderPath = nil }
                    }
                }
                if config.hasSharedFolder {
                    Text("Appears as the disk \u{201C}\(config.sharedVolumeName)\u{201D}." +
                         (config.bootFromCD && config.cdImagePath?.isEmpty == false
                          ? " Sharing is off while the Mac starts up from a CD." : ""))
                        .platinumText(.detail)
                }
            }
        }
    }

    private func chooseFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.message = "Choose a folder on this computer to share with your classic Mac"
        panel.prompt = "Share"
        if panel.runModal() == .OK, let url = panel.url {
            config.sharedFolderPath = url.path
        }
    }

    // MARK: Printing

    @ViewBuilder
    private var printingPane: some View {
        if !config.machineFamily.supportsPrinting {
            Text(pippin ? "Printing is unavailable for Apple Pippin." : "Printing is unavailable for Copland.")
                .platinumText(.body)
        } else {
            VStack(alignment: .leading, spacing: 10) {
                PlatinumCheckbox("Enable virtual printer (off by default)", isOn: $config.printingEnabled)

                HStack(spacing: 10) {
                    Text("Printer:").platinumText(.emphasis)
                    PlatinumPopUp(selection: printerSelection, options: printerOptions, width: 280)
                }

                HStack(spacing: Platinum.buttonGap) {
                    PlatinumButton("Refresh Printers") {
                        refreshPrinters(announce: true)
                    }
                    PlatinumButton("Print Test Page") {
                        printTestPage()
                    }
                    .disabled(isPrintingTestPage)
                }

                if let printingStatusMessage {
                    Text(printingStatusMessage)
                        .platinumText(.small)
                        .foregroundStyle(printingStatusIsError ? Platinum.errorText.color : Platinum.dimText.color)
                        .fixedSize(horizontal: false, vertical: true)
                }

                VStack(alignment: .leading, spacing: 3) {
                    Text("Guest connection: 10.0.2.4 · LPD queue classicmac · IPP ipp://10.0.2.4:631/printers/classicmac · JetDirect 9100.")
                        .platinumText(.detail)
                        .fixedSize(horizontal: false, vertical: true)
                    if config.machineFamily == .powerMacG4 {
                        Text("Mac OS 9 automatically creates “ClassicMac Printer” on the desktop when enabled.")
                            .platinumText(.detail)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    if HostPrinterManager.ghostscriptPath() == nil {
                        Text("Tip: Install Ghostscript (brew install ghostscript) for Save as PDF and AirPrint or raster printers.")
                            .platinumText(.detail)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                .padding(.top, 2)
            }
        }
    }

    private var printerSelection: Binding<String> {
        Binding(
            get: { config.printerName ?? "" },
            set: { value in
                let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
                config.printerName = trimmed.isEmpty ? nil : trimmed
                printingStatusMessage = nil
            }
        )
    }

    private var printerOptions: [(String, String)] {
        let defaultDisplay: String? = defaultPrinterQueue.flatMap { queue in
            printers.first(where: { $0.queueName == queue })?.displayName ?? queue
        }
        let defaultLabel = defaultDisplay.map { "System Default (\($0))" } ?? "System Default Printer"
        var options: [(String, String)] = [("", defaultLabel)]
        for printer in printers {
            let label = printer.isDefault ? "\(printer.displayName) (Default)" : printer.displayName
            options.append((printer.queueName, label))
        }
        if let saved = config.printerName,
           !saved.isEmpty,
           saved != HostPrinterManager.pdfTargetValue,
           !printers.contains(where: { $0.queueName == saved }) {
            options.append((saved, "\(saved) (Offline)"))
        }
        options.append((HostPrinterManager.pdfTargetValue, "Save as PDF (Printed PDFs)"))
        return options
    }

    private func refreshPrinters(announce: Bool = false) {
        DispatchQueue.global(qos: .userInitiated).async {
            let discovered = HostPrinterManager.availablePrinters()
            let defaultQueue = HostPrinterManager.defaultPrinterQueue()
            DispatchQueue.main.async {
                printers = discovered
                defaultPrinterQueue = defaultQueue
                if announce {
                    printingStatusIsError = false
                    printingStatusMessage = discovered.isEmpty
                        ? "No CUPS printers found. You can still use Save as PDF."
                        : "Found \(discovered.count) printer\(discovered.count == 1 ? "" : "s")."
                }
            }
        }
    }

    private func printTestPage() {
        isPrintingTestPage = true
        printingStatusMessage = "Sending test page…"
        printingStatusIsError = false
        let targetPrinter = config.printerName
        let pdfDir = HostPrinterManager.defaultPDFOutputDirectory(for: config)
        let targetLabel: String
        if targetPrinter == HostPrinterManager.pdfTargetValue {
            targetLabel = "Save as PDF"
        } else if let targetPrinter, !targetPrinter.isEmpty {
            targetLabel = targetPrinter
        } else {
            targetLabel = defaultPrinterQueue ?? "System Default Printer"
        }
        let version = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? ""

        DispatchQueue.global(qos: .userInitiated).async {
            let ps = HostPrinterManager.generateTestPostScript(targetLabel: targetLabel, version: version)
            do {
                let result = try HostPrinterManager.submitJob(
                    data: ps,
                    jobName: "ClassicMac Test Page (\(targetLabel))",
                    targetPrinter: targetPrinter,
                    pdfOutputDir: pdfDir
                )
                DispatchQueue.main.async {
                    isPrintingTestPage = false
                    printingStatusIsError = false
                    if let url = result.savedPDFURL {
                        printingStatusMessage = "Saved test PDF: \(url.lastPathComponent)"
                    } else {
                        printingStatusMessage = "Sent test page to \(result.target)."
                    }
                }
            } catch {
                DispatchQueue.main.async {
                    isPrintingTestPage = false
                    printingStatusIsError = true
                    printingStatusMessage = error.localizedDescription
                }
            }
        }
    }
}
