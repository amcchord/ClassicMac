import Foundation

/// Evidence from the running VGA transport, not the bundled guest installer.
/// This host interface deliberately does not claim an installed driver version.
struct GXMetalStatusSnapshot: Decodable, Equatable {
    let schema: Int
    let `protocol`: UInt32
    let renderer: String
    let completedCommands: UInt64
    let activeContexts: UInt32
    let successfulDraws: UInt64
    let lastDrawAgeMs: Int64
    let faulted: Bool
    let errorCode: UInt32

    var guestHasContacted: Bool { completedCommands > 0 }

    var protocolVersion: String { "\(`protocol` >> 16).\(`protocol` & 0xffff)" }

    var hasRecentDraws: Bool {
        activeContexts > 0 && successfulDraws > 0 &&
        lastDrawAgeMs >= 0 && lastDrawAgeMs < 2_000
    }

    /// HMP prints a QOM string as a JSON string, surrounded by its echoed
    /// command and prompt. Decode that string before decoding the payload.
    /// Reject unknown schemas/backends instead of displaying guessed health.
    static func parseHMP(_ response: String) -> GXMetalStatusSnapshot? {
        let decoder = JSONDecoder()
        for line in response.components(separatedBy: .newlines) {
            let candidate = line.trimmingCharacters(in: .whitespacesAndNewlines)
            guard let data = candidate.data(using: .utf8),
                  let payload = try? decoder.decode(String.self, from: data),
                  let payloadData = payload.data(using: .utf8),
                  let snapshot = try? decoder.decode(Self.self, from: payloadData),
                  snapshot.schema == 1,
                  ["metal", "software"].contains(snapshot.renderer),
                  snapshot.lastDrawAgeMs >= -1 else { continue }
            return snapshot
        }
        return nil
    }

    var rendererDetail: String {
        renderer == "metal" ? "Host renderer: Metal" : "Host renderer: software"
    }

    var guestDetail: String {
        guestHasContacted ? "Guest: GXMetal commands received" : "Guest: no GXMetal commands yet"
    }
}

enum GXMetalStatus: Equatable {
    case stopped, unsupported, checking, unavailable, paused
    case waitingForGuest, ready, accelerating, softwareFallback, faulted

    static func resolve(
        snapshot: GXMetalStatusSnapshot?,
        isPowerMac: Bool,
        isRunning: Bool,
        isPaused: Bool,
        hasChecked: Bool = true
    ) -> Self {
        guard isPowerMac else { return .unsupported }
        guard isRunning else { return .stopped }
        if isPaused { return .paused }
        guard let snapshot else { return hasChecked ? .unavailable : .checking }
        if snapshot.faulted || snapshot.errorCode != 0 { return .faulted }
        if snapshot.renderer == "software" { return .softwareFallback }
        if !snapshot.guestHasContacted { return .waitingForGuest }
        return snapshot.hasRecentDraws ? .accelerating : .ready
    }

    var title: String {
        switch self {
        case .stopped: return "Mac is shut down"
        case .unsupported: return "GXMetal requires a Power Mac"
        case .checking: return "Checking GXMetal…"
        case .unavailable: return "GXMetal status unavailable"
        case .paused: return "Mac is paused"
        case .waitingForGuest: return "Metal ready · waiting for guest"
        case .ready: return "GXMetal connected · idle"
        case .accelerating: return "Rendering with Metal"
        case .softwareFallback: return "Software renderer"
        case .faulted: return "GXMetal needs attention"
        }
    }

    var symbol: String {
        switch self {
        case .accelerating: return "bolt.fill"
        case .ready: return "checkmark.circle"
        case .paused: return "pause.circle"
        case .faulted, .softwareFallback: return "exclamationmark.triangle"
        case .unsupported, .unavailable: return "questionmark.circle"
        default: return "bolt.slash"
        }
    }

    var explanation: String {
        switch self {
        case .stopped: return "Start this Mac to check its graphics accelerator."
        case .unsupported: return "GXMetal accelerates compatible Power Mac games."
        case .checking: return "Reading the running Mac’s graphics status."
        case .unavailable: return "The running Mac did not report graphics status. Try again after startup."
        case .paused: return "Resume this Mac to see current rendering activity."
        case .waitingForGuest: return "Open GXMetal Test or a compatible game to check the guest connection."
        case .ready: return "The guest has used GXMetal; no recent 3D draws. A game may also use Apple Software RAVE."
        case .accelerating: return "The guest sent successful 3D draw commands to Metal in the last two seconds."
        case .softwareFallback: return "GXMetal’s host renderer is using software. Metal acceleration is unavailable in this session."
        case .faulted: return "The graphics transport reported an error. Quit the game and run GXMetal Test."
        }
    }
}

enum GXMetalStatusHelp {
    static let instructions = """
    Open ClassicMac Tools in the guest, then its GXMetal folder, and run GXMetal Test. It checks the installed driver version, rendering, and software fallback.

    To install or repair, run Install GXMetal from that same folder, restart the guest, then run GXMetal Test again.

    If Tools is missing, shut down the guest, enable ClassicMac Tools in this Mac’s media controls, and start it again.

    The status icon shows current host activity. It cannot confirm the installed extension version, a passing test, or whether a game has selected Apple Software RAVE.
    """
}
