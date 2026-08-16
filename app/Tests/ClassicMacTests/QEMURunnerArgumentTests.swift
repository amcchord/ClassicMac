import XCTest
import Darwin
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
        sharedFolderPath: String? = nil
    ) -> VMConfig {
        VMConfig(
            name: "Argument Test",
            machineFamily: family,
            ramMB: family.defaultRAMMB,
            depth: depth,
            cdImagePath: cdImagePath,
            bootFromCD: bootFromCD,
            floppyImagePath: floppyImagePath,
            toolsCDInserted: toolsCDInserted,
            networking: networking,
            sound: false,
            useG4CPU: useG4CPU,
            tabletInput: tabletInput,
            sharedFolderPath: sharedFolderPath,
            bundleURL: URL(fileURLWithPath: "/tmp/argument-test.classic")
        )
    }

    func testPowerMacGraphicsAccelerationOptionsAndBootDepth() {
        let thousands = QEMUManager.buildArguments(for: config())
        let millions = QEMUManager.buildArguments(
            for: config(depth: ColorDepth.millions.rawValue)
        )

        XCTAssertTrue(optionValues("-global", in: thousands).contains("VGA.hardware-cursor=on"))
        XCTAssertTrue(optionValues("-global", in: thousands).contains("VGA.untracked-vram=on"))
        XCTAssertEqual(optionValues("-cpu", in: thousands), ["7400"])
        XCTAssertEqual(optionValues("-g", in: thousands), ["1024x768x15"])
        XCTAssertEqual(optionValues("-g", in: millions), ["1024x768x32"])
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

    func testPowerMacCDStartupKeepsToolsTrayEmpty() throws {
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
            XCTAssertEqual(
                optionValues("-blockdev", in: arguments),
                [
                    "driver=file,node-name=classicmac-cd-file,filename=/tmp/install.iso,read-only=on",
                    "driver=raw,node-name=classicmac-cd-boot,file=classicmac-cd-file,read-only=on"
                ]
            )
            XCTAssertTrue(
                optionValues("-device", in: arguments)
                    .contains("virtio-blk-pci,drive=classicmac-cd-boot")
            )
        }
    }

    func testPowerMacNormalStartupLoadsRequestedToolsCD() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(for: config(bootFromCD: false))
            let toolsDrive = try XCTUnwrap(
                optionValues("-drive", in: arguments).first { $0.contains("id=tools0") }
            )

            XCTAssertTrue(
                toolsDrive.hasPrefix(
                    "if=ide,index=2,media=cdrom,id=tools0,file="
                )
            )
            XCTAssertTrue(toolsDrive.hasSuffix(",format=raw,readonly=on"))
            XCTAssertFalse(
                optionValues("-device", in: arguments)
                    .contains { $0.contains("classicmac-tools") }
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
        XCTAssertTrue(
            optionValues("-blockdev", in: arguments).contains(
                "driver=file,node-name=classicmac-cd-file,filename=/tmp/Mac OS 8,,5.iso,read-only=on"
            )
        )
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
        XCTAssertTrue(optionValues("-boot", in: arguments).isEmpty)
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

    func testPowerMacCDStartupWithoutTabletUsesCUDAMouseAndBootLoader() {
        let arguments = QEMUManager.buildArguments(
            for: config(tabletInput: false)
        )
        let devices = optionValues("-device", in: arguments)

        XCTAssertEqual(
            optionValues("-M", in: arguments),
            ["mac99,via=cuda,audiodev=snd0"]
        )
        XCTAssertFalse(devices.contains("virtio-tablet-pci"))
        XCTAssertTrue(
            devices.contains { $0.hasPrefix("loader,addr=0x4000000,file=") }
        )
        XCTAssertTrue(
            optionValues("-prom-env", in: arguments)
                .contains("boot-command=init-program go")
        )
        XCTAssertTrue(
            optionValues("-prom-env", in: arguments)
                .contains("boot-device=virtio0:\\\\:tbxi")
        )
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
}
