import XCTest
import Darwin
import CDisplaySocket
@testable import ClassicMac

@MainActor
final class QEMURunnerArgumentTests: XCTestCase {
    private func config(
        family: MachineFamily = .powerMacG4,
        cdImagePath: String? = "/tmp/install.iso",
        bootFromCD: Bool = true,
        floppyImagePath: String? = nil,
        toolsCDInserted: Bool = true,
        networking: Bool = true,
        useG4CPU: Bool = true,
        tabletInput: Bool = true,
        depth: Int = ColorDepth.thousands.rawValue,
        sharedFolderPath: String? = nil,
        useBrowserDisplay: Bool = false,
        classicInputHelpers: Bool = true
    ) -> VMConfig {
        VMConfig(
            name: "Argument Test",
            machineFamily: family,
            ramMB: family.defaultRAMMB,
            depth: depth,
            useBrowserDisplay: useBrowserDisplay,
            cdImagePath: cdImagePath,
            bootFromCD: bootFromCD,
            floppyImagePath: floppyImagePath,
            toolsCDInserted: toolsCDInserted,
            networking: networking,
            sound: false,
            useG4CPU: useG4CPU,
            tabletInput: tabletInput,
            classicInputHelpers: classicInputHelpers,
            sharedFolderPath: sharedFolderPath,
            bundleURL: URL(fileURLWithPath: "/tmp/argument-test.classic")
        )
    }

    func testOSXUsesDedicatedFirmwareAndNativeUSB() {
        let vm = config(family: .powerMacOSX, cdImagePath: "/tmp/Tiger, retail.img",
                        tabletInput: false, sharedFolderPath: "/tmp/share")
        let args = QEMUManager.buildArguments(for: vm)
        XCTAssertEqual(optionValues("-M", in: args), ["mac99,via=pmu,usb=off,audiodev=snd0"])
        XCTAssertEqual(optionValues("-boot", in: args), ["d"])
        XCTAssertEqual(optionValues("-g", in: args), ["1024x768x32"])
        XCTAssertEqual(optionValues("-device", in: args),
                       ["pci-ohci,id=osx-usb", "usb-kbd,bus=osx-usb.0", "usb-mouse,bus=osx-usb.0"])
        XCTAssertTrue(optionValues("-bios", in: args).first?.hasSuffix("/openbios-ppc-osx") == true)
        XCTAssertTrue(optionValues("-drive", in: args).contains { $0.contains("Tiger,, retail.img") })
        XCTAssertTrue(optionValues("-drive", in: args).contains { $0.contains("/nvram.img") })
        XCTAssertFalse(args.contains("-icount"))
        XCTAssertFalse(args.contains("-fsdev"))
        XCTAssertFalse(vm.toolsCDInserted)
    }

    func testVirtioStartupAndCompatibilityCDHaveSelectedBootDeviceFirst() {
        for fromCD in [false, true] {
            var vm = config(cdImagePath: fromCD ? "/tmp/install.iso" : nil,
                            bootFromCD: fromCD, toolsCDInserted: false,
                            useG4CPU: false, tabletInput: false)
            vm.useVirtioDisk = true
            let args = QEMUManager.buildArguments(for: vm)
            let disks = optionValues("-device", in: args).filter { $0.hasPrefix("virtio-blk-pci,") }
            XCTAssertEqual(disks, fromCD
                ? ["virtio-blk-pci,drive=classicmac-cd-boot,addr=0xf", "virtio-blk-pci,drive=startup0,addr=0x10"]
                : ["virtio-blk-pci,drive=startup0,addr=0xf"])
            XCTAssertTrue(optionValues("-prom-env", in: args).contains("boot-device=/pci@f2000000/scsi@f/disk:\\\\:tbxi"))
            XCTAssertTrue(optionValues("-device", in: args).contains { $0.hasPrefix("loader,") })
            XCTAssertTrue(args.contains("-nodefaults"))
            XCTAssertFalse(args.contains("-icount"))
            XCTAssertEqual(optionValues("-M", in: args), ["mac99,via=cuda,usb=off,audiodev=snd0"])
            XCTAssertEqual(optionValues("-nic", in: args), ["user,model=rtl8139"])
            XCTAssertTrue(optionValues("-global", in: args).contains("cuda.idle-irq-ms=50"))
            XCTAssertTrue(optionValues("-global", in: args).contains("rtl8139.tx-delay-ns=1000000"))
            XCTAssertFalse(args.contains("usb-mouse,id=classicmac-game-mouse"))
            if !fromCD { XCTAssertFalse(optionValues("-drive", in: args).contains { $0.contains("if=ide") }) }
            XCTAssertFalse(optionValues("-drive", in: args).contains { $0.contains("disk.img,format=raw,media=disk") })
        }
        let native = QEMUManager.buildArguments(for: config(cdImagePath: nil, bootFromCD: false))
        XCTAssertFalse(native.contains("virtio-blk-pci,drive=startup0"))
    }

    func testPowerMacIncludesInputSprocketHIDMouseWithAndWithoutTablet() {
        for tablet in [true, false] {
            let args = QEMUManager.buildArguments(for: config(tabletInput: tablet))
            XCTAssertTrue(optionValues("-device", in: args).contains("usb-mouse,id=classicmac-game-mouse"))
        }
        let quadra = QEMUManager.buildArguments(for: config(family: .quadra800))
        XCTAssertFalse(quadra.contains("usb-mouse,id=classicmac-game-mouse"))
    }

    func testPowerMacGraphicsAccelerationOptionsAndBootDepth() {
        let thousands = QEMUManager.buildArguments(for: config())
        let millions = QEMUManager.buildArguments(
            for: config(depth: ColorDepth.millions.rawValue)
        )

        XCTAssertTrue(optionValues("-global", in: thousands).contains("VGA.hardware-cursor=on"))
        XCTAssertTrue(optionValues("-global", in: thousands).contains("VGA.untracked-vram=on"))
        XCTAssertFalse(thousands.contains { $0.hasPrefix("VGA.gxmetal") }, "GXMetal is gone in 4.0")
        XCTAssertEqual(optionValues("-vga", in: thousands), ["std"])
        XCTAssertEqual(optionValues("-accel", in: thousands), ["tcg,tb-size=512"])
        XCTAssertEqual(optionValues("-cpu", in: thousands), ["7400"])
        XCTAssertEqual(optionValues("-g", in: thousands), ["1024x768x15"])
        XCTAssertEqual(optionValues("-g", in: millions), ["1024x768x32"])
    }

    func testATIRadeonCardUsesStockDriversAtTheChosenSize() {
        var vm = config(cdImagePath: nil, bootFromCD: false)
        vm.graphicsCard = .atiRadeon
        vm.width = 1024
        vm.height = 768
        let arguments = QEMUManager.buildArguments(for: vm)

        XCTAssertEqual(optionValues("-vga", in: arguments), ["none"])
        XCTAssertTrue(optionValues("-device", in: arguments).contains(
            "ati-vga,id=ati0,model=r100,vgamem_mb=32,romfile=,guest_hwcursor=on,xres=1024,yres=768,refresh_rate=75000"))
        XCTAssertTrue(optionValues("-prom-env", in: arguments).contains("classicmac-display=1024x768"))
        XCTAssertTrue(optionValues("-prom-env", in: arguments).contains("retroboot-display=1024x768"))
        XCTAssertEqual(optionValues("-g", in: arguments), ["640x480x32"])
        XCTAssertTrue(optionValues("-device", in: arguments).contains("classicmac-display-control"))
        XCTAssertTrue(optionValues("-device", in: arguments).contains { $0.contains("file=\(AppPaths.ndrvLoader.path)") })
        // ATI starts run in real time and never use the VGA-only Finder detector.
        XCTAssertTrue(optionValues("-icount", in: arguments).isEmpty)
        XCTAssertFalse(optionValues("-global", in: arguments).contains { $0.hasPrefix("VGA.") })
    }

    func testATIMacStartsFromCDOnTheStandardCard() {
        var vm = config()
        vm.graphicsCard = .atiRadeon
        let arguments = QEMUManager.buildArguments(for: vm)
        XCTAssertEqual(optionValues("-vga", in: arguments), ["std"])
        XCTAssertFalse(arguments.contains { $0.hasPrefix("ati-vga") })
    }

    func testATIMacSeparatesTabletAndNetworkInterruptsWithTools() throws {
        let resources = FileManager.default.temporaryDirectory
            .appendingPathComponent("ClassicMac-argument-tools-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: resources, withIntermediateDirectories: true)
        try Data().write(to: resources.appendingPathComponent("ClassicMacTools.iso"))
        let previous = ProcessInfo.processInfo.environment["CLASSICMAC_RESOURCES"]
        setenv("CLASSICMAC_RESOURCES", resources.path, 1)
        defer {
            if let previous { setenv("CLASSICMAC_RESOURCES", previous, 1) }
            else { unsetenv("CLASSICMAC_RESOURCES") }
            try? FileManager.default.removeItem(at: resources)
        }
        var vm = config(cdImagePath: nil, bootFromCD: false)
        vm.graphicsCard = .atiRadeon
        XCTAssertTrue(optionValues("-device", in: QEMUManager.buildArguments(for: vm))
            .contains("virtio-tablet-pci,addr=0x13"))

        vm.networking = false
        XCTAssertTrue(optionValues("-device", in: QEMUManager.buildArguments(for: vm))
            .contains("virtio-tablet-pci"))
        vm.networking = true
        vm.toolsCDInserted = false
        XCTAssertTrue(optionValues("-device", in: QEMUManager.buildArguments(for: vm))
            .contains("virtio-tablet-pci"))
        vm.toolsCDInserted = true
        vm.graphicsCard = .standard
        XCTAssertTrue(optionValues("-device", in: QEMUManager.buildArguments(for: vm))
            .contains("virtio-tablet-pci"))
    }

    func testATIRendererNameFromMonitorReply() {
        XCTAssertEqual(ATIDisplay.rendererName(fromHMP: "\"CGL; GL 4.1 Metal - 91.7; Apple M5 Max\"\r\n"), "Apple M5 Max")
        XCTAssertEqual(ATIDisplay.rendererName(fromHMP: "\"\"\r\n"), "")
    }

    func testATIDisplaySizesFollowTheDriverLimits() {
        XCTAssertEqual(ATIDisplay.windowSize(width: 907, height: 656).width, 904)
        XCTAssertEqual(ATIDisplay.windowSize(width: 900, height: 1400).height, 1400)
        XCTAssertEqual(ATIDisplay.windowSize(width: 3840, height: 2160).height, 1440)
        XCTAssertEqual(ATIDisplay.refresh(width: 1024, height: 768), 75)
        XCTAssertEqual(ATIDisplay.refresh(width: 1920, height: 1080), 50, "60 Hz would need 173.7 MHz")
        // 1922 rounds down to an 8-pixel width; portrait and oversize fall back.
        XCTAssertEqual(ATIDisplay.size(fitting: 1922, height: 1080).width, 1920)
        XCTAssertEqual(ATIDisplay.size(fitting: 900, height: 1400).width, 832)
        XCTAssertEqual(ATIDisplay.size(fitting: 3840, height: 2160).width, 1920)
        XCTAssertEqual(ATIDisplay.size(fitting: 500, height: 300).width, 640)
        var quadra = config(family: .quadra800)
        quadra.graphicsCard = .atiRadeon
        let decoded = try! JSONDecoder().decode(VMConfig.self, from: JSONEncoder().encode(quadra))
        XCTAssertEqual(decoded.graphicsCard, .standard, "Only Power Macs have the ATI card")
    }

    func testBothMachinesShowTheirScreenInClassicMacsWindow() {
        let powerMac = config()
        let quadra = config(family: .quadra800)

        for vm in [powerMac, quadra] {
            let arguments = QEMUManager.buildArguments(for: vm)
            let display = try! XCTUnwrap(optionValues("-display", in: arguments).first)
            XCTAssertEqual(display, "classicmac,socket=\(GuestDisplaySession.socketPath(for: vm.id))")
            XCTAssertLessThan(GuestDisplaySession.socketPath(for: vm.id).utf8.count, 104, "AF_UNIX path limit")
        }
    }

    func testBothMachinesCanOptIntoLoopbackBrowserVNCDisplay() {
        let powerMac = config(useBrowserDisplay: true)
        let quadra = config(family: .quadra800, useBrowserDisplay: true)

        for vm in [powerMac, quadra] {
            let arguments = QEMUManager.buildArguments(for: vm)
            let display = try! XCTUnwrap(optionValues("-display", in: arguments).first)

            XCTAssertFalse(display.contains("cocoa"))
            XCTAssertFalse(display.contains("websocket="))
            XCTAssertTrue(display.hasPrefix("vnc=unix:\(AppPaths.runtimeSocketDir.path)/"))
            XCTAssertTrue(display.hasSuffix("share=force-shared"))
        }
    }

    func testQEMUWindowFallbackCanDisableClassicInputHelpers() {
        UserDefaults.standard.set(true, forKey: "ClassicMacUseQEMUWindow")
        defer { UserDefaults.standard.removeObject(forKey: "ClassicMacUseQEMUWindow") }
        let arguments = QEMUManager.buildArguments(
            for: config(classicInputHelpers: false)
        )
        let display = try! XCTUnwrap(
            optionValues("-display", in: arguments).first
        )

        XCTAssertEqual(display, "cocoa,swap-opt-cmd=off")
    }

    func testPowerMacIDECompletionDelayOnlyProtectsInstallerBoots() {
        let installer = QEMUManager.buildArguments(for: config())
        let hardDisk = QEMUManager.buildArguments(
            for: config(cdImagePath: nil, bootFromCD: false)
        )

        XCTAssertTrue(
            optionValues("-global", in: installer)
                .contains("macio-ide.dma-completion-delay-ns=1000000")
        )
        XCTAssertTrue(
            optionValues("-global", in: hardDisk)
                .contains("macio-ide.dma-completion-delay-ns=0")
        )
    }

    func testPowerMacInstructionClockOnlyAcceleratesHardDiskStartup() {
        let installer = QEMUManager.buildArguments(for: config())
        let hardDisk = QEMUManager.buildArguments(
            for: config(cdImagePath: nil, bootFromCD: false)
        )

        XCTAssertTrue(optionValues("-icount", in: installer).isEmpty)
        XCTAssertEqual(
            optionValues("-icount", in: hardDisk),
            ["shift=4,sleep=off"]
        )
        XCTAssertFalse(
            optionValues("-global", in: installer)
                .contains("VGA.classicmac-boot-handoff=on")
        )
        XCTAssertTrue(
            optionValues("-global", in: hardDisk)
                .contains("VGA.classicmac-boot-handoff=on")
        )
    }

    func testPowerMacCanRetainG3ForMacOS85Compatibility() {
        let arguments = QEMUManager.buildArguments(for: config(useG4CPU: false))

        XCTAssertEqual(optionValues("-cpu", in: arguments), ["g3"])
    }

    private func optionValues(_ option: String, in arguments: [String]) -> [String] {
        arguments.indices.compactMap { index in
            guard arguments[index] == option, arguments.indices.contains(index + 1) else {
                return nil
            }
            return arguments[index + 1]
        }
    }

    private func withTemporaryToolsCD<T>(_ body: () throws -> T) throws -> T {
        let key = "CLASSICMAC_REPO"
        let previous = ProcessInfo.processInfo.environment[key]
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        let dist = root.appendingPathComponent("dist", isDirectory: true)
        let toolsCD = dist.appendingPathComponent("ClassicMacTools.iso")

        try FileManager.default.createDirectory(
            at: dist,
            withIntermediateDirectories: true
        )
        try Data().write(to: toolsCD)
        setenv(key, root.path, 1)
        defer {
            if let previous {
                setenv(key, previous, 1)
            } else {
                unsetenv(key)
            }
            try? FileManager.default.removeItem(at: root)
        }

        return try body()
    }

    func testPowerMacG4CDStartupUsesSingleIDEInstallerVolume() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(for: config())
            let toolsDrive = optionValues("-drive", in: arguments)
                .first { $0.contains("id=tools0") }

            XCTAssertEqual(
                toolsDrive,
                "if=ide,index=3,media=cdrom,id=tools0,readonly=on"
            )
            let userDisc = optionValues("-drive", in: arguments)
                .first { $0.contains("id=cd0") }
            XCTAssertEqual(
                userDisc,
                "if=ide,index=2,media=cdrom,id=cd0,readonly=on,file=/tmp/install.iso,format=raw"
            )
            let devices = optionValues("-device", in: arguments)
            XCTAssertFalse(devices.contains { $0.contains("classicmac-tools") })
            XCTAssertTrue(optionValues("-nic", in: arguments).contains("user,model=sungem"))
            XCTAssertTrue(optionValues("-blockdev", in: arguments).isEmpty)
            XCTAssertFalse(
                optionValues("-device", in: arguments)
                    .contains("virtio-blk-pci,drive=classicmac-cd-boot")
            )
            XCTAssertEqual(optionValues("-boot", in: arguments), ["d"])
            XCTAssertEqual(
                arguments.filter { $0.contains("/tmp/install.iso") }.count,
                1
            )
        }
    }

    func testPowerMacG3CDStartupKeepsIDEMediaAndVirtioBootMirror() {
        let arguments = QEMUManager.buildArguments(
            for: config(useG4CPU: false, tabletInput: false)
        )

        XCTAssertEqual(
            optionValues("-drive", in: arguments)
                .first { $0.contains("id=cd0") },
            "if=ide,index=2,media=cdrom,id=cd0,readonly=on,file=/tmp/install.iso,format=raw"
        )
        XCTAssertTrue(
            optionValues("-blockdev", in: arguments).contains(
                "driver=file,node-name=classicmac-cd-file,filename=/tmp/install.iso,read-only=on"
            )
        )
        XCTAssertTrue(
            optionValues("-device", in: arguments)
                .contains("virtio-blk-pci,drive=classicmac-cd-boot")
        )
        XCTAssertTrue(optionValues("-boot", in: arguments).isEmpty)
        XCTAssertEqual(
            arguments.filter { $0.contains("/tmp/install.iso") }.count,
            2
        )
    }

    func testPowerMacNormalStartupMountsToolsThroughVirtio() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(for: config(bootFromCD: false))
            let toolsDrive = try XCTUnwrap(
                optionValues("-drive", in: arguments).first { $0.contains("id=tools0") }
            )

            XCTAssertEqual(
                toolsDrive,
                "if=ide,index=2,media=cdrom,id=tools0,readonly=on"
            )
            XCTAssertEqual(
                optionValues("-blockdev", in: arguments),
                [
                    "driver=file,node-name=classicmac-tools-file,filename=\(AppPaths.toolsCD!.path),read-only=on",
                    "driver=raw,node-name=classicmac-tools,file=classicmac-tools-file,read-only=on"
                ]
            )
            XCTAssertTrue(
                optionValues("-device", in: arguments)
                    .contains("virtio-blk-pci,drive=classicmac-tools")
            )
            XCTAssertFalse(
                optionValues("-prom-env", in: arguments)
                    .contains { $0.hasPrefix("boot-device=virtio") }
            )
            XCTAssertTrue(
                optionValues("-device", in: arguments)
                    .contains { $0.hasPrefix("loader,addr=0x4000000,file=") }
            )
        }
    }

    func testPowerMacCDStartupKeepsToolsTrayEmptyWhenNetworkingIsOff() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(
                for: config(networking: false)
            )
            let toolsDrive = try XCTUnwrap(
                optionValues("-drive", in: arguments).first { $0.contains("id=tools0") }
            )

            XCTAssertEqual(
                toolsDrive,
                "if=ide,index=3,media=cdrom,id=tools0,readonly=on"
            )
            XCTAssertEqual(
                optionValues("-drive", in: arguments)
                    .first { $0.contains("id=cd0") },
                "if=ide,index=2,media=cdrom,id=cd0,readonly=on,file=/tmp/install.iso,format=raw"
            )
            XCTAssertEqual(optionValues("-nic", in: arguments), ["none"])
        }
    }

    func testPowerMacCDStartupEscapesCommasInSelectedDiscPath() {
        let arguments = QEMUManager.buildArguments(
            for: config(cdImagePath: "/tmp/Mac OS 8,5.iso")
        )

        XCTAssertEqual(
            optionValues("-drive", in: arguments)
                .first { $0.contains("id=cd0") },
            "if=ide,index=2,media=cdrom,id=cd0,readonly=on,file=/tmp/Mac OS 8,,5.iso,format=raw"
        )
        XCTAssertTrue(optionValues("-blockdev", in: arguments).isEmpty)
    }

    func testPowerMacToolsPreferenceOffKeepsRemovableDriveEmpty() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(
                for: config(
                    bootFromCD: false,
                    toolsCDInserted: false,
                    networking: false
                )
            )
            let toolsDrive = try XCTUnwrap(
                optionValues("-drive", in: arguments).first { $0.contains("id=tools0") }
            )

            XCTAssertEqual(
                toolsDrive,
                "if=ide,index=2,media=cdrom,id=tools0,readonly=on"
            )
            XCTAssertFalse(
                optionValues("-blockdev", in: arguments)
                    .contains { $0.contains("classicmac-tools") }
            )
            XCTAssertFalse(
                optionValues("-device", in: arguments)
                    .contains("virtio-blk-pci,drive=classicmac-tools")
            )
        }
    }

    func testPowerMacToolsVirtioMountLoadsNDRVWithoutOtherVirtioFeatures() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(
                for: config(
                    cdImagePath: nil,
                    bootFromCD: false,
                    toolsCDInserted: true,
                    tabletInput: false,
                    sharedFolderPath: nil
                )
            )

            XCTAssertTrue(
                optionValues("-device", in: arguments)
                    .contains("virtio-blk-pci,drive=classicmac-tools")
            )
            XCTAssertTrue(
                optionValues("-device", in: arguments)
                    .contains { $0.hasPrefix("loader,addr=0x4000000,file=") }
            )
            XCTAssertTrue(
                optionValues("-prom-env", in: arguments)
                    .contains("boot-command=init-program go")
            )
        }
    }

    func testPowerMacToolsAndSharedFolderUseIndependentVirtioDevices() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(
                for: config(
                    cdImagePath: nil,
                    bootFromCD: false,
                    toolsCDInserted: true,
                    sharedFolderPath: "/tmp/Shared Test"
                )
            )
            let devices = optionValues("-device", in: arguments)

            XCTAssertTrue(devices.contains("virtio-blk-pci,drive=classicmac-tools"))
            XCTAssertTrue(
                devices.contains("virtio-9p-pci,fsdev=share0,mount_tag=Shared Test")
            )
            XCTAssertEqual(
                optionValues("-fsdev", in: arguments),
                ["local,id=share0,security_model=none,path=/tmp/Shared Test"]
            )
        }
    }

    func testPowerMacKeepsAnEmptyUserDiscTrayAvailable() {
        let arguments = QEMUManager.buildArguments(
            for: config(
                cdImagePath: nil,
                bootFromCD: false,
                toolsCDInserted: false
            )
        )

        XCTAssertTrue(optionValues("-boot", in: arguments).isEmpty)
        XCTAssertEqual(
            optionValues("-drive", in: arguments)
                .first { $0.contains("id=cd0") },
            "if=ide,index=3,media=cdrom,id=cd0,readonly=on"
        )
    }

    func testQuadraToolsDriveRemainsOnDedicatedSCSIPosition() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(
                for: config(family: .quadra800, bootFromCD: false)
            )
            let toolsDrive = try XCTUnwrap(
                optionValues("-drive", in: arguments).first { $0.contains("id=tools0") }
            )

            XCTAssertTrue(
                toolsDrive.hasPrefix("if=none,media=cdrom,id=tools0,file=")
            )
            XCTAssertFalse(toolsDrive.contains("bus="))
            XCTAssertFalse(toolsDrive.contains("unit="))
            XCTAssertTrue(
                optionValues("-device", in: arguments)
                    .contains(
                        "scsi-cd,scsi-id=4,drive=tools0"
                    )
            )
        }
    }

    func testQuadraToolsPreferenceOffKeepsSCSITrayEmpty() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(
                for: config(
                    family: .quadra800,
                    bootFromCD: false,
                    toolsCDInserted: false
                )
            )
            let toolsDrive = try XCTUnwrap(
                optionValues("-drive", in: arguments).first { $0.contains("id=tools0") }
            )

            XCTAssertEqual(
                toolsDrive,
                "if=none,media=cdrom,id=tools0,readonly=on"
            )
            XCTAssertTrue(
                optionValues("-device", in: arguments)
                    .contains(
                        "scsi-cd,scsi-id=4,drive=tools0"
                    )
            )
        }
    }

    func testQuadraKeepsAnEmptyUserDiscTrayAvailable() {
        let arguments = QEMUManager.buildArguments(
            for: config(
                family: .quadra800,
                cdImagePath: nil,
                bootFromCD: false,
                toolsCDInserted: false
            )
        )

        XCTAssertTrue(
            optionValues("-device", in: arguments)
                .contains("scsi-cd,scsi-id=3,drive=cd0")
        )
        XCTAssertEqual(
            optionValues("-drive", in: arguments)
                .first { $0.contains("id=cd0") },
            "media=cdrom,if=none,id=cd0,readonly=on"
        )
    }

    func testQuadraRestartsThroughInspectableProcessBoundary() {
        let arguments = QEMUManager.buildArguments(
            for: config(family: .quadra800)
        )

        XCTAssertEqual(
            optionValues("-action", in: arguments),
            ["reboot=shutdown"]
        )
        XCTAssertEqual(optionValues("-boot", in: arguments), ["d"])
        XCTAssertEqual(optionValues("-qmp", in: arguments).count, 1)
    }

    func testQuadraKeepsAnEmptyWritableFloppyDriveAvailable() {
        let arguments = QEMUManager.buildArguments(
            for: config(
                family: .quadra800,
                cdImagePath: nil,
                bootFromCD: false,
                floppyImagePath: nil,
                toolsCDInserted: false
            )
        )

        XCTAssertEqual(
            optionValues("-drive", in: arguments)
                .first { $0.contains("id=fd0") },
            "if=none,id=fd0,media=cdrom,readonly=off"
        )
        XCTAssertTrue(
            optionValues("-device", in: arguments).contains(
                "virtio-blk-device,drive=fd0,removable=on"
            )
        )
    }

    func testQuadraLoadsConfiguredFloppyImageWritable() {
        let arguments = QEMUManager.buildArguments(
            for: config(
                family: .quadra800,
                cdImagePath: nil,
                bootFromCD: false,
                floppyImagePath: "/tmp/System Tools.img",
                toolsCDInserted: false
            )
        )

        XCTAssertEqual(
            optionValues("-drive", in: arguments)
                .first { $0.contains("id=fd0") },
            "if=none,id=fd0,file=/tmp/System Tools.img,format=raw"
        )
        XCTAssertTrue(
            optionValues("-device", in: arguments).contains(
                "virtio-blk-device,drive=fd0,removable=on"
            )
        )
    }

    func testPowerMacDoesNotExposeAFloppyDrive() {
        let arguments = QEMUManager.buildArguments(
            for: config(
                family: .powerMacG4,
                floppyImagePath: "/tmp/System Tools.img"
            )
        )

        XCTAssertFalse(
            optionValues("-drive", in: arguments)
                .contains { $0.contains("id=fd0") }
        )
        XCTAssertFalse(
            optionValues("-device", in: arguments)
                .contains { $0.contains("drive=fd0") }
        )
    }

    func testNetworkingOffExplicitlyDisablesDefaultNICs() {
        let powerMacArguments = QEMUManager.buildArguments(
            for: config(networking: false)
        )
        let quadraArguments = QEMUManager.buildArguments(
            for: config(family: .quadra800, networking: false)
        )

        XCTAssertEqual(optionValues("-nic", in: powerMacArguments), ["none"])
        XCTAssertEqual(optionValues("-nic", in: quadraArguments), ["none"])
    }

    func testPowerMacCDStartupKeepsTabletInputEnabled() {
        let arguments = QEMUManager.buildArguments(
            for: config(tabletInput: true)
        )
        let devices = optionValues("-device", in: arguments)

        XCTAssertEqual(
            optionValues("-M", in: arguments),
            ["mac99,via=cuda,audiodev=snd0"]
        )
        XCTAssertEqual(optionValues("-cpu", in: arguments), ["7400"])
        XCTAssertEqual(optionValues("-boot", in: arguments), ["d"])
        XCTAssertTrue(devices.contains("virtio-tablet-pci"))
        XCTAssertTrue(
            devices.contains { $0.hasPrefix("loader,addr=0x4000000,file=") }
        )
        XCTAssertTrue(
            optionValues("-prom-env", in: arguments)
                .contains("boot-command=init-program go")
        )
    }

    func testPowerMacCDStartupSuppressesSharingButKeepsTabletInput() {
        let arguments = QEMUManager.buildArguments(
            for: config(
                tabletInput: true,
                sharedFolderPath: "/tmp/shared-folder"
            )
        )
        let devices = optionValues("-device", in: arguments)

        XCTAssertTrue(devices.contains("virtio-tablet-pci"))
        XCTAssertFalse(devices.contains { $0.hasPrefix("virtio-9p-pci,") })
        XCTAssertTrue(optionValues("-fsdev", in: arguments).isEmpty)
    }

    func testPowerMacG4CDStartupWithoutTabletUsesCUDAMouseAndIDEBoot() {
        let arguments = QEMUManager.buildArguments(
            for: config(tabletInput: false)
        )
        let devices = optionValues("-device", in: arguments)

        XCTAssertEqual(
            optionValues("-M", in: arguments),
            ["mac99,via=cuda,audiodev=snd0"]
        )
        XCTAssertFalse(devices.contains("virtio-tablet-pci"))
        XCTAssertFalse(
            devices.contains { $0.hasPrefix("loader,addr=0x4000000,file=") }
        )
        XCTAssertFalse(
            optionValues("-prom-env", in: arguments)
                .contains("boot-command=init-program go")
        )
        XCTAssertFalse(
            optionValues("-prom-env", in: arguments)
                .contains("boot-device=virtio0:\\\\:tbxi")
        )
        XCTAssertEqual(optionValues("-boot", in: arguments), ["d"])
    }

    func testPowerMacNormalStartupKeepsTabletInputEnabled() {
        let arguments = QEMUManager.buildArguments(
            for: config(bootFromCD: false, tabletInput: true)
        )

        XCTAssertEqual(
            optionValues("-M", in: arguments),
            ["mac99,via=cuda,audiodev=snd0"]
        )
        XCTAssertTrue(
            optionValues("-device", in: arguments)
                .contains("virtio-tablet-pci")
        )
    }

    func testQemuEscapeDoublesCommasAcrossAllFamiliesAndPaths() throws {
        XCTAssertEqual(QEMUManager.qemuEscape("/tmp/A, B/disk.img"), "/tmp/A,, B/disk.img")

        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("ClassicMac,Repo-\(UUID().uuidString)", isDirectory: true)
        let dist = root.appendingPathComponent("dist", isDirectory: true)
        let shared = root.appendingPathComponent("shared", isDirectory: true)
        try FileManager.default.createDirectory(at: dist, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: shared, withIntermediateDirectories: true)
        try Data().write(to: dist.appendingPathComponent("ClassicMacTools.iso"))
        try Data().write(to: shared.appendingPathComponent("ndrvloader"))
        try Data().write(to: shared.appendingPathComponent("declrom"))
        let prevRepo = ProcessInfo.processInfo.environment["CLASSICMAC_REPO"]
        setenv("CLASSICMAC_REPO", root.path, 1)
        defer {
            if let prevRepo { setenv("CLASSICMAC_REPO", prevRepo, 1) }
            else { unsetenv("CLASSICMAC_REPO") }
            try? FileManager.default.removeItem(at: root)
        }

        let bundle = root.appendingPathComponent("My, Mac.classic", isDirectory: true)
        let shareDir = root.appendingPathComponent("Share, Folder", isDirectory: true)

        var powerMac = VMConfig(
            name: "Mac, Nine",
            machineFamily: .powerMacG4,
            cdImagePath: "/tmp/OS, 9.iso",
            bootFromCD: false,
            toolsCDInserted: true,
            sharedFolderPath: shareDir.path,
            bundleURL: bundle
        )
        let pmArgs = QEMUManager.buildArguments(for: powerMac)
        XCTAssertEqual(optionValues("-name", in: pmArgs), ["Mac,, Nine"])
        XCTAssertTrue(optionValues("-drive", in: pmArgs).contains { $0.contains("ClassicMac,,Repo-") && $0.contains("My,, Mac.classic/disk.img") })
        XCTAssertTrue(optionValues("-blockdev", in: pmArgs).contains { $0.contains("ClassicMac,,Repo-") && $0.contains("ClassicMacTools.iso") })
        XCTAssertTrue(optionValues("-device", in: pmArgs).contains { $0.contains("ClassicMac,,Repo-") && $0.contains("ndrvloader") })
        XCTAssertTrue(optionValues("-fsdev", in: pmArgs).contains { $0.contains("Share,, Folder") })

        powerMac.machineFamily = .quadra800
        var quadra = VMConfig(
            name: "Quadra, 800",
            machineFamily: .quadra800,
            cdImagePath: "/tmp/OS, 8.iso",
            bootFromCD: false,
            floppyImagePath: "/tmp/Floppy, Disk.img",
            toolsCDInserted: true,
            sharedFolderPath: shareDir.path,
            bundleURL: bundle
        )
        let qArgs = QEMUManager.buildArguments(for: quadra)
        XCTAssertEqual(optionValues("-name", in: qArgs), ["Quadra,, 800"])
        XCTAssertTrue(optionValues("-drive", in: qArgs).contains { $0.contains("My,, Mac.classic/pram.img") })
        XCTAssertTrue(optionValues("-drive", in: qArgs).contains { $0.contains("My,, Mac.classic/disk.img") })
        XCTAssertTrue(optionValues("-drive", in: qArgs).contains { $0.contains("/tmp/OS,, 8.iso") })
        XCTAssertTrue(optionValues("-drive", in: qArgs).contains { $0.contains("/tmp/Floppy,, Disk.img") })
        XCTAssertTrue(optionValues("-drive", in: qArgs).contains { $0.contains("ClassicMac,,Repo-") && $0.contains("ClassicMacTools.iso") })
        XCTAssertTrue(optionValues("-device", in: qArgs).contains { $0.contains("ClassicMac,,Repo-") && $0.contains("declrom") })
        XCTAssertTrue(optionValues("-fsdev", in: qArgs).contains { $0.contains("Share,, Folder") })

        quadra.name = "Tiger, Mac"
        let osx = VMConfig(
            name: "Tiger, Mac",
            machineFamily: .powerMacOSX,
            cdImagePath: "/tmp/Tiger, Disc.iso",
            bundleURL: bundle
        )
        let osxArgs = QEMUManager.buildArguments(for: osx)
        XCTAssertEqual(optionValues("-name", in: osxArgs), ["Tiger,, Mac"])
        XCTAssertTrue(optionValues("-drive", in: osxArgs).contains { $0.contains("My,, Mac.classic/nvram.img") })
        XCTAssertTrue(optionValues("-drive", in: osxArgs).contains { $0.contains("My,, Mac.classic/disk.img") })
        XCTAssertTrue(optionValues("-drive", in: osxArgs).contains { $0.contains("/tmp/Tiger,, Disc.iso") })
    }

    func testOSXCoreAudioUsesBufferedOutputWhenSoundEnabled() {
        let soundOn = VMConfig(name: "Tiger", machineFamily: .powerMacOSX, sound: true)
        let soundOff = VMConfig(name: "Tiger", machineFamily: .powerMacOSX, sound: false)
        XCTAssertEqual(
            optionValues("-audiodev", in: QEMUManager.buildArguments(for: soundOn)),
            ["coreaudio,id=snd0,out.buffer-length=50000"]
        )
        XCTAssertEqual(
            optionValues("-audiodev", in: QEMUManager.buildArguments(for: soundOff)),
            ["none,id=snd0"]
        )
    }

    func testOSXClassicMacDisplayProbeAndFallback() throws {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("probe-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }

        let supported = dir.appendingPathComponent("qemu-supported")
        try "#!/bin/sh\necho 'Available display backend types:'\necho 'none'\necho 'cocoa'\necho 'classicmac'\n"
            .write(to: supported, atomically: true, encoding: .utf8)
        chmod(supported.path, 0o755)
        XCTAssertTrue(AppPaths.probeClassicMacDisplay(binary: supported))

        let unsupported = dir.appendingPathComponent("qemu-unsupported")
        try "#!/bin/sh\necho 'Available display backend types:'\necho 'none'\necho 'cocoa'\n"
            .write(to: unsupported, atomically: true, encoding: .utf8)
        chmod(unsupported.path, 0o755)
        XCTAssertFalse(AppPaths.probeClassicMacDisplay(binary: unsupported))
    }

    func testRuntimeSocketDirRejectsSymlinksAndFitsSunPath() throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("sock-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let target = root.appendingPathComponent("real", isDirectory: true)
        try FileManager.default.createDirectory(at: target, withIntermediateDirectories: true)
        let linkURL = root.appendingPathComponent("link", isDirectory: false)
        XCTAssertEqual(Darwin.symlink(target.path, linkURL.path), 0)

        let fallback = URL(fileURLWithPath: NSTemporaryDirectory(), isDirectory: true)
            .appendingPathComponent("cm-test-\(getuid())", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: fallback) }

        let chosen = AppPaths.verifiedRuntimeSocketDir(primary: linkURL, fallback: fallback)
        XCTAssertNotEqual(chosen.path, linkURL.path)
        let longestSocket = chosen.appendingPathComponent("\(UUID().uuidString).qmp.sock").path
        XCTAssertLessThan(longestSocket.utf8.count, 104)
    }

    func testHMPClientPerSocketLockDoesNotBlockOtherVMs() async throws {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("hmp-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }

        let slowPath = dir.appendingPathComponent("slow.sock").path
        let fastPath = dir.appendingPathComponent("fast.sock").path

        func listenUnix(_ path: String) -> Int32 {
            let fd = socket(AF_UNIX, SOCK_STREAM, 0)
            var addr = sockaddr_un()
            addr.sun_family = sa_family_t(AF_UNIX)
            let maxLen = MemoryLayout.size(ofValue: addr.sun_path) - 1
            withUnsafeMutablePointer(to: &addr.sun_path) {
                $0.withMemoryRebound(to: CChar.self, capacity: maxLen + 1) { dest in
                    _ = path.withCString { src in strncpy(dest, src, maxLen) }
                }
            }
            _ = withUnsafePointer(to: &addr) {
                $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                    Darwin.bind(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
                }
            }
            listen(fd, 2)
            return fd
        }

        let slowFD = listenUnix(slowPath)
        let fastFD = listenUnix(fastPath)
        defer { close(slowFD); close(fastFD) }

        // Slow VM accepts connection and stalls (never sends prompt).
        let slowServer = Task.detached {
            let client = accept(slowFD, nil, nil)
            if client >= 0 {
                try? await Task.sleep(nanoseconds: 1_200_000_000)
                close(client)
            }
        }
        // Fast VM immediately sends prompt, reads command, sends prompt.
        let fastServer = Task.detached {
            let client = accept(fastFD, nil, nil)
            if client >= 0 {
                let prompt = Data("(qemu) ".utf8)
                _ = prompt.withUnsafeBytes { write(client, $0.baseAddress, prompt.count) }
                var buf = [UInt8](repeating: 0, count: 256)
                _ = recv(client, &buf, buf.count, 0)
                let reply = Data("ok\r\n(qemu) ".utf8)
                _ = reply.withUnsafeBytes { write(client, $0.baseAddress, reply.count) }
                close(client)
            }
        }

        let slowCall = Task.detached {
            HMPClient.command("info status", socketPath: slowPath)
        }
        try await Task.sleep(nanoseconds: 50_000_000)

        let startedFast = ProcessInfo.processInfo.systemUptime
        let fastReply = await Task.detached {
            HMPClient.command("info status", socketPath: fastPath)
        }.value
        let elapsedFast = ProcessInfo.processInfo.systemUptime - startedFast

        XCTAssertNotNil(fastReply)
        XCTAssertLessThan(elapsedFast, 0.8, "Hung VM socket must not serialize unrelated VMs")
        _ = await slowCall.value
        await slowServer.value
        await fastServer.value
    }

    func testBootClockHandoffCancelsWithoutWaiting() {
        let handoff = BootClockHandoff(socketPath: "/tmp/nonexistent-\(UUID().uuidString).sock")
        let started = ProcessInfo.processInfo.systemUptime
        handoff.start(delay: 15.0)
        handoff.cancel()
        XCTAssertLessThan(ProcessInfo.processInfo.systemUptime - started, 0.1)
    }

    func testForceStopSuppressesCrashErrorAndUncaughtSignalReportsSignalInLog() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("qemu-proc-\(UUID().uuidString)", isDirectory: true)
        let osxFirmware = root.appendingPathComponent("osx/firmware", isDirectory: true)
        let osxBinDir = root.appendingPathComponent("vendor/qemu-osx/build", isDirectory: true)
        let bundle = root.appendingPathComponent("SignalTest.classic", isDirectory: true)
        try FileManager.default.createDirectory(at: osxFirmware, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: osxBinDir, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: bundle, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        try Data().write(to: osxFirmware.appendingPathComponent("openbios-ppc-osx"))
        try Data(repeating: 0, count: 16_384).write(to: osxFirmware.appendingPathComponent("macosx-flash-seed.img"))
        try Data().write(to: osxFirmware.appendingPathComponent("qemu_vga.ndrv"))

        let fakeQemu = osxBinDir.appendingPathComponent("qemu-system-ppc")
        let prevRepo = ProcessInfo.processInfo.environment["CLASSICMAC_REPO"]
        let prevUseWindow = UserDefaults.standard.object(forKey: "ClassicMacUseQEMUWindow")
        UserDefaults.standard.set(true, forKey: "ClassicMacUseQEMUWindow")
        setenv("CLASSICMAC_REPO", root.path, 1)
        defer {
            if let prevRepo { setenv("CLASSICMAC_REPO", prevRepo, 1) }
            else { unsetenv("CLASSICMAC_REPO") }
            if let prevUseWindow { UserDefaults.standard.set(prevUseWindow, forKey: "ClassicMacUseQEMUWindow") }
            else { UserDefaults.standard.removeObject(forKey: "ClassicMacUseQEMUWindow") }
        }

        let vm = VMConfig(name: "Signal Test", machineFamily: .powerMacOSX, bundleURL: bundle)
        try Data(repeating: 0, count: 1024).write(to: vm.diskImageURL)

        // 1. Intentional forceStop sends SIGTERM (and escalates to SIGKILL if needed) without surfacing lastError.
        try "#!/bin/sh\nif [ \"$1\" = \"-display\" ] && [ \"$2\" = \"help\" ]; then echo 'cocoa'; exit 0; fi\nsleep 30\n"
            .write(to: fakeQemu, atomically: true, encoding: .utf8)
        chmod(fakeQemu.path, 0o755)

        let manager = QEMUManager()
        manager.start(vm)
        XCTAssertTrue(manager.isRunning(vm.id))
        manager.forceStop(vm.id)
        for _ in 0..<40 where manager.isRunning(vm.id) {
            try await Task.sleep(nanoseconds: 50_000_000)
        }
        XCTAssertFalse(manager.isRunning(vm.id))
        XCTAssertNil(manager.lastError)

        // 2. Unexpected uncaught signal reports lastError and writes signal details to AppLog.
        try "#!/bin/sh\nif [ \"$1\" = \"-display\" ] && [ \"$2\" = \"help\" ]; then echo 'cocoa'; exit 0; fi\nkill -ABRT $$\n"
            .write(to: fakeQemu, atomically: true, encoding: .utf8)
        chmod(fakeQemu.path, 0o755)

        manager.start(vm)
        for _ in 0..<40 where manager.isRunning(vm.id) {
            try await Task.sleep(nanoseconds: 50_000_000)
        }
        try await Task.sleep(nanoseconds: 100_000_000)
        XCTAssertFalse(manager.isRunning(vm.id))
        let error = try XCTUnwrap(manager.lastError)
        let logURL = try XCTUnwrap(error.logURL)
        defer { try? FileManager.default.removeItem(at: logURL) }
        let logText = try String(contentsOf: logURL, encoding: .utf8)
        XCTAssertTrue(logText.contains("signal \(SIGABRT)"), "Unexpected log text: \(logText)")
    }

    func testCDisplaySocketCreatesOwnerOnlySocket() throws {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("cds-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }

        let sockPath = dir.appendingPathComponent("d.sock").path
        let fd = cds_listen(sockPath)
        XCTAssertGreaterThanOrEqual(fd, 0)
        defer {
            cds_close(fd)
            unlink(sockPath)
        }

        var st = stat()
        XCTAssertEqual(lstat(sockPath, &st), 0)
        XCTAssertEqual(st.st_mode & 0o777, 0o600)
    }
}
