import XCTest
import Carbon
@testable import ClassicMac

final class PasteTextTests: XCTestCase {
    func testEveryPrintableASCIICharacterHasASequence() throws {
        let text = String((32...126).map { Character(UnicodeScalar($0)!) })
        let plan = try PasteTextPlan(text)
        XCTAssertEqual(plan.characters.count, 95)
        XCTAssertEqual(Set(plan.characters.map { $0.joined() }).count, 95)
    }

    func testMonitorMetacharactersAreOnlyKeyNames() throws {
        let plan = try PasteTextPlan("\"\\;\nquit\r\n$()")
        XCTAssertEqual(plan.characters, [["shift-apostrophe"], ["backslash"], ["semicolon"], ["ret"],
                                        ["q"], ["u"], ["i"], ["t"], ["ret"], ["shift-4"], ["shift-9"], ["shift-0"]])
        for chord in plan.characters.flatMap({ $0 }) {
            XCTAssertNotNil(chord.range(of: "^[a-z0-9_-]+$", options: .regularExpression))
        }
    }

    func testNewlinesAndTabsArePreservedAsSingleKeys() throws {
        XCTAssertEqual(try PasteTextPlan("a\r\nb\rc\nd\t ").characters,
                       [["a"], ["ret"], ["b"], ["ret"], ["c"], ["ret"], ["d"], ["tab"], ["spc"]])
    }

    func testCommonMacRomanAccentsUseBalancedOptionChords() throws {
        XCTAssertEqual(try PasteTextPlan("café Ñ ç Å ü").characters,
                       [["c"], ["a"], ["f"], ["alt-e", "e"], ["spc"], ["alt-n", "shift-n"],
                        ["spc"], ["alt-c"], ["spc"], ["alt-shift-a"], ["spc"], ["alt-u", "u"]])
        XCTAssertEqual(try PasteTextPlan("cafe\u{301}"), try PasteTextPlan("café"))
    }

    func testSupportedAccentsMatchTheMacUSKeyboardLayout() throws {
        let sources = TISCreateInputSourceList(
            [kTISPropertyInputSourceID as String: "com.apple.keylayout.US"] as CFDictionary, false
        ).takeRetainedValue() as NSArray
        guard let source = sources.firstObject else { throw XCTSkip("The Mac US keyboard layout is unavailable") }
        let inputSource = unsafeBitCast(source as AnyObject, to: TISInputSource.self)
        let rawData = try XCTUnwrap(TISGetInputSourceProperty(inputSource, kTISPropertyUnicodeKeyLayoutData))
        let data = Unmanaged<CFData>.fromOpaque(rawData).takeUnretainedValue()
        let layout = UnsafeRawPointer(CFDataGetBytePtr(data)).assumingMemoryBound(to: UCKeyboardLayout.self)
        let codes: [String: UInt16] = ["a": UInt16(kVK_ANSI_A), "e": UInt16(kVK_ANSI_E),
            "i": UInt16(kVK_ANSI_I), "o": UInt16(kVK_ANSI_O), "u": UInt16(kVK_ANSI_U),
            "y": UInt16(kVK_ANSI_Y), "n": UInt16(kVK_ANSI_N), "c": UInt16(kVK_ANSI_C),
            "s": UInt16(kVK_ANSI_S), "grave_accent": UInt16(kVK_ANSI_Grave)]
        for scalar in 128...0x2ff {
            let character = String(UnicodeScalar(scalar)!)
            guard let plan = try? PasteTextPlan(character) else { continue }
            var deadKey: UInt32 = 0
            var actual = ""
            for chord in plan.characters[0] {
                let components = chord.split(separator: "-").map(String.init)
                let key = try XCTUnwrap(codes[components.last!])
                var modifiers = 0
                if components.contains("alt") { modifiers |= optionKey }
                if components.contains("shift") { modifiers |= shiftKey }
                var buffer = [UniChar](repeating: 0, count: 8)
                var length = 0
                XCTAssertEqual(UCKeyTranslate(layout, key, UInt16(kUCKeyActionDown), UInt32(modifiers >> 8),
                    UInt32(LMGetKbdType()), 0, &deadKey, buffer.count, &length, &buffer), noErr)
                actual += String(utf16CodeUnits: buffer, count: length)
            }
            XCTAssertEqual(actual, character, "US keyboard sequence for U+\(String(scalar, radix: 16))")
            // UCKeyTranslate retains private bookkeeping in the upper bits of
            // its state. Check subsequent behavior rather than its encoding.
            var next = [UniChar](repeating: 0, count: 8)
            var nextLength = 0
            XCTAssertEqual(UCKeyTranslate(layout, UInt16(kVK_ANSI_A), UInt16(kUCKeyActionDown), 0,
                UInt32(LMGetKbdType()), 0, &deadKey, next.count, &nextLength, &next), noErr)
            XCTAssertEqual(String(utf16CodeUnits: next, count: nextLength), "a",
                           "Accent must not affect the next manually typed character")
        }
    }

    func testUnsupportedUnicodeAndControlCharactersRejectEntirePaste() {
        for text in ["hello😀", "汉字", "\u{1b}", "\u{0}", "\u{7f}", "smart “quote”"] {
            XCTAssertThrowsError(try PasteTextPlan(text)) { error in
                guard case PasteTextPlan.ValidationError.unsupported = error else { return XCTFail("Wrong error: \(error)") }
            }
        }
    }

    func testEmptyAndOversizedTextAreRejectedWithoutTruncation() throws {
        XCTAssertThrowsError(try PasteTextPlan(""))
        XCTAssertThrowsError(try PasteTextPlan(String(repeating: "a", count: PasteTextPlan.maximumCharacters + 1)))
        XCTAssertEqual(try PasteTextPlan(String(repeating: "a", count: PasteTextPlan.maximumCharacters)).characters.count,
                       PasteTextPlan.maximumCharacters)
    }

    func testTransferStopsBeforeFirstKeyWhenUnavailable() async throws {
        let calls = PasteTextCallRecorder()
        do {
            try await PasteTextTransfer.run(PasteTextPlan("abc"), intervalNanoseconds: 0,
                available: { false }, send: { await calls.add($0); return true }, progress: { _ in })
            XCTFail("Unavailable transfer succeeded")
        } catch PasteTextTransferError.unavailable { }
        let recorded = await calls.commands
        XCTAssertTrue(recorded.isEmpty)
    }

    func testTransferStopsOnMonitorFailureAndReportsOnlyCompletedCharacters() async throws {
        let calls = PasteTextCallRecorder()
        do {
            try await PasteTextTransfer.run(PasteTextPlan("abc"), intervalNanoseconds: 0,
                available: { true }, send: { command in
                    await calls.add(command)
                    return command != "sendkey b 1"
                }, progress: { await calls.markProgress($0) })
            XCTFail("Failed monitor transfer succeeded")
        } catch PasteTextTransferError.monitorFailed { }
        let recorded = await calls.commands
        let progress = await calls.progress
        XCTAssertEqual(recorded, ["sendkey a 1", "sendkey b 1"])
        XCTAssertEqual(progress, [1])
    }

    func testCancellationFinishesAccentButDoesNotStartAnotherCharacter() async throws {
        let calls = PasteTextCallRecorder()
        let firstKey = expectation(description: "Accent started")
        let task = Task {
            try await PasteTextTransfer.run(PasteTextPlan("éx"), intervalNanoseconds: 20_000_000,
                available: { true }, send: { command in
                    await calls.add(command)
                    if command == "sendkey alt-e 1" { firstKey.fulfill() }
                    return true
                }, progress: { await calls.markProgress($0) })
        }
        await fulfillment(of: [firstKey], timeout: 1)
        task.cancel()
        do { try await task.value; XCTFail("Cancelled transfer succeeded") }
        catch is CancellationError { }
        let recorded = await calls.commands
        let progress = await calls.progress
        XCTAssertEqual(recorded, ["sendkey alt-e 1", "sendkey e 1"])
        XCTAssertEqual(progress, [1])
    }

    func testPauseAfterACharacterStopsTheFollowingCharacter() async throws {
        let calls = PasteTextCallRecorder()
        do {
            try await PasteTextTransfer.run(PasteTextPlan("abc"), intervalNanoseconds: 0,
                available: { await calls.progress.isEmpty }, send: { await calls.add($0); return true },
                progress: { await calls.markProgress($0) })
            XCTFail("Paused transfer succeeded")
        } catch PasteTextTransferError.unavailable { }
        let recorded = await calls.commands
        XCTAssertEqual(recorded, ["sendkey a 1"])
    }
}

private actor PasteTextCallRecorder {
    var commands: [String] = []
    var progress: [Int] = []
    func add(_ command: String) { commands.append(command) }
    func markProgress(_ count: Int) { progress.append(count) }
}
