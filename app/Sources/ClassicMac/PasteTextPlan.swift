import Foundation

/// Text entry without a guest clipboard driver. Every monitor command comes
/// from this fixed US keyboard table; clipboard contents never become HMP code.
struct PasteTextPlan: Equatable {
    static let maximumCharacters = 8_192
    let characters: [[String]]

    enum ValidationError: LocalizedError, Equatable {
        case empty
        case tooLong
        case unsupported(String)

        var errorDescription: String? {
            switch self {
            case .empty: return "Copy some plain text, or type it below."
            case .tooLong: return "Paste up to 8,192 characters at a time. Split this text into smaller sections."
            case .unsupported(let characters):
                return "This text contains unsupported characters: \(characters). Edit them before pasting. Nothing has been sent."
            }
        }
    }

    init(_ text: String) throws {
        // Classic Mac editors expect Return. Normalize line endings once, and
        // compose equivalent accent sequences without changing visible text.
        let normalized = text.replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\r", with: "\n")
            .precomposedStringWithCanonicalMapping
        guard !normalized.isEmpty else { throw ValidationError.empty }
        guard normalized.count <= Self.maximumCharacters else { throw ValidationError.tooLong }
        var result: [[String]] = []
        var unsupported: [String] = []
        for character in normalized {
            if let sequence = Self.sequence(for: character) {
                result.append(sequence)
            } else {
                let label = character.unicodeScalars.map { String(format: "U+%04X", $0.value) }.joined(separator: " ")
                if !unsupported.contains(label), unsupported.count < 5 {
                    unsupported.append(label)
                }
            }
        }
        guard unsupported.isEmpty else { throw ValidationError.unsupported(unsupported.joined(separator: ", ")) }
        characters = result
    }

    private static func sequence(for character: Character) -> [String]? {
        if let key = ascii[character] { return [key] }
        // Standard Mac US dead keys. Restrict this to combinations represented
        // in MacRoman, rather than claiming arbitrary Unicode support.
        for (accent, lowercase, uppercase) in [
            ("e", "áéíóú", "ÁÉÍÓÚ"),
            ("u", "äëïöüÿ", "ÄËÏÖÜŸ"),
            ("i", "âêîôû", "ÂÊÎÔÛ"),
            ("grave_accent", "àèìòù", "ÀÈÌÒÙ"),
            ("n", "ãñõ", "ÃÑÕ")
        ] {
            let bases = accent == "n" ? Array("ano") : Array("aeiouy")
            if let index = Array(lowercase).firstIndex(of: character) {
                return ["alt-\(accent)", String(bases[index])]
            }
            if let index = Array(uppercase).firstIndex(of: character) {
                return ["alt-\(accent)", "shift-\(bases[index])"]
            }
        }
        return ["ç": "alt-c", "Ç": "alt-shift-c", "å": "alt-a", "Å": "alt-shift-a",
                "ø": "alt-o", "Ø": "alt-shift-o", "ß": "alt-s"][character].map { [$0] }
    }

    private static let ascii: [Character: String] = {
        var keys: [Character: String] = [:]
        for character in "abcdefghijklmnopqrstuvwxyz0123456789" { keys[character] = String(character) }
        for character in "ABCDEFGHIJKLMNOPQRSTUVWXYZ" { keys[character] = "shift-\(String(character).lowercased())" }
        for (plain, shifted, code) in [
            ("`", "~", "grave_accent"), ("-", "_", "minus"), ("=", "+", "equal"),
            ("[", "{", "bracket_left"), ("]", "}", "bracket_right"), ("\\", "|", "backslash"),
            (";", ":", "semicolon"), ("'", "\"", "apostrophe"), (",", "<", "comma"),
            (".", ">", "dot"), ("/", "?", "slash"),
            ("1", "!", "1"), ("2", "@", "2"), ("3", "#", "3"), ("4", "$", "4"),
            ("5", "%", "5"), ("6", "^", "6"), ("7", "&", "7"), ("8", "*", "8"),
            ("9", "(", "9"), ("0", ")", "0")
        ] {
            keys[Character(plain)] = code
            keys[Character(shifted)] = "shift-\(code)"
        }
        keys[" "] = "spc"
        keys["\t"] = "tab"
        keys["\n"] = "ret"
        return keys
    }()
}

enum PasteTextTransferError: LocalizedError {
    case unavailable
    case monitorFailed
    var errorDescription: String? {
        switch self {
        case .unavailable: return "Pasting stopped because the Mac paused or shut down. Text already entered remains in the Mac."
        case .monitorFailed: return "The Mac stopped responding while pasting. Text already entered remains in the Mac."
        }
    }
}

/// One acknowledged, balanced key chord at a time. No large input queue builds
/// up, so Cancel or Pause stops at the current character (including its accent).
enum PasteTextTransfer {
    static func run(
        _ plan: PasteTextPlan,
        intervalNanoseconds: UInt64,
        available: @escaping () async -> Bool,
        send: @escaping (String) async -> Bool,
        progress: @escaping (Int) async -> Void
    ) async throws {
        for (index, sequence) in plan.characters.enumerated() {
            try Task.checkCancellation()
            guard await available() else { throw PasteTextTransferError.unavailable }
            for chord in sequence {
                // Finish an accent pair before checking cancellation again;
                // otherwise the next manually typed letter inherits a dead key.
                guard await send("sendkey \(chord) 1") else { throw PasteTextTransferError.monitorFailed }
                // This short chord-settling delay is deliberately noncancellable.
                // QEMU's key-up events must be allowed to run before another key.
                await Task.detached { try? await Task.sleep(nanoseconds: intervalNanoseconds) }.value
            }
            await progress(index + 1)
        }
    }
}
