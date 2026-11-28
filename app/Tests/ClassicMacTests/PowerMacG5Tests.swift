import XCTest
@testable import ClassicMac

@MainActor
final class PowerMacG5Tests: XCTestCase {
    private func optionValues(_ flag: String, in args: [String]) -> [String] {
        var result: [String] = []
        var idx = 0
        while idx + 1 < args.count {
            if args[idx] == flag {
                result.append(args[idx + 1])
                idx += 2
            } else {
                idx += 1
            }
        }
        return result
    }

    private func makeG5Config(
        ramMB: Int = 2048,
        cpuCount: Int? = 2,
        graphicsCard: GraphicsCard = .standard,
        g5GraphicsROMPath: String? = nil,
        g5GraphicsGLMode: G5GraphicsGLMode? = nil,
        bootFromCD: Bool = false,
        cdImagePath: String? = nil,
        tabletInput: Bool = true,
        sharedFolderPath: String? = nil,
        networking: Bool = true,
        printingEnabled: Bool = false,
        useBrowserDisplay: Bool = false,
        bundleURL: URL = URL(fileURLWithPath: "/tmp/g5-test.classic")
    ) -> VMConfig {
        VMConfig(
            name: "Power Mac G5",
            machineFamily: .powerMacG5,
            ramMB: ramMB,
            depth: ColorDepth.millions.rawValue,
            useBrowserDisplay: useBrowserDisplay,
            cdImagePath: cdImagePath,
            bootFromCD: bootFromCD,
            networking: networking,
            sound: false,
            printingEnabled: printingEnabled,
            graphicsCard: graphicsCard,
            tabletInput: tabletInput,
            sharedFolderPath: sharedFolderPath,
            cpuCount: cpuCount,
            g5GraphicsROMPath: g5GraphicsROMPath,
            g5GraphicsGLMode: g5GraphicsGLMode,
            bundleURL: bundleURL
        )
    }

    func testConfigDefaultsSanitizationAndJSONRoundTrip() throws {
        XCTAssertTrue(MachineFamily.powerMacG5.isOSXFamily)
        XCTAssertTrue(MachineFamily.powerMacOSX.isOSXFamily)
        XCTAssertFalse(MachineFamily.powerMacG4.isOSXFamily)
        XCTAssertTrue(MachineFamily.powerMacG5.supportsSharedFolder)
        XCTAssertTrue(MachineFamily.powerMacG5.supportsPrinting)

        let defaults = makeG5Config()
        XCTAssertEqual(defaults.ramMB, 2048)
        XCTAssertEqual(defaults.effectiveCPUCount, 2)
        XCTAssertEqual(defaults.depth, ColorDepth.millions.rawValue)
        XCTAssertEqual(defaults.graphicsCard, .standard)
        XCTAssertFalse(defaults.toolsCDInserted)
        XCTAssertFalse(defaults.classicInputHelpers)

        // Clamping checks
        let clamped = VMConfig(
            name: "G5 Bounds",
            machineFamily: .powerMacG5,
            ramMB: 65_536,
            depth: ColorDepth.thousands.rawValue,
            graphicsCard: .atiRadeon,
            cpuCount: 64
        )
        XCTAssertEqual(clamped.ramMB, 16_384)
        XCTAssertEqual(clamped.effectiveCPUCount, 16)
        XCTAssertEqual(clamped.depth, ColorDepth.millions.rawValue)
        XCTAssertEqual(clamped.graphicsCard, .standard)

        let low = VMConfig(name: "G5 Low", machineFamily: .powerMacG5, ramMB: 128, cpuCount: 0)
        XCTAssertEqual(low.ramMB, 512)
        XCTAssertEqual(low.effectiveCPUCount, 1)

        // G5 graphics card on non-G5 resets to standard
        let g4WithG5Card = VMConfig(
            name: "G4",
            machineFamily: .powerMacG4,
            graphicsCard: .atiRadeon9800,
            cpuCount: 4,
            g5GraphicsROMPath: "/tmp/rom.bin",
            g5GraphicsGLMode: .fast
        )
        XCTAssertEqual(g4WithG5Card.graphicsCard, .standard)
        XCTAssertNil(g4WithG5Card.cpuCount)
        XCTAssertNil(g4WithG5Card.g5GraphicsROMPath)
        XCTAssertNil(g4WithG5Card.g5GraphicsGLMode)

        // JSON round-trip preserves G5 keys
        let original = makeG5Config(
            ramMB: 4096,
            cpuCount: 4,
            graphicsCard: .atiRadeon9800,
            g5GraphicsROMPath: "/tmp/radeon9800.rom",
            g5GraphicsGLMode: .on
        )
        let data = try JSONEncoder().encode(original)
        let decoded = try JSONDecoder().decode(VMConfig.self, from: data)
        XCTAssertEqual(decoded.machineFamily, .powerMacG5)
        XCTAssertEqual(decoded.cpuCount, 4)
        XCTAssertEqual(decoded.g5GraphicsROMPath, "/tmp/radeon9800.rom")
        XCTAssertEqual(decoded.g5GraphicsGLMode, .on)
    }

    func testStandardVGAArgumentsOmitVGARomfileAndUSBOff() {
        let vm = makeG5Config(ramMB: 4096, cpuCount: 4)
        let args = QEMUManager.buildArguments(for: vm)

        XCTAssertEqual(optionValues("-M", in: args), ["mac99,via=pmu"])
        XCTAssertEqual(optionValues("-cpu", in: args), ["970fx"])
        XCTAssertEqual(optionValues("-smp", in: args), ["4"])
        XCTAssertEqual(optionValues("-m", in: args), ["4096"])
        XCTAssertEqual(optionValues("-boot", in: args), ["c"])
        XCTAssertEqual(optionValues("-rtc", in: args), ["base=utc"])
        XCTAssertEqual(optionValues("-g", in: args), ["1024x768x32"])
        XCTAssertEqual(optionValues("-vga", in: args), ["std"])
        XCTAssertTrue(optionValues("-bios", in: args).first?.hasSuffix("/openbios-ppc64-g5") == true)

        let globals = optionValues("-global", in: args)
        XCTAssertTrue(globals.contains("macio-newworld.audiodev=snd0"))
        XCTAssertTrue(globals.contains("VGA.vgamem_mb=64"))
        // Regression guard: passing -global VGA.romfile= panics Mac OS X 10.3.9.
        XCTAssertFalse(globals.contains { $0.hasPrefix("VGA.romfile") })
        XCTAssertFalse(args.contains { $0.contains("usb=off") })

        let drives = optionValues("-drive", in: args)
        XCTAssertTrue(drives.contains { $0.contains("if=mtd,index=0") && $0.contains("/nvram.img") })
        XCTAssertTrue(drives.contains { $0.contains("if=none,id=sata0") })
        XCTAssertTrue(drives.contains { $0.contains("if=ide,index=0,media=cdrom,id=cd0,readonly=on") })

        let devices = optionValues("-device", in: args)
        XCTAssertTrue(devices.contains("ide-hd,bus=sata.0,drive=sata0"))
        XCTAssertTrue(devices.contains("usb-tablet"))
        XCTAssertEqual(optionValues("-nic", in: args), ["user,model=sungem"])
    }

    func testCDBootFallsBackToStandardGraphicsAndSkipsSharedFolder() {
        let vm = makeG5Config(
            graphicsCard: .atiRadeon9800,
            g5GraphicsROMPath: "/tmp/9800.rom",
            g5GraphicsGLMode: .fast,
            bootFromCD: true,
            cdImagePath: "/tmp/Panther, Disc 1.iso",
            sharedFolderPath: "/tmp/HostShare"
        )
        let args = QEMUManager.buildArguments(for: vm)

        XCTAssertEqual(optionValues("-boot", in: args), ["d"])
        XCTAssertEqual(optionValues("-vga", in: args), ["std"])
        XCTAssertFalse(optionValues("-device", in: args).contains { $0.contains("ati-radeon9800") })
        XCTAssertFalse(args.contains("-fsdev"))
        XCTAssertTrue(optionValues("-drive", in: args).contains { $0.contains("Panther,, Disc 1.iso") })
    }

    func testRadeon9800And7000ArgumentsAndGLModes() {
        for mode in G5GraphicsGLMode.allCases {
            let vm9800 = makeG5Config(
                graphicsCard: .atiRadeon9800,
                g5GraphicsROMPath: "/tmp/ati,9800.rom",
                g5GraphicsGLMode: mode
            )
            let args9800 = QEMUManager.buildArguments(for: vm9800)
            XCTAssertEqual(optionValues("-vga", in: args9800), ["none"])
            XCTAssertTrue(
                optionValues("-device", in: args9800)
                    .contains("ati-radeon9800,bus=pci.0,addr=0x10,gl=\(mode.rawValue),romfile=/tmp/ati,,9800.rom")
            )
        }

        let vm7000 = makeG5Config(
            graphicsCard: .atiRadeon7000,
            g5GraphicsROMPath: "/tmp/r7000.rom"
        )
        let args7000 = QEMUManager.buildArguments(for: vm7000)
        XCTAssertEqual(optionValues("-vga", in: args7000), ["none"])
        XCTAssertTrue(
            optionValues("-device", in: args7000)
                .contains("ati-vga,model=rv100,bus=pci.0,addr=0x10,agp=on,romfile=/tmp/r7000.rom")
        )
    }

    func testLeopardTabletAxisInsetRelativeMouseSharePrintingAndBrowser() throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("g5-leopard-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let leopardMachine = DownloadableMachine(
            id: "osx-10.5", name: "Mac OS X 10.5", summary: "Leopard",
            osVersion: "10.5.8", minimumAppVersion: "5.0.1",
            archiveURL: URL(string: "https://example.com/leopard.tar.gz")!,
            archiveBytes: 100, installedBytes: 1000,
            sha256: String(repeating: "b", count: 64),
            machineFamily: .powerMacG5
        )
        try JSONEncoder().encode(VMTemplateMetadata(machine: leopardMachine))
            .write(to: root.appendingPathComponent(VMTemplateMetadata.fileName))

        let vm = makeG5Config(
            sharedFolderPath: "/tmp/Shared,Dir",
            printingEnabled: true,
            useBrowserDisplay: true,
            bundleURL: root
        )
        let ports = QEMUManager.PrinterForwardPorts(lpd: 1515, ipp: 1631, raw: 19100)
        let args = QEMUManager.buildArguments(for: vm, printerPorts: ports)

        let devices = optionValues("-device", in: args)
        XCTAssertTrue(devices.contains("usb-tablet,axis-inset=75"))
        XCTAssertTrue(devices.contains("virtio-9p-pci,bus=pcix1,addr=0x2,fsdev=hostshare,mount_tag=Shared-Dir"))
        XCTAssertTrue(optionValues("-fsdev", in: args).contains("local,id=hostshare,path=/tmp/Shared,,Dir,security_model=none,macos_compat=on"))
        let nic = optionValues("-nic", in: args).first ?? ""
        XCTAssertTrue(nic.contains("model=sungem"))
        XCTAssertTrue(nic.contains("10.0.2.4:515"))
        XCTAssertTrue(nic.contains("10.0.2.4:631"))
        XCTAssertTrue(nic.contains("10.0.2.4:9100"))
        XCTAssertTrue(optionValues("-display", in: args).first?.hasPrefix("vnc=unix:") == true)

        // Relative mouse omits usb-tablet
        let relative = makeG5Config(tabletInput: false)
        let relativeArgs = QEMUManager.buildArguments(for: relative)
        XCTAssertFalse(optionValues("-device", in: relativeArgs).contains { $0.hasPrefix("usb-tablet") })
    }

    func testG5CheckpointSupportsFreshAllFFNVRAM() throws {
        let bundle = FileManager.default.temporaryDirectory
            .appendingPathComponent("g5-cp-\(UUID().uuidString).classic", isDirectory: true)
        try FileManager.default.createDirectory(at: bundle, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: bundle) }

        let vm = makeG5Config(bundleURL: bundle)
        try JSONEncoder().encode(vm).write(to: vm.configURL)
        try Data(repeating: 0xAA, count: 4096).write(to: vm.diskImageURL)
        try Data(repeating: 0xFF, count: OSXNVRAM.byteCount).write(to: vm.nvramImageURL)

        VMCheckpointStore.createFreshInstallIfSupported(in: bundle)
        let list = VMCheckpointStore.list(in: bundle)
        XCTAssertEqual(list.count, 1)
        XCTAssertEqual(list.first?.kind, .freshInstall)

        // Mutate disk + NVRAM and restore
        try Data(repeating: 0x11, count: 4096).write(to: vm.diskImageURL)
        try Data(repeating: 0x22, count: OSXNVRAM.byteCount).write(to: vm.nvramImageURL)
        try VMCheckpointStore.restore(list[0].id, for: vm, createBackup: false)

        XCTAssertEqual(try Data(contentsOf: vm.diskImageURL), Data(repeating: 0xAA, count: 4096))
        XCTAssertEqual(try Data(contentsOf: vm.nvramImageURL), Data(repeating: 0xFF, count: OSXNVRAM.byteCount))
    }

    func testLibraryEntrySupportsPowerMacG5ByOSVersion() {
        func entry(version: String, family: MachineFamily = .powerMacOSX) -> LibraryEntry {
            LibraryEntry(
                machine: DownloadableMachine(
                    id: "test-\(version)",
                    name: "Mac OS X \(version)",
                    summary: "Test",
                    osVersion: version,
                    minimumAppVersion: "4.8.0",
                    archiveURL: URL(string: "https://example.com/test.tar.gz")!,
                    archiveBytes: 100,
                    installedBytes: 1000,
                    sha256: String(repeating: "c", count: 64),
                    machineFamily: family
                ),
                edition: nil
            )
        }

        XCTAssertFalse(entry(version: "10.2.8").supportsPowerMacG5)
        XCTAssertFalse(entry(version: "10.3.3").supportsPowerMacG5)
        XCTAssertTrue(entry(version: "10.3.4").supportsPowerMacG5)
        XCTAssertTrue(entry(version: "10.3.9").supportsPowerMacG5)
        XCTAssertTrue(entry(version: "10.4.11").supportsPowerMacG5)
        XCTAssertTrue(entry(version: "10.5.8").supportsPowerMacG5)
        XCTAssertFalse(entry(version: "9.2.2", family: .powerMacG4).supportsPowerMacG5)
    }
}
