import Foundation

// A user-presentable error: a specific title, a plain-language message that
// suggests a way forward, and optionally a log file holding the technical
// details (shown behind a "Show Details" button rather than in the alert).
struct AppError: Identifiable {
    let id = UUID()
    var title: String
    var message: String
    var logURL: URL?

    init(_ title: String, _ message: String, logURL: URL? = nil) {
        self.title = title
        self.message = message
        self.logURL = logURL
    }
}

// Writes technical output (like the emulator's crash log) to
// ~/Library/Logs/ClassicMac so alerts can stay readable while the full
// details remain available for support.
enum AppLog {
    static var logsDir: URL {
        let base = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first!
        let dir = base.appendingPathComponent("Logs/ClassicMac", isDirectory: true)
        AppPaths.ensureDirectory(dir)
        return dir
    }

    // Writes `contents` to a timestamped log named after the machine and
    // returns its location, or nil if the write failed or there was nothing
    // to write.
    static func write(_ contents: String, machineName: String) -> URL? {
        let trimmed = contents.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }

        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH.mm.ss"
        var safeName = machineName.replacingOccurrences(of: "/", with: "-")
        safeName = safeName.replacingOccurrences(of: ":", with: "-")
        if safeName.isEmpty {
            safeName = "Machine"
        }
        let url = logsDir.appendingPathComponent("\(safeName) \(formatter.string(from: Date())).log")
        do {
            try trimmed.write(to: url, atomically: true, encoding: .utf8)
            return url
        } catch {
            return nil
        }
    }
}
