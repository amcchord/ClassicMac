import XCTest
import Darwin
@testable import ClassicMac

@MainActor
final class QEMURunnerArgumentTests: XCTestCase {
    private func config(
        family: MachineFamily = .powerMacG4,
        cdImagePath: String? = "/tmp/install.iso",
        bootFromCD: Bool = true,
        toolsCDInserted: Bool = true,
        networking: Bool = true,
        tabletInput: Bool = true,
        sharedFolderPath: String? = nil
    ) -> VMConfig {
        VMConfig(
            name: "Argument Test",
            machineFamily: family,
            ramMB: family.defaultRAMMB,
            cdImagePath: cdImagePath,
            bootFromCD: bootFromCD,
            toolsCDInserted: toolsCDInserted,
            networking: networking,
            sound: false,
            tabletInput: tabletInput,
            sharedFolderPath: sharedFolderPath,
            bundleURL: URL(fileURLWithPath: "/tmp/argument-test.classic")
        )
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
            ["mac99,via=pmu-adb,audiodev=snd0"]
        )
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

    func testPowerMacCDStartupWithoutTabletUsesRelativeMouse() {
        let arguments = QEMUManager.buildArguments(
            for: config(tabletInput: false)
        )
        let devices = optionValues("-device", in: arguments)

        XCTAssertEqual(
            optionValues("-M", in: arguments),
            ["mac99,via=pmu,audiodev=snd0"]
        )
        XCTAssertFalse(devices.contains("virtio-tablet-pci"))
        XCTAssertFalse(
            devices.contains { $0.hasPrefix("loader,addr=0x4000000,file=") }
        )
        XCTAssertFalse(
            optionValues("-prom-env", in: arguments)
                .contains("boot-command=init-program go")
        )
    }

    func testPowerMacNormalStartupKeepsTabletInputEnabled() {
        let arguments = QEMUManager.buildArguments(
            for: config(bootFromCD: false, tabletInput: true)
        )

        XCTAssertEqual(
            optionValues("-M", in: arguments),
            ["mac99,via=pmu-adb,audiodev=snd0"]
        )
        XCTAssertTrue(
            optionValues("-device", in: arguments)
                .contains("virtio-tablet-pci")
        )
    }
}
