import XCTest
import Darwin
@testable import ClassicMac

@MainActor
final class QEMURunnerArgumentTests: XCTestCase {
    private func config(
        family: MachineFamily = .powerMacG4,
        bootFromCD: Bool = true,
        toolsCDInserted: Bool = true,
        networking: Bool = true
    ) -> VMConfig {
        VMConfig(
            name: "Argument Test",
            machineFamily: family,
            ramMB: family.defaultRAMMB,
            cdImagePath: "/tmp/install.iso",
            bootFromCD: bootFromCD,
            toolsCDInserted: toolsCDInserted,
            networking: networking,
            sound: false,
            tabletInput: false,
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

            XCTAssertEqual(toolsDrive, "if=ide,media=cdrom,id=tools0")
            XCTAssertTrue(optionValues("-nic", in: arguments).contains("user,model=sungem"))
        }
    }

    func testPowerMacNormalStartupLoadsRequestedToolsCD() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(for: config(bootFromCD: false))
            let toolsDrive = try XCTUnwrap(
                optionValues("-drive", in: arguments).first { $0.contains("id=tools0") }
            )

            XCTAssertTrue(toolsDrive.contains("file="))
            XCTAssertTrue(toolsDrive.hasSuffix(",format=raw"))
        }
    }

    func testPowerMacCDStartupCanLoadToolsWhenNetworkingIsOff() throws {
        try withTemporaryToolsCD {
            let arguments = QEMUManager.buildArguments(
                for: config(networking: false)
            )
            let toolsDrive = try XCTUnwrap(
                optionValues("-drive", in: arguments).first { $0.contains("id=tools0") }
            )

            XCTAssertTrue(toolsDrive.contains("file="))
            XCTAssertEqual(optionValues("-nic", in: arguments), ["none"])
        }
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
}
