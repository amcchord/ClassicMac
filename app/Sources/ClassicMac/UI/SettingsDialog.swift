import AppKit
import SwiftUI
import PlatinumKit

/// A Mac's settings, in the shape of the Finder's Get Info window: a "Show:"
/// pop-up for General, Display and Sharing, then Cancel and OK. Edits apply
/// when you click OK.
struct SettingsDialog: View {
    static let size = NSSize(width: 520, height: 440)

    enum Pane: Hashable { case general, display, sharing }

    let original: VMConfig
    let handle: DialogHandle
    @State private var config: VMConfig
    @State private var pane: Pane = {
        switch ProcessInfo.processInfo.environment["CLASSICMAC_UI_SETTINGS_PANE"] {
        case "display": return .display
        case "sharing": return .sharing
        default: return .general
        }
    }()
    @State private var widthText: String
    @State private var heightText: String
    @State private var errorMessage: String?
    @ObservedObject private var manager = QEMUManager.shared

    init(original: VMConfig, handle: DialogHandle) {
        self.original = original
        self.handle = handle
        _config = State(initialValue: original)
        _widthText = State(initialValue: String(original.width))
        _heightText = State(initialValue: String(original.height))
    }

    private var running: Bool { manager.isRunning(original.id) }
    private var copland: Bool { config.machineFamily == .powerMac7500 }

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
                PlatinumPopUp(selection: $pane, options: [
                    (Pane.general, "General Information"),
                    (Pane.display, "Display"),
                    (Pane.sharing, "Sharing"),
                ], width: 190)
                Spacer()
            }
            PlatinumSeparator()
            Group {
                switch pane {
                case .general: generalPane
                case .display: displayPane
                case .sharing: sharingPane
                }
            }
            .disabled(running)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            if running {
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
                if running {
                    PlatinumButton("OK", role: .default) { handle.close() }
                } else {
                    PlatinumButton("Cancel", role: .cancel) { handle.close() }
                    PlatinumButton("OK", role: .default, action: save)
                }
            }
        }
        .padding(12)
        .frame(width: Self.size.width, height: Self.size.height)
        .background(Platinum.face.color)
        .onAppear { coerceDepth() }
        .onChange(of: config.name) { _, _ in errorMessage = nil }
    }

    private func save() {
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
                    Text("Hard disk:").platinumText(.emphasis).padding(.leading, 12)
                    Text("\(config.diskSizeGB) GB").platinumText(.body)
                }
            }
            if !copland {
                VStack(alignment: .leading, spacing: 7) {
                    PlatinumCheckbox("Use this computer's Internet connection", isOn: $config.networking)
                    PlatinumCheckbox("Play the Mac's sound", isOn: $config.sound)
                    if config.machineFamily == .powerMacG4 {
                        PlatinumCheckbox("PowerPC G4 processor (off for Mac OS 8.5)", isOn: $config.useG4CPU)
                    }
                    if config.machineFamily != .powerMacOSX {
                        PlatinumCheckbox("Right-click and scroll wheel in classic apps", isOn: $config.classicInputHelpers)
                    }
                    PlatinumCheckbox("Seamless mouse: move in and out freely", isOn: $config.tabletInput)
                    if config.machineFamily == .powerMacOSX {
                        Text("Turn off the seamless mouse to use a relative USB mouse.")
                            .platinumText(.detail)
                    } else {
                        Text("Turn off right-click and scrolling when USB Overdrive is installed in Mac OS. Games can still capture the mouse.")
                            .platinumText(.detail)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                .padding(.leading, 92)
                .padding(.top, 4)
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

    // MARK: Sharing

    @ViewBuilder
    private var sharingPane: some View {
        if !config.machineFamily.supportsSharedFolder {
            let message = config.machineFamily == .powerMacOSX
                ? "Shared folders are unavailable for Mac OS X."
                : "Shared folders are unavailable for Copland."
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
}
