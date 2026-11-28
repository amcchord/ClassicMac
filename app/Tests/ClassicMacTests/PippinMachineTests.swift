import Carbon.HIToolbox
import XCTest
@testable import ClassicMac

final class PippinMachineTests: XCTestCase {
    func testPippinConfigSanitizeAndEncodeDecodeRoundTrip() throws {
        var config = VMConfig(
            name: "  Pippin Test  ",
            machineFamily: .pippin,
            ramMB: 99,
            diskSizeGB: 32,
            cdImagePath: "/tmp/game.iso",
            networking: true,
            sound: true,
            printingEnabled: true,
            tabletInput: true,
            classicInputHelpers: true,
            sharedFolderPath: "/tmp/share",
            pippinVideoOut: .pal,
            flashImageName: "save.img",
            pippinROMPath: " /tmp/kinka13.rom "
        )
        XCTAssertEqual(config.name, "Pippin Test")
        XCTAssertEqual(config.ramMB, PippinMachine.maxRAMMB)
        XCTAssertEqual(config.width, 640)
        XCTAssertEqual(config.height, 512)
        XCTAssertEqual(config.effectivePippinVideoOut, .pal)
        XCTAssertEqual(config.flashImageName, "save.img")
        XCTAssertEqual(config.pippinROMPath, "/tmp/kinka13.rom")
        XCTAssertFalse(config.networking)
        XCTAssertFalse(config.printingEnabled)
        XCTAssertFalse(config.tabletInput)
        XCTAssertNil(config.sharedFolderPath)
        XCTAssertTrue(config.bootFromCD)

        config.ramMB = 5
        let encoded = try JSONEncoder().encode(config)
        let decoded = try JSONDecoder().decode(VMConfig.self, from: encoded)
        XCTAssertEqual(decoded.machineFamily, .pippin)
        XCTAssertEqual(decoded.ramMB, 5)
        XCTAssertEqual(decoded.pippinVideoOut, .pal)
        XCTAssertEqual(decoded.flashImageName, "save.img")
        XCTAssertEqual(decoded.pippinROMPath, "/tmp/kinka13.rom")
    }

    func testLegacyConfigWithoutPippinKeysDecodesCleanly() throws {
        let legacyJSON = """
        {
          "id": "11111111-1111-1111-1111-111111111111",
          "name": "Old G4",
          "machineFamily": "powerMacG4",
          "ramMB": 256,
          "diskImageName": "disk.img",
          "pramImageName": "pram.img",
          "diskSizeGB": 16,
          "width": 1024,
          "height": 768,
          "depth": 16,
          "useEnhancedFramebuffer": false,
          "customResolution": false,
          "useBrowserDisplay": false,
          "bootFromCD": false,
          "bootFromFloppy": false,
          "networking": true,
          "sound": true,
          "printingEnabled": false,
          "useG4CPU": true,
          "useVirtioDisk": false
        }
        """.data(using: .utf8)!

        let decoded = try JSONDecoder().decode(VMConfig.self, from: legacyJSON)
        XCTAssertEqual(decoded.machineFamily, .powerMacG4)
        XCTAssertNil(decoded.pippinVideoOut)
        XCTAssertNil(decoded.flashImageName)
        XCTAssertNil(decoded.pippinROMPath)
    }

    func testROMValidationAndPersistenceImages() throws {
        let fm = FileManager.default
        let tmp = fm.temporaryDirectory.appendingPathComponent("pippin-test-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: tmp) }
        try fm.createDirectory(at: tmp, withIntermediateDirectories: true)

        let badROM = tmp.appendingPathComponent("short.rom")
        try Data(repeating: 0x00, count: 1024).write(to: badROM)
        XCTAssertThrowsError(try PippinMachine.validateROM(at: badROM))

        let goodROM = tmp.appendingPathComponent("pippin.rom")
        try Data(repeating: 0xAA, count: PippinMachine.romByteCount).write(to: goodROM)
        let label = try PippinMachine.validateROM(at: goodROM)
        XCTAssertEqual(label, "Pippin Boot ROM (4 MB)")

        let bundle = tmp.appendingPathComponent("Test.classic", isDirectory: true)
        let cfg = VMConfig(
            name: "Pippin",
            machineFamily: .pippin,
            ramMB: 13,
            pippinVideoOut: .ntsc,
            bundleURL: bundle
        )
        try PippinMachine.ensurePersistenceImages(for: cfg)

        let flashData = try Data(contentsOf: cfg.flashImageURL)
        XCTAssertEqual(flashData.count, PippinMachine.flashByteCount)
        XCTAssertTrue(flashData.allSatisfy { $0 == 0xFF })

        let nvramData = try Data(contentsOf: cfg.nvramImageURL)
        XCTAssertEqual(nvramData.count, PippinMachine.nvramByteCount)
        XCTAssertTrue(nvramData.allSatisfy { $0 == 0x00 })

        let pramData = try Data(contentsOf: cfg.pramImageURL)
        XCTAssertEqual(pramData.count, PippinMachine.pramByteCount)
    }

    func testConvertRaw2352BinToISO() throws {
        let fm = FileManager.default
        let tmp = fm.temporaryDirectory.appendingPathComponent("pippin-bin-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: tmp) }
        try fm.createDirectory(at: tmp, withIntermediateDirectories: true)

        // Build two 2352-byte Mode 1 raw sectors (sync header + mode=1 + 2048 payload)
        var raw = Data()
        for sectorIdx in 0..<2 {
            var block = Data(repeating: 0, count: 2352)
            block.replaceSubrange(0..<12, with: [0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00])
            block[15] = 0x01 // Mode 1 -> user data at offset 16..2064
            block.replaceSubrange(16..<(16 + 2048), with: Data(repeating: UInt8(0x10 + sectorIdx), count: 2048))
            raw.append(block)
        }

        let binURL = tmp.appendingPathComponent("disc.bin")
        let isoURL = tmp.appendingPathComponent("disc.iso")
        try raw.write(to: binURL)
        XCTAssertTrue(PippinMachine.isRaw2352CDImage(at: binURL))

        try PippinMachine.convertRawBinToISO(from: binURL, to: isoURL)
        let iso = try Data(contentsOf: isoURL)
        XCTAssertEqual(iso.count, 4096)
        XCTAssertEqual(iso[0], 0x10)
        XCTAssertEqual(iso[2047], 0x10)
        XCTAssertEqual(iso[2048], 0x11)
        XCTAssertEqual(iso[4095], 0x11)
    }

    @MainActor
    func testBuildArgumentsWithAndWithoutCD() throws {
        let tmp = FileManager.default.temporaryDirectory
            .appendingPathComponent("pippin-args-\(UUID().uuidString).classic", isDirectory: true)
        let rom = tmp.appendingPathComponent("pippin.rom")

        let withCD = VMConfig(
            name: "Marathon, Pippin",
            machineFamily: .pippin,
            ramMB: 13,
            cdImagePath: "/tmp/Super_Marathon.iso",
            sound: true,
            pippinVideoOut: .vga,
            pippinROMPath: rom.path,
            bundleURL: tmp
        )
        let args = QEMUManager.buildArguments(for: withCD)
        XCTAssertTrue(args.contains("tcg,tb-size=512"))
        XCTAssertTrue(args.contains("pippin,video-out=vga,audiodev=snd0"))
        XCTAssertTrue(args.contains("coreaudio,id=snd0,out.buffer-length=50000"))
        XCTAssertTrue(args.contains("file=/tmp/Super_Marathon.iso,if=none,id=cd0,media=cdrom,readonly=on,format=raw"))
        XCTAssertTrue(args.contains(PippinMachine.scsiCDDeviceSpec))
        XCTAssertTrue(args.contains("Marathon,, Pippin"))

        let noCD = VMConfig(
            name: "Empty Pippin",
            machineFamily: .pippin,
            ramMB: 5,
            cdImagePath: nil,
            sound: false,
            pippinVideoOut: .ntsc,
            bundleURL: tmp
        )
        let noCDArgs = QEMUManager.buildArguments(for: noCD)
        XCTAssertTrue(noCDArgs.contains("pippin,video-out=ntsc,audiodev=snd0"))
        XCTAssertTrue(noCDArgs.contains("none,id=snd0"))
        XCTAssertTrue(noCDArgs.contains("if=none,id=cd0,media=cdrom,readonly=on"))
        XCTAssertTrue(noCDArgs.contains(PippinMachine.scsiCDDeviceSpec))
    }

    func testGuestBootFramePippinLogoDetection() {
        let width = 640, height = 480
        let black = [UInt8](repeating: 0, count: width * height * 4)
        let blackReady = black.withUnsafeBytes {
            GuestBootFrame.isReady($0.baseAddress!, width: width, height: height, stride: width * 4, family: .pippin)
        }
        XCTAssertFalse(blackReady)

        // Centered white/silver logo patch over pure black background
        var splash = black
        for y in 200..<280 {
            for x in 260..<380 {
                let offset = (y * width + x) * 4
                splash[offset] = 220
                splash[offset + 1] = 220
                splash[offset + 2] = 220
            }
        }
        let splashReady = splash.withUnsafeBytes {
            GuestBootFrame.isReady($0.baseAddress!, width: width, height: height, stride: width * 4, family: .pippin)
        }
        XCTAssertTrue(splashReady)
    }

    func testCheckpointCreateAndRestoreForPippin() throws {
        let fm = FileManager.default
        let bundle = fm.temporaryDirectory.appendingPathComponent("pippin-cp-\(UUID().uuidString).classic", isDirectory: true)
        defer { try? fm.removeItem(at: bundle) }

        let config = VMConfig(name: "Pippin CP", machineFamily: .pippin, bundleURL: bundle)
        try PippinMachine.ensurePersistenceImages(for: config)

        let cp = try VMCheckpointStore.create(for: config, label: "Clean Flash")
        XCTAssertEqual(cp.diskBytes, Int64(PippinMachine.flashByteCount))
        XCTAssertEqual(VMCheckpointStore.list(in: bundle).count, 1)

        // Overwrite live flash with zeroes, then restore checkpoint
        try Data(repeating: 0x00, count: PippinMachine.flashByteCount).write(to: config.flashImageURL)
        try VMCheckpointStore.restore(cp.id, for: config, createBackup: false)

        let restoredFlash = try Data(contentsOf: config.flashImageURL)
        XCTAssertTrue(restoredFlash.allSatisfy { $0 == 0xFF })
    }

    func testPippinControlConfigRoundTripDeduplicationAndClamping() throws {
        var controls = PippinControlConfig.defaultConfig
        XCTAssertEqual(controls.keyCode(for: .blue), UInt16(kVK_ANSI_W))
        XCTAssertEqual(controls.button(forKeyCode: UInt16(kVK_ANSI_W)), .blue)
        XCTAssertEqual(controls.gamepadInput(for: .green), .buttonB)

        // Assigning a key or gamepad input already bound elsewhere clears the duplicate.
        controls.setKeyCode(UInt16(kVK_ANSI_W), for: .red)
        XCTAssertNil(controls.keyCode(for: .blue))
        XCTAssertEqual(controls.keyCode(for: .red), UInt16(kVK_ANSI_W))

        controls.setGamepadInput(.buttonB, for: .yellow)
        XCTAssertNil(controls.gamepadInput(for: .green))
        XCTAssertEqual(controls.gamepadInput(for: .yellow), .buttonB)

        controls.stickMode = .leftDpadRightTrackball
        controls.stickSensitivity = 9.0
        controls.stickDeadzone = 0.01

        let config = VMConfig(name: "Pippin Pads", machineFamily: .pippin, pippinControls: controls)
        XCTAssertEqual(config.pippinControls?.stickSensitivity, PippinControlConfig.maxSensitivity)
        XCTAssertEqual(config.pippinControls?.stickDeadzone, PippinControlConfig.minDeadzone)

        let data = try JSONEncoder().encode(config)
        let jsonObject = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let encodedControls = try XCTUnwrap(jsonObject["pippinControls"] as? [String: Any])
        XCTAssertTrue(encodedControls["keyboardBindings"] is [String: Any])
        XCTAssertTrue(encodedControls["gamepadBindings"] is [String: Any])

        let decoded = try JSONDecoder().decode(VMConfig.self, from: data)
        XCTAssertEqual(decoded.pippinControls, config.pippinControls)

        // Non-Pippin machines clear pippinControls during sanitize.
        let g4 = VMConfig(name: "G4", machineFamily: .powerMacG4, pippinControls: controls)
        XCTAssertNil(g4.pippinControls)
    }

    @MainActor
    func testPippinButtonBitMaskAndStickMath() {
        let allMask = PippinButton.allCases.reduce(UInt32(0)) { $0 | $1.bitMask }
        XCTAssertEqual(allMask, 0x3FF07)
        XCTAssertEqual(PippinButton.square.bitMask, 1 << 0)
        XCTAssertEqual(PippinButton.circle.bitMask, 1 << 1)
        XCTAssertEqual(PippinButton.triangle.bitMask, 1 << 2)
        XCTAssertEqual(PippinButton.blue.bitMask, 1 << 8)
        XCTAssertEqual(PippinButton.yellow.bitMask, 1 << 9)
        XCTAssertEqual(PippinButton.dpadUp.bitMask, 1 << 10)
        XCTAssertEqual(PippinButton.dpadLeft.bitMask, 1 << 11)
        XCTAssertEqual(PippinButton.dpadRight.bitMask, 1 << 12)
        XCTAssertEqual(PippinButton.dpadDown.bitMask, 1 << 13)
        XCTAssertEqual(PippinButton.red.bitMask, 1 << 14)
        XCTAssertEqual(PippinButton.green.bitMask, 1 << 15)
        XCTAssertEqual(PippinButton.rightTrigger.bitMask, 1 << 16)
        XCTAssertEqual(PippinButton.leftTrigger.bitMask, 1 << 17)

        let merged = PippinControllerManager.mergedMask(
            keyboardMask: PippinButton.dpadUp.bitMask | 0x8000_0000,
            gamepadMask: PippinButton.red.bitMask
        )
        XCTAssertEqual(merged, PippinButton.dpadUp.bitMask | PippinButton.red.bitMask)

        // Within radial deadzone -> zero motion
        let zeroDelta = PippinControllerManager.stickTrackballDelta(x: 0.1, y: 0.05, deadzone: 0.15, sensitivity: 1.0)
        XCTAssertEqual(zeroDelta.dx, 0)
        XCTAssertEqual(zeroDelta.dy, 0)

        // Full right + up flips Y for screen coordinates
        let fullDelta = PippinControllerManager.stickTrackballDelta(x: 1.0, y: 0.0, deadzone: 0.15, sensitivity: 1.0)
        XCTAssertEqual(fullDelta.dx, 14.0, accuracy: 0.001)
        XCTAssertEqual(fullDelta.dy, 0.0, accuracy: 0.001)

        let upDelta = PippinControllerManager.stickTrackballDelta(x: 0.0, y: 1.0, deadzone: 0.15, sensitivity: 1.0)
        XCTAssertEqual(upDelta.dx, 0.0, accuracy: 0.001)
        XCTAssertEqual(upDelta.dy, -14.0, accuracy: 0.001)
    }

    func testDisplaySessionSendsGamepadMessage() throws {
        XCTAssertEqual(DisplayMessage.gamepad.rawValue, 23)
    }

    func testKnownPippinROMSHA1Labels() throws {
        XCTAssertEqual(PippinMachine.knownROMSHA1[PippinMachine.kinka13SHA1], "Pippin Kinka 1.3 (4 MB)")
        XCTAssertEqual(PippinMachine.knownROMSHA1["6478ee9e8d5cfe3b00f699f7072482210e00c5cc"], "Pippin Kinka 1.0 (4 MB)")
        XCTAssertEqual(PippinMachine.knownROMSHA1["eb8739cab1807c6c7c51acc7f4a3afc1f9c6ddbb"], "Pippin Kinka Dev (4 MB)")
        XCTAssertEqual(PippinMachine.archiveOrgROMURLs.count, 2)
        for url in PippinMachine.archiveOrgROMURLs {
            XCTAssertEqual(url.scheme, "https")
            XCTAssertEqual(url.host, "archive.org")
        }
    }

    func testDownloadKinka13SuccessWithStubAndProgress() async throws {
        let fm = FileManager.default
        let tmp = fm.temporaryDirectory.appendingPathComponent("pippin-dl-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: tmp) }
        try fm.createDirectory(at: tmp, withIntermediateDirectories: true)

        let payload = Data(repeating: 0x5A, count: PippinMachine.romByteCount)
        let sha256 = PippinMachine.romSHA256(data: payload)
        let url = URL(string: "https://archive.example.org/pippin-kinka13.rom")!
        let session = StubURLProtocol.session(responses: [
            url: .ok(data: payload),
        ])

        final class PhaseRecorder: @unchecked Sendable {
            private let lock = NSLock()
            var phases: [PippinROMDownloader.DownloadPhase] = []
            func record(_ phase: PippinROMDownloader.DownloadPhase) {
                lock.lock()
                phases.append(phase)
                lock.unlock()
            }
        }
        let recorder = PhaseRecorder()
        let dest = tmp.appendingPathComponent("ROMs/pippin.rom")

        let installed = try await PippinROMDownloader.downloadKinka13(
            to: dest,
            mirrors: [url],
            expectedSHA256: sha256,
            session: session
        ) { phase in
            recorder.record(phase)
        }

        XCTAssertEqual(installed, dest)
        XCTAssertEqual(try Data(contentsOf: dest), payload)
        XCTAssertEqual(recorder.phases.first, .connecting)
        XCTAssertTrue(recorder.phases.contains(.verifying))
    }

    func testDownloadKinka13FallsBackToSecondMirror() async throws {
        let fm = FileManager.default
        let tmp = fm.temporaryDirectory.appendingPathComponent("pippin-fallback-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: tmp) }
        try fm.createDirectory(at: tmp, withIntermediateDirectories: true)

        let payload = Data(repeating: 0x3C, count: PippinMachine.romByteCount)
        let sha256 = PippinMachine.romSHA256(data: payload)
        let badURL = URL(string: "https://archive.example.org/mirror1.rom")!
        let goodURL = URL(string: "https://archive.example.org/mirror2.rom")!
        let session = StubURLProtocol.session(responses: [
            badURL: .status(503),
            goodURL: .ok(data: payload),
        ])

        let dest = tmp.appendingPathComponent("pippin.rom")
        let installed = try await PippinROMDownloader.downloadKinka13(
            to: dest,
            mirrors: [badURL, goodURL],
            expectedSHA256: sha256,
            session: session
        )
        XCTAssertEqual(installed, dest)
        XCTAssertEqual(try Data(contentsOf: dest), payload)
    }

    func testDownloadKinka13RejectsChecksumSizeStatusAndInsecureURL() async throws {
        let fm = FileManager.default
        let tmp = fm.temporaryDirectory.appendingPathComponent("pippin-reject-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: tmp) }
        try fm.createDirectory(at: tmp, withIntermediateDirectories: true)

        let full = Data(repeating: 0x7E, count: PippinMachine.romByteCount)
        let wrongDigest = String(repeating: "0", count: 64)
        let badDigestURL = URL(string: "https://archive.example.org/bad-digest.rom")!
        let shortURL = URL(string: "https://archive.example.org/short.rom")!
        let missingURL = URL(string: "https://archive.example.org/404.rom")!
        let httpURL = URL(string: "http://archive.example.org/insecure.rom")!

        let session = StubURLProtocol.session(responses: [
            badDigestURL: .ok(data: full),
            shortURL: .ok(data: Data(repeating: 0x7E, count: 2048)),
            missingURL: .status(404),
        ])

        for url in [badDigestURL, shortURL, missingURL, httpURL] {
            let dest = tmp.appendingPathComponent("\(UUID().uuidString).rom")
            do {
                _ = try await PippinROMDownloader.downloadKinka13(
                    to: dest,
                    mirrors: [url],
                    expectedSHA256: wrongDigest,
                    session: session
                )
                XCTFail("Expected downloadKinka13 to reject \(url)")
            } catch {
                XCTAssertFalse(fm.fileExists(atPath: dest.path))
                let dirFiles = try fm.contentsOfDirectory(atPath: tmp.path)
                XCTAssertTrue(dirFiles.allSatisfy { !$0.contains(".part.") })
            }
        }
    }
}

private final class StubURLProtocol: URLProtocol {
    enum StubResponse {
        case ok(data: Data)
        case status(Int)
    }

    private static let lock = NSLock()
    nonisolated(unsafe) private static var registeredResponses: [URL: StubResponse] = [:]

    static func session(responses: [URL: StubResponse]) -> URLSession {
        lock.lock()
        registeredResponses.merge(responses) { _, new in new }
        lock.unlock()
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [StubURLProtocol.self]
        return URLSession(configuration: config)
    }

    override class func canInit(with request: URLRequest) -> Bool {
        true
    }

    override class func canonicalRequest(for request: URLRequest) -> URLRequest {
        request
    }

    override func startLoading() {
        guard let url = request.url else {
            client?.urlProtocol(self, didFailWithError: URLError(.badURL))
            return
        }
        Self.lock.lock()
        let stub = Self.registeredResponses[url]
        Self.lock.unlock()

        switch stub {
        case .ok(let data):
            let response = HTTPURLResponse(
                url: url,
                statusCode: 200,
                httpVersion: "HTTP/1.1",
                headerFields: ["Content-Length": String(data.count)]
            )!
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: data)
            client?.urlProtocolDidFinishLoading(self)
        case .status(let code):
            let response = HTTPURLResponse(
                url: url,
                statusCode: code,
                httpVersion: "HTTP/1.1",
                headerFields: [:]
            )!
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocolDidFinishLoading(self)
        case nil:
            client?.urlProtocol(self, didFailWithError: URLError(.fileDoesNotExist))
        }
    }

    override func stopLoading() {}
}
