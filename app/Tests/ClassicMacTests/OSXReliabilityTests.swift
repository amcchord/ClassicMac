import CoreGraphics
import XCTest
@testable import ClassicMac

final class OSXReliabilityTests: XCTestCase {
    // MARK: - NVRAM helpers

    static func makeNVRAMBank(
        generation: UInt32 = 1,
        xpramOffset: Int? = 0,
        corruptHeader: Bool = false
    ) -> Data {
        var bank = Data(repeating: 0, count: OSXNVRAM.bankSize)

        // Partition 1: "APL,MacOS75" at offset 0 (length = 32 * 16 = 512 bytes),
        // or starts at offset 32 (after CHRP header) like real OpenBIOS flash.
        // Offset 0..32 is the 16-byte CHRP header + 16 bytes reserved/header padding,
        // and the first partition starts at offset 0x20? Actually in osx.py:
        // rtc_offset walks partitions starting from offset 0!
        // Look at macosx-flash-seed.img: offset 0 has length=32 (name="wwwwwwwwwwww"),
        // offset 32 has length=3088 ("common"), offset 3120 has "APL,MacOS75".
        // At offset 0, the CHRP header itself is 16 bytes (0x5A, sum, len=2 -> 32 bytes),
        // followed by 4 bytes Adler32 (16..20) + 4 bytes generation (20..24) + 8 bytes zero = 32 bytes.
        // Then offset 32 starts the partitions!

        // Build partition at offset 32: "APL,MacOS75" with length 512 bytes (32 * 16).
        let partOffset = 32
        let partLengthBlocks: UInt16 = 32 // 512 bytes
        var partHeader = Data(repeating: 0, count: 16)
        partHeader[0] = 0x70
        partHeader[2] = UInt8(partLengthBlocks >> 8)
        partHeader[3] = UInt8(partLengthBlocks & 0xFF)
        let nameBytes = Array("APL,MacOS75".utf8)
        for (i, b) in nameBytes.enumerated() {
            partHeader[4 + i] = b
        }
        partHeader[1] = OSXNVRAM.headerSum(partHeader)
        bank.replaceSubrange(partOffset..<(partOffset + 16), with: partHeader)

        if let xpramOffset {
            let encoded = UInt32(bitPattern: Int32(xpramOffset)) & 0x00FF_FFFF
            let pos = partOffset + 16 + 0xEC
            bank[pos] = 0
            bank[pos + 1] = UInt8((encoded >> 16) & 0xFF)
            bank[pos + 2] = UInt8((encoded >> 8) & 0xFF)
            bank[pos + 3] = UInt8(encoded & 0xFF)
        }

        // Fill remaining space up to 8192 with a free partition ("wwwwwwwwwwww")
        let restOffset = partOffset + Int(partLengthBlocks) * 16
        let restBlocks = UInt16((OSXNVRAM.bankSize - restOffset) / 16)
        var freeHeader = Data(repeating: 0, count: 16)
        freeHeader[0] = 0x7F
        freeHeader[2] = UInt8(restBlocks >> 8)
        freeHeader[3] = UInt8(restBlocks & 0xFF)
        for i in 4..<16 { freeHeader[i] = 0x77 }
        freeHeader[1] = OSXNVRAM.headerSum(freeHeader)
        bank.replaceSubrange(restOffset..<(restOffset + 16), with: freeHeader)

        // Bank CHRP header at offset 0 (length = 2 -> 32 bytes)
        bank[0] = 0x5A
        bank[2] = 0
        bank[3] = 2
        for i in 4..<16 { bank[i] = 0x77 }
        bank[1] = corruptHeader ? 0xFF : OSXNVRAM.headerSum(Data(bank[0..<16]))

        // Generation at 20..24
        bank[20] = UInt8((generation >> 24) & 0xFF)
        bank[21] = UInt8((generation >> 16) & 0xFF)
        bank[22] = UInt8((generation >> 8) & 0xFF)
        bank[23] = UInt8(generation & 0xFF)

        // Adler32 over 20..<8192 stored at 16..20
        let adler = OSXNVRAM.adler32(Data(bank[20..<OSXNVRAM.bankSize]))
        bank[16] = UInt8((adler >> 24) & 0xFF)
        bank[17] = UInt8((adler >> 16) & 0xFF)
        bank[18] = UInt8((adler >> 8) & 0xFF)
        bank[19] = UInt8(adler & 0xFF)

        return bank
    }

    static func makeNVRAMData(
        gen0: UInt32 = 1,
        offset0: Int = 0,
        gen1: UInt32 = 2,
        offset1: Int = 0
    ) -> Data {
        var data = Data()
        data.append(makeNVRAMBank(generation: gen0, xpramOffset: offset0))
        data.append(makeNVRAMBank(generation: gen1, xpramOffset: offset1))
        return data
    }

    // MARK: - OSXNVRAM tests

    func testOSXNVRAMValidatesDualAndSingleSurvivingBank() throws {
        let valid = Self.makeNVRAMData(gen0: 1, offset0: 0, gen1: 2, offset1: 3600)
        XCTAssertEqual(OSXNVRAM.validBanks(in: valid).count, 2)
        XCTAssertNoThrow(try OSXNVRAM.validate(data: valid))
        XCTAssertEqual(try OSXNVRAM.rtcOffset(data: valid), 3600)

        var oneCorrupted = valid
        oneCorrupted[0] = 0x00 // corrupt bank 0
        XCTAssertEqual(OSXNVRAM.validBanks(in: oneCorrupted).count, 1)
        XCTAssertNoThrow(try OSXNVRAM.validate(data: oneCorrupted))
        XCTAssertEqual(try OSXNVRAM.rtcOffset(data: oneCorrupted), 3600)

        var bothCorrupted = oneCorrupted
        bothCorrupted[OSXNVRAM.bankSize + 100] ^= 0xFF // corrupt bank 1 Adler32
        XCTAssertTrue(OSXNVRAM.validBanks(in: bothCorrupted).isEmpty)
        XCTAssertThrowsError(try OSXNVRAM.validate(data: bothCorrupted)) { error in
            XCTAssertEqual(error as? OSXNVRAMError, .damaged)
        }
    }

    func testOSXNVRAMSignedRTCOffsetAndRTCBaseFormatting() throws {
        let fixedNow = Date(timeIntervalSince1970: 1_700_000_000) // 2023-11-14T22:13:20Z

        let zero = Self.makeNVRAMData(gen0: 1, offset0: 0, gen1: 2, offset1: 0)
        XCTAssertEqual(try OSXNVRAM.rtcBase(data: zero, now: fixedNow), "base=utc")

        let negative = Self.makeNVRAMData(gen0: 3, offset0: 0, gen1: 5, offset1: -18_000)
        XCTAssertEqual(try OSXNVRAM.rtcOffset(data: negative), -18_000)
        XCTAssertEqual(try OSXNVRAM.rtcBase(data: negative, now: fixedNow), "base=2023-11-14T17:13:20")

        // Newest generation wins even if placed in bank 0
        let bank0Newer = Self.makeNVRAMData(gen0: 10, offset0: 7200, gen1: 9, offset1: 0)
        XCTAssertEqual(try OSXNVRAM.rtcOffset(data: bank0Newer), 7200)
        XCTAssertEqual(try OSXNVRAM.rtcBase(data: bank0Newer, now: fixedNow), "base=2023-11-15T00:13:20")
    }

    func testOSXNVRAMRejectsAmbiguousAndOutOfRangeClock() throws {
        let ambiguousSameGen = Self.makeNVRAMData(gen0: 4, offset0: 0, gen1: 4, offset1: 3600)
        XCTAssertThrowsError(try OSXNVRAM.rtcOffset(data: ambiguousSameGen)) { error in
            XCTAssertEqual(error as? OSXNVRAMError, .ambiguousClock)
        }

        let ambiguousWrap = Self.makeNVRAMData(gen0: 1, offset0: 0, gen1: 0x8000_0005, offset1: 0)
        XCTAssertThrowsError(try OSXNVRAM.rtcOffset(data: ambiguousWrap)) { error in
            XCTAssertEqual(error as? OSXNVRAMError, .ambiguousClock)
        }

        let outOfRange = Self.makeNVRAMData(gen0: 1, offset0: 0, gen1: 2, offset1: 90_000)
        XCTAssertThrowsError(try OSXNVRAM.rtcOffset(data: outOfRange)) { error in
            XCTAssertEqual(error as? OSXNVRAMError, .invalidClock)
        }

        let missingURL = FileManager.default.temporaryDirectory.appendingPathComponent("missing-\(UUID().uuidString).img")
        XCTAssertEqual(OSXNVRAM.rtcBase(at: missingURL), "base=utc")
    }

    // MARK: - Shutdown Detector helpers

    private func makeRGB(width: Int = 800, height: Int = 600, r: UInt8 = 200, g: UInt8 = 200, b: UInt8 = 200) -> Data {
        var data = Data(count: width * height * 3)
        data.withUnsafeMutableBytes { raw in
            let ptr = raw.bindMemory(to: UInt8.self)
            for i in 0..<(width * height) {
                ptr[i * 3] = r
                ptr[i * 3 + 1] = g
                ptr[i * 3 + 2] = b
            }
        }
        return data
    }

    private func paintRect(
        _ rgb: inout Data,
        width: Int,
        x0: Int, y0: Int, x1: Int, y1: Int,
        r: UInt8, g: UInt8, b: UInt8
    ) {
        rgb.withUnsafeMutableBytes { raw in
            let ptr = raw.bindMemory(to: UInt8.self)
            for y in y0..<y1 {
                for x in x0..<x1 {
                    let off = (y * width + x) * 3
                    ptr[off] = r
                    ptr[off + 1] = g
                    ptr[off + 2] = b
                }
            }
        }
    }

    func testOSXShutdownDetectorMenuAndSelectionAndConfirmation() {
        let w = 800, h = 600
        let before = makeRGB(width: w, height: h, r: 120, g: 150, b: 200)

        // Identical frame never passes
        XCTAssertFalse(OSXShutdownDetector.detectMenuOpened(width: w, height: h, before: before, current: before))

        // Open Apple menu: white dropdown in (10, 22, 180, 300)
        var menuFrame = before
        paintRect(&menuFrame, width: w, x0: 10, y0: 22, x1: 180, y1: 300, r: 245, g: 245, b: 245)
        XCTAssertTrue(OSXShutdownDetector.detectMenuOpened(width: w, height: h, before: before, current: menuFrame))

        // Full-screen redraw fails detectMenuOpened
        let fullRedraw = makeRGB(width: w, height: h, r: 20, g: 20, b: 20)
        XCTAssertFalse(OSXShutdownDetector.detectMenuOpened(width: w, height: h, before: before, current: fullRedraw))

        // Selection moved inside open Apple menu (y = 264..283)
        var selFrame = menuFrame
        paintRect(&selFrame, width: w, x0: 10, y0: 264, x1: 180, y1: 283, r: 50, g: 110, b: 230)
        XCTAssertTrue(OSXShutdownDetector.detectMenuSelectionMoved(width: w, height: h, prevMenu: menuFrame, current: selFrame))
        XCTAssertFalse(OSXShutdownDetector.detectMenuSelectionMoved(width: w, height: h, prevMenu: menuFrame, current: menuFrame))

        // Confirmation dialog via modalChange (menu bar dimmed + centered alert box)
        var beforeWithMenus = before
        // Menu bar background white, black menu title ink columns in x = 40..240, y = 4..15
        paintRect(&beforeWithMenus, width: w, x0: 0, y0: 0, x1: w, y1: 22, r: 245, g: 245, b: 245)
        for col in stride(from: 40, to: 220, by: 6) {
            paintRect(&beforeWithMenus, width: w, x0: col, y0: 5, x1: col + 2, y1: 15, r: 10, g: 10, b: 10)
        }
        var confFrame = beforeWithMenus
        // Dim menu titles to light gray (same columns, recolored > 60 away from black, still far from white bg)
        for col in stride(from: 40, to: 220, by: 6) {
            paintRect(&confFrame, width: w, x0: col, y0: 5, x1: col + 2, y1: 15, r: 150, g: 150, b: 150)
        }
        // Centered dialog 360 x 140
        paintRect(&confFrame, width: w, x0: 220, y0: 180, x1: 580, y1: 320, r: 235, g: 235, b: 235)
        XCTAssertTrue(OSXShutdownDetector.detectConfirmationDialog(width: w, height: h, before: beforeWithMenus, current: confFrame))

        // Unrecognized small edge-only change does not authorize Return
        var edgeOnly = beforeWithMenus
        paintRect(&edgeOnly, width: w, x0: w - 120, y0: 40, x1: w - 10, y1: 520, r: 10, g: 10, b: 10)
        XCTAssertFalse(OSXShutdownDetector.detectConfirmationDialog(width: w, height: h, before: beforeWithMenus, current: edgeOnly))
    }

    func testOSXShutdownDetectorParsesPPMAndCGImage() throws {
        let w = 4, h = 2
        let pixels = Data((0..<(w * h * 3)).map { UInt8($0 * 7) })
        var ppm = Data("P6\n# comment\n4 2\n255\n".utf8)
        ppm.append(pixels)

        let frame = try XCTUnwrap(OSXShutdownDetector.RGBFrame.from(ppmData: ppm))
        XCTAssertEqual(frame.width, 4)
        XCTAssertEqual(frame.height, 2)
        XCTAssertEqual(frame.rgb, pixels)

        let space = CGColorSpace(name: CGColorSpace.sRGB)!
        var rgba = Data(count: w * h * 4)
        for i in 0..<(w * h) {
            rgba[i * 4] = pixels[i * 3]
            rgba[i * 4 + 1] = pixels[i * 3 + 1]
            rgba[i * 4 + 2] = pixels[i * 3 + 2]
            rgba[i * 4 + 3] = 255
        }
        let provider = try XCTUnwrap(CGDataProvider(data: rgba as CFData))
        let cg = try XCTUnwrap(CGImage(
            width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32,
            bytesPerRow: w * 4, space: space,
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent
        ))
        let fromCG = try XCTUnwrap(OSXShutdownDetector.RGBFrame.from(cgImage: cg))
        XCTAssertEqual(fromCG, frame)
    }

    // MARK: - Shutdown Controller tests

    func testOSXShutdownControllerConfirmsHappyPathAndAbortsWithoutReturnOnUnverified() async {
        let w = 800, h = 600
        let before = makeRGB(width: w, height: h, r: 120, g: 150, b: 200)
        var menu = before
        paintRect(&menu, width: w, x0: 10, y0: 22, x1: 180, y1: 300, r: 245, g: 245, b: 245)
        var sel = menu
        paintRect(&sel, width: w, x0: 10, y0: 264, x1: 180, y1: 283, r: 50, g: 110, b: 230)
        var conf = before
        paintRect(&conf, width: w, x0: 200, y0: 180, x1: 600, y1: 340, r: 240, g: 240, b: 240)

        final class Box: @unchecked Sendable {
            var keys: [String] = []
            var unpaused = false
            var frameIndex = 0
        }

        let frames = [
            OSXShutdownDetector.RGBFrame(width: w, height: h, rgb: before),
            OSXShutdownDetector.RGBFrame(width: w, height: h, rgb: menu),
            OSXShutdownDetector.RGBFrame(width: w, height: h, rgb: sel),
            OSXShutdownDetector.RGBFrame(width: w, height: h, rgb: conf),
        ]

        // 1. Verified Restart path (wasPaused: true -> unpause + two up keys)
        let box1 = Box()
        let t1 = OSXShutdownController.Transport(
            sendKey: { box1.keys.append($0); return true },
            unpause: { box1.unpaused = true; return true },
            captureFrame: {
                let f = frames[min(box1.frameIndex, frames.count - 1)]
                box1.frameIndex += 1
                return f
            },
            sleepMillis: { _ in }
        )
        let outcome1 = await OSXShutdownController.perform(action: .restart, wasPaused: true, transport: t1)
        XCTAssertEqual(outcome1, .confirmed)
        XCTAssertTrue(box1.unpaused)
        XCTAssertEqual(box1.keys, ["ctrl-f2 100", "down 100", "end 100", "up 100", "up 100", "ret 100", "ret 100"])

        // 2. Unverified confirmation dialog must NOT send final Return
        let badFrames = [
            frames[0], frames[1], frames[2],
            OSXShutdownDetector.RGBFrame(width: w, height: h, rgb: before),
        ]
        let box2 = Box()
        let t2 = OSXShutdownController.Transport(
            sendKey: { box2.keys.append($0); return true },
            unpause: { true },
            captureFrame: {
                let f = badFrames[min(box2.frameIndex, badFrames.count - 1)]
                box2.frameIndex += 1
                return f
            },
            sleepMillis: { _ in }
        )
        let outcome2 = await OSXShutdownController.perform(action: .shutDown, wasPaused: false, transport: t2)
        if case .unverified = outcome2 {
            // expected
        } else {
            XCTFail("Expected .unverified, got \(outcome2)")
        }
        XCTAssertEqual(box2.keys, ["ctrl-f2 100", "down 100", "end 100", "up 100", "ret 100"])
    }
}
