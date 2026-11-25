import AppKit
import Foundation

struct HostPrinter: Identifiable, Hashable, Sendable {
    var id: String { queueName }
    let queueName: String
    let displayName: String
    let isDefault: Bool
    let state: String
}

struct PrintJobResult: Sendable {
    let jobName: String
    let target: String
    let bytes: Int
    let savedPDFURL: URL?
}

enum PrintError: LocalizedError, Sendable {
    case invalidJob(String)
    case converterMissing(String)
    case convertFailed(String)
    case printerUnavailable(String)
    case submitFailed(String)

    var errorDescription: String? {
        switch self {
        case .invalidJob(let msg),
             .converterMissing(let msg),
             .convertFailed(let msg),
             .printerUnavailable(let msg),
             .submitFailed(let msg):
            return msg
        }
    }
}

enum HostPrinterManager {
    static let pdfTargetValue = "pdf"
    static let maxJobBytes = 64 * 1024 * 1024

    // MARK: Printer discovery

    static func defaultPrinterQueue() -> String? {
        let res = runTool("/usr/bin/lpstat", arguments: ["-d"], timeout: 5.0)
        guard res.exitCode == 0 else { return nil }
        for line in res.stdout.components(separatedBy: .newlines) {
            let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
            let prefix = "system default destination:"
            if trimmed.lowercased().hasPrefix(prefix) {
                let queue = trimmed.dropFirst(prefix.count).trimmingCharacters(in: .whitespacesAndNewlines)
                if !queue.isEmpty {
                    return queue
                }
            }
        }
        return nil
    }

    static func availablePrinters() -> [HostPrinter] {
        let defaultQueue = defaultPrinterQueue()
        let res = runTool("/usr/bin/lpstat", arguments: ["-p", "-l"], timeout: 8.0)
        let combined = "\(res.stdout)\n\(res.stderr)".lowercased()
        if res.exitCode != 0 {
            if combined.contains("no destinations") {
                return []
            }
            return NSPrinter.printerNames.map { name in
                HostPrinter(
                    queueName: name,
                    displayName: name,
                    isDefault: name == defaultQueue,
                    state: "unknown"
                )
            }
        }

        var order: [String] = []
        var byQueue: [String: (display: String, state: String)] = [:]
        var currentQueue: String?

        for rawLine in res.stdout.components(separatedBy: .newlines) {
            if rawLine.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                continue
            }
            if rawLine.hasPrefix("printer ") {
                let afterPrefix = rawLine.dropFirst("printer ".count)
                if let spaceIdx = afterPrefix.firstIndex(where: { $0.isWhitespace }) {
                    let queue = String(afterPrefix[..<spaceIdx])
                    let rest = afterPrefix[spaceIdx...].trimmingCharacters(in: .whitespaces).lowercased()
                    let state: String
                    if rest.hasPrefix("is idle") {
                        state = "idle"
                    } else if rest.hasPrefix("is printing") {
                        state = "printing"
                    } else if rest.contains("disabled") {
                        state = "disabled"
                    } else {
                        state = "unknown"
                    }
                    if byQueue[queue] == nil {
                        order.append(queue)
                        byQueue[queue] = (display: queue, state: state)
                    } else {
                        byQueue[queue]?.state = state
                    }
                    currentQueue = queue
                }
                continue
            }
            if let q = currentQueue, rawLine.first?.isWhitespace == true {
                let trimmed = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
                if trimmed.hasPrefix("Description:") {
                    let desc = trimmed.dropFirst("Description:".count)
                        .trimmingCharacters(in: .whitespacesAndNewlines)
                    if !desc.isEmpty {
                        byQueue[q]?.display = desc
                    }
                }
            }
        }

        return order.compactMap { queue in
            guard let entry = byQueue[queue] else { return nil }
            return HostPrinter(
                queueName: queue,
                displayName: entry.display,
                isDefault: queue == defaultQueue,
                state: entry.state
            )
        }
    }

    // MARK: Ghostscript & PostScript helpers

    static func ghostscriptPath() -> String? {
        let fm = FileManager.default
        var candidates: [String] = []
        if let bundleRes = Bundle.main.resourceURL {
            candidates.append(bundleRes.appendingPathComponent("bin/gs").path)
            candidates.append(bundleRes.appendingPathComponent("gs").path)
        }
        candidates.append(AppPaths.resourcesDir.appendingPathComponent("bin/gs").path)
        candidates.append(AppPaths.resourcesDir.appendingPathComponent("gs").path)
        candidates.append("/opt/homebrew/bin/gs")
        candidates.append("/usr/local/bin/gs")
        candidates.append("/opt/local/bin/gs")
        if let envPath = ProcessInfo.processInfo.environment["PATH"] {
            for dir in envPath.split(separator: ":") where !dir.isEmpty {
                candidates.append(URL(fileURLWithPath: String(dir)).appendingPathComponent("gs").path)
            }
        }
        var seen = Set<String>()
        for path in candidates where seen.insert(path).inserted {
            if fm.isExecutableFile(atPath: path) {
                return path
            }
        }
        return nil
    }

    private static func psEscape(_ text: String) -> String {
        var out = ""
        out.reserveCapacity(text.count)
        for scalar in text.unicodeScalars {
            if scalar.value < 0x20 || scalar.value == 0x7F {
                out.append(" ")
            } else if scalar.value > 0x7E {
                out.append("?")
            } else if scalar == "\\" {
                out.append("\\\\")
            } else if scalar == "(" {
                out.append("\\(")
            } else if scalar == ")" {
                out.append("\\)")
            } else {
                out.append(Character(scalar))
            }
        }
        return out
    }

    static func generateTestPostScript(targetLabel: String, version: String = "") -> Data {
        let trimmedVer = version.trimmingCharacters(in: .whitespacesAndNewlines)
        let verLine = trimmedVer.isEmpty ? "ClassicMac" : "ClassicMac \(trimmedVer)"
        let escVer = psEscape(verLine)
        let trimmedTarget = targetLabel.trimmingCharacters(in: .whitespacesAndNewlines)
        let escTarget = psEscape(trimmedTarget.isEmpty ? "ClassicMac Printer" : trimmedTarget)
        let ps = """
        %!PS-Adobe-3.0
        %%Title: ClassicMac Printer Test Page
        %%Creator: ClassicMac
        %%Pages: 1
        %%BoundingBox: 0 0 612 792
        %%EndComments
        %%Page: 1 1
        /Helvetica-Bold findfont 22 scalefont setfont
        72 710 moveto (\(escVer) - Printer Test Page) show

        /Helvetica findfont 12 scalefont setfont
        72 682 moveto (Target: \(escTarget)) show
        72 664 moveto (Page size: US Letter 612 x 792 pt \\(PostScript Level 2/3\\)) show

        % Outer border
        0.8 setlinewidth
        0 setgray
        54 54 504 684 rectstroke

        % Checkerboard test pattern
        72 520 translate
        0 1 7 {
          /row exch def
          0 1 15 {
            /col exch def
            row col add 2 mod 0 eq {
              0.15 setgray
            } {
              0.92 setgray
            } ifelse
            col 18 mul row 14 mul 18 14 rectfill
          } for
        } for
        0 setgray
        0 0 288 112 rectstroke

        % Grayscale ramp
        0 -60 translate
        0 1 9 {
          /i exch def
          i 10 div setgray
          i 28 mul 0 28 32 rectfill
        } for
        0 setgray
        0 0 280 32 rectstroke

        grestore
        gsave
        /Helvetica findfont 10 scalefont setfont
        72 90 moveto (If you can read this page clearly, ClassicMac printing is working.) show
        grestore
        showpage
        %%EOF
        """
        return Data(ps.utf8)
    }

    static func convertPostScriptToPDF(_ psData: Data, outputURL: URL) throws {
        guard let gs = ghostscriptPath() else {
            throw PrintError.converterMissing(
                "Ghostscript (gs) is not installed. Install it with: brew install ghostscript"
            )
        }
        let args = [
            "-q", "-dBATCH", "-dNOPAUSE", "-dSAFER",
            "-sDEVICE=pdfwrite",
            "-sOutputFile=\(outputURL.path)",
            "-"
        ]
        let res = runTool(gs, arguments: args, stdinData: psData, timeout: 120.0)
        if res.timedOut {
            throw PrintError.convertFailed("PDF conversion timed out.")
        }
        let size = (try? FileManager.default.attributesOfItem(atPath: outputURL.path)[.size] as? NSNumber)?.intValue ?? 0
        guard res.exitCode == 0, FileManager.default.fileExists(atPath: outputURL.path), size > 0 else {
            let tail = res.stderr.trimmingCharacters(in: .whitespacesAndNewlines).suffix(200)
            let detail = tail.isEmpty ? "exit status \(res.exitCode)" : String(tail)
            throw PrintError.convertFailed("PDF conversion failed: \(detail)")
        }
    }

    // MARK: Job sanitization & submission

    static func sanitizeJobName(_ raw: String) -> String {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        var mapped = ""
        mapped.reserveCapacity(trimmed.count)
        for scalar in trimmed.unicodeScalars {
            let v = scalar.value
            let isSafe = (v >= 0x41 && v <= 0x5A) // A-Z
                || (v >= 0x61 && v <= 0x7A) // a-z
                || (v >= 0x30 && v <= 0x39) // 0-9
                || v == 0x20 || v == 0x2E || v == 0x5F || v == 0x28 || v == 0x29 || v == 0x2D
            mapped.append(isSafe ? Character(scalar) : " ")
        }
        var collapsed = mapped
            .split(whereSeparator: { $0.isWhitespace })
            .joined(separator: " ")
        for suffix in [".ps", ".eps", ".pdf"] {
            if collapsed.lowercased().hasSuffix(suffix) {
                collapsed = String(collapsed.dropLast(suffix.count))
            }
        }
        let edgeTrimmed = collapsed.trimmingCharacters(in: CharacterSet(charactersIn: " ._-"))
        let fallback = edgeTrimmed.isEmpty ? "Mac OS Print" : edgeTrimmed
        return String(fallback.prefix(64))
    }

    static func stripEOT(_ data: Data) -> Data {
        var start = data.startIndex
        var end = data.endIndex
        while start < end && data[start] == 0x04 {
            start = data.index(after: start)
        }
        while end > start && data[data.index(before: end)] == 0x04 {
            end = data.index(before: end)
        }
        return Data(data[start..<end])
    }

    static func isPDFData(_ data: Data) -> Bool {
        let whitespace: Set<UInt8> = [0x20, 0x09, 0x0A, 0x0D, 0x0B, 0x0C]
        let trimmed = data.drop(while: { whitespace.contains($0) })
        return trimmed.starts(with: [0x25, 0x50, 0x44, 0x46, 0x2D]) // "%PDF-"
    }

    static func defaultPDFOutputDirectory(for config: VMConfig) -> URL {
        if let shared = config.sharedFolderPath, !shared.isEmpty {
            var isDir: ObjCBool = false
            if FileManager.default.fileExists(atPath: shared, isDirectory: &isDir), isDir.boolValue {
                return URL(fileURLWithPath: shared, isDirectory: true)
                    .appendingPathComponent("Printed PDFs", isDirectory: true)
            }
        }
        return AppPaths.defaultLibraryDir.appendingPathComponent("Printed PDFs", isDirectory: true)
    }

    static func submitJob(
        data: Data,
        jobName: String,
        targetPrinter: String?,
        pdfOutputDir: URL
    ) throws -> PrintJobResult {
        let cleanData = stripEOT(data)
        guard !cleanData.isEmpty, cleanData.count <= maxJobBytes else {
            throw PrintError.invalidJob("The print job is empty or too large.")
        }
        let cleanName = sanitizeJobName(jobName)
        let isPDF = isPDFData(cleanData)
        let trimmedTarget = targetPrinter?.trimmingCharacters(in: .whitespacesAndNewlines)

        if trimmedTarget == pdfTargetValue {
            let fm = FileManager.default
            try fm.createDirectory(at: pdfOutputDir, withIntermediateDirectories: true)
            let tmpURL = pdfOutputDir.appendingPathComponent(".print-\(UUID().uuidString).pdf")
            defer { try? fm.removeItem(at: tmpURL) }

            if isPDF {
                try cleanData.write(to: tmpURL, options: .atomic)
            } else {
                try convertPostScriptToPDF(cleanData, outputURL: tmpURL)
            }
            let size = (try? fm.attributesOfItem(atPath: tmpURL.path)[.size] as? NSNumber)?.intValue ?? 0
            guard size > 0 else {
                throw PrintError.convertFailed("Converted PDF is empty.")
            }

            let formatter = DateFormatter()
            formatter.locale = Locale(identifier: "en_US_POSIX")
            formatter.dateFormat = "yyyy-MM-dd HH.mm.ss"
            let baseName = "\(formatter.string(from: Date())) - \(cleanName)"

            var dstURL = pdfOutputDir.appendingPathComponent("\(baseName).pdf")
            var n = 1
            while fm.fileExists(atPath: dstURL.path) {
                n += 1
                dstURL = pdfOutputDir.appendingPathComponent("\(baseName) (\(n)).pdf")
            }
            chmod(tmpURL.path, 0o644)
            try fm.moveItem(at: tmpURL, to: dstURL)
            let finalSize = (try? fm.attributesOfItem(atPath: dstURL.path)[.size] as? NSNumber)?.intValue ?? size
            return PrintJobResult(jobName: cleanName, target: pdfTargetValue, bytes: finalSize, savedPDFURL: dstURL)
        }

        let queue: String
        if let t = trimmedTarget, !t.isEmpty {
            queue = t
        } else if let def = defaultPrinterQueue(), !def.isEmpty {
            queue = def
        } else {
            throw PrintError.printerUnavailable(
                "No default printer is configured on this Mac. Choose a printer or Save as PDF in Get Info \u{2192} Printing."
            )
        }

        let fm = FileManager.default
        let workDir = fm.temporaryDirectory.appendingPathComponent("classicmac-print-\(UUID().uuidString)", isDirectory: true)
        try fm.createDirectory(at: workDir, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: workDir) }

        if isPDF {
            let pdfURL = workDir.appendingPathComponent("job.pdf")
            try cleanData.write(to: pdfURL)
            let res = runTool("/usr/bin/lp", arguments: ["-d", queue, "-t", cleanName, "--", pdfURL.path], timeout: 30.0)
            guard res.exitCode == 0 else {
                let errText = res.stderr.trimmingCharacters(in: .whitespacesAndNewlines)
                throw PrintError.submitFailed(errText.isEmpty ? "lp failed with status \(res.exitCode)." : errText)
            }
            return PrintJobResult(jobName: cleanName, target: queue, bytes: cleanData.count, savedPDFURL: nil)
        }

        if ghostscriptPath() != nil {
            let pdfURL = workDir.appendingPathComponent("job.pdf")
            try convertPostScriptToPDF(cleanData, outputURL: pdfURL)
            let res = runTool("/usr/bin/lp", arguments: ["-d", queue, "-t", cleanName, "--", pdfURL.path], timeout: 30.0)
            guard res.exitCode == 0 else {
                let errText = res.stderr.trimmingCharacters(in: .whitespacesAndNewlines)
                throw PrintError.submitFailed(errText.isEmpty ? "lp failed with status \(res.exitCode)." : errText)
            }
            let pdfSize = (try? fm.attributesOfItem(atPath: pdfURL.path)[.size] as? NSNumber)?.intValue ?? cleanData.count
            return PrintJobResult(jobName: cleanName, target: queue, bytes: pdfSize, savedPDFURL: nil)
        }

        let psURL = workDir.appendingPathComponent("job.ps")
        try cleanData.write(to: psURL)
        let res = runTool(
            "/usr/bin/lp",
            arguments: ["-d", queue, "-t", cleanName, "-o", "document-format=application/postscript", "--", psURL.path],
            timeout: 30.0
        )
        guard res.exitCode == 0 else {
            let errText = res.stderr.trimmingCharacters(in: .whitespacesAndNewlines)
            let detail = errText.isEmpty ? "lp exited with status \(res.exitCode)." : errText
            throw PrintError.submitFailed(
                "\(detail) AirPrint and raster printers require Ghostscript (install with: brew install ghostscript)."
            )
        }
        return PrintJobResult(jobName: cleanName, target: queue, bytes: cleanData.count, savedPDFURL: nil)
    }

    // MARK: Process runner

    private struct ToolOutput {
        let exitCode: Int32
        let stdout: String
        let stderr: String
        let timedOut: Bool
    }

    private static func runTool(
        _ executablePath: String,
        arguments: [String],
        stdinData: Data? = nil,
        timeout: TimeInterval
    ) -> ToolOutput {
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: executablePath)
        proc.arguments = arguments
        var env = ProcessInfo.processInfo.environment
        env["LC_ALL"] = "C"
        proc.environment = env

        let outPipe = Pipe()
        let errPipe = Pipe()
        proc.standardOutput = outPipe
        proc.standardError = errPipe

        let inPipe: Pipe?
        if stdinData != nil {
            let p = Pipe()
            proc.standardInput = p
            inPipe = p
        } else {
            proc.standardInput = FileHandle.nullDevice
            inPipe = nil
        }

        let outBox = DataBox()
        let errBox = DataBox()
        let group = DispatchGroup()

        group.enter()
        DispatchQueue.global(qos: .userInitiated).async {
            while true {
                let chunk = outPipe.fileHandleForReading.availableData
                if chunk.isEmpty { break }
                outBox.append(chunk)
            }
            group.leave()
        }

        group.enter()
        DispatchQueue.global(qos: .userInitiated).async {
            while true {
                let chunk = errPipe.fileHandleForReading.availableData
                if chunk.isEmpty { break }
                errBox.append(chunk)
            }
            group.leave()
        }

        let done = DispatchSemaphore(value: 0)
        proc.terminationHandler = { _ in done.signal() }

        do {
            try proc.run()
        } catch {
            try? outPipe.fileHandleForReading.close()
            try? errPipe.fileHandleForReading.close()
            if let inPipe { try? inPipe.fileHandleForWriting.close() }
            return ToolOutput(exitCode: 127, stdout: "", stderr: error.localizedDescription, timedOut: false)
        }

        if let inPipe, let stdinData {
            DispatchQueue.global(qos: .userInitiated).async {
                inPipe.fileHandleForWriting.write(stdinData)
                try? inPipe.fileHandleForWriting.close()
            }
        }

        let timedOut = done.wait(timeout: .now() + timeout) == .timedOut
        if timedOut {
            proc.terminate()
            _ = done.wait(timeout: .now() + 2.0)
            if proc.isRunning {
                kill(proc.processIdentifier, SIGKILL)
                _ = done.wait(timeout: .now() + 1.0)
            }
        }
        _ = group.wait(timeout: .now() + 2.0)
        return ToolOutput(
            exitCode: timedOut ? 124 : proc.terminationStatus,
            stdout: outBox.string(),
            stderr: errBox.string(),
            timedOut: timedOut
        )
    }
}
