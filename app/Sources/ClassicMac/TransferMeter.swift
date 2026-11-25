import Foundation

/// Sliding-window transfer speed and ETA estimator.
struct TransferMeter: Equatable, Sendable {
    struct Sample: Equatable, Sendable {
        let timestamp: Date
        let bytes: Int64
    }

    static let windowDuration: TimeInterval = 4.0
    static let minimumElapsed: TimeInterval = 0.15

    private(set) var samples: [Sample] = []
    private(set) var lastDone: Int64 = 0
    private(set) var totalBytes: Int64 = 0
    private(set) var bytesPerSecond: Double?
    private(set) var etaSeconds: Double?

    mutating func reset(totalBytes: Int64 = 0) {
        samples.removeAll()
        lastDone = 0
        self.totalBytes = max(0, totalBytes)
        bytesPerSecond = nil
        etaSeconds = nil
    }

    mutating func record(done: Int64, total: Int64, at now: Date = Date()) {
        let clampedDone = max(0, done)
        totalBytes = max(0, total)
        if clampedDone < lastDone {
            samples.removeAll()
            bytesPerSecond = nil
            etaSeconds = nil
        }
        lastDone = clampedDone

        if let last = samples.last, last.timestamp == now {
            samples[samples.count - 1] = Sample(timestamp: now, bytes: clampedDone)
        } else {
            samples.append(Sample(timestamp: now, bytes: clampedDone))
        }

        while samples.count > 1 && now.timeIntervalSince(samples[0].timestamp) > Self.windowDuration {
            samples.removeFirst()
        }

        guard let first = samples.first, let last = samples.last else {
            bytesPerSecond = nil
            etaSeconds = nil
            return
        }
        let elapsed = last.timestamp.timeIntervalSince(first.timestamp)
        let moved = last.bytes - first.bytes
        guard elapsed >= Self.minimumElapsed, moved > 0 else {
            bytesPerSecond = nil
            etaSeconds = nil
            return
        }
        let rate = Double(moved) / elapsed
        bytesPerSecond = rate
        if totalBytes > clampedDone, rate > 0 {
            etaSeconds = Double(totalBytes - clampedDone) / rate
        } else {
            etaSeconds = nil
        }
    }

    static func formatSpeed(_ bytesPerSecond: Double) -> String {
        if bytesPerSecond >= 1_000_000 {
            return String(format: "%.1f MB/s", bytesPerSecond / 1_000_000)
        }
        let kb = max(1, Int((bytesPerSecond / 1_000).rounded()))
        return "\(kb) KB/s"
    }

    static func formatETA(_ seconds: Double) -> String {
        let total = max(1, Int(seconds.rounded()))
        if total < 60 {
            return "\(total) s left"
        }
        if total < 3600 {
            return String(format: "%dm %02ds left", total / 60, total % 60)
        }
        let hours = total / 3600
        let minutes = (total % 3600) / 60
        return String(format: "%dh %02dm left", hours, minutes)
    }
}
