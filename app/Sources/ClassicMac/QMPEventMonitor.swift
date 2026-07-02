import Foundation
import Darwin

// Listens on QEMU's QMP socket for the SHUTDOWN event, which carries the
// reason the emulator is exiting (e.g. "guest-shutdown" vs "guest-reset").
// Power Mac VMs run with -action reboot=shutdown because an in-place mac99
// reset hangs the guest, so a guest-initiated restart surfaces here as a
// SHUTDOWN event with reason "guest-reset" followed by a clean process exit;
// QEMUManager uses the recorded reason to relaunch the VM.
final class QMPEventMonitor: @unchecked Sendable {
    private let socketPath: String
    private let lock = NSLock()
    private var fd: Int32 = -1
    private var recordedReason: String?
    private var finished = false
    private var cancelled = false

    init(socketPath: String) {
        self.socketPath = socketPath
    }

    // The SHUTDOWN reason seen on the event stream, if any arrived before the
    // stream closed.
    var shutdownReason: String? {
        lock.lock()
        defer { lock.unlock() }
        return recordedReason
    }

    private var isFinished: Bool {
        lock.lock()
        defer { lock.unlock() }
        return finished
    }

    func start() {
        DispatchQueue.global(qos: .utility).async {
            self.run()
        }
    }

    // Waits (up to the timeout) for the event stream to close, which happens
    // when QEMU exits, then returns the recorded SHUTDOWN reason. Called from
    // the process termination handler, so the wait is normally instant.
    func shutdownReasonAfterExit(timeout: TimeInterval) async -> String? {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if isFinished {
                break
            }
            try? await Task.sleep(nanoseconds: 50_000_000)
        }
        return shutdownReason
    }

    // Stops listening and unblocks the reader thread.
    func cancel() {
        lock.lock()
        cancelled = true
        let currentFD = fd
        lock.unlock()
        if currentFD >= 0 {
            shutdown(currentFD, SHUT_RDWR)
        }
    }

    private func run() {
        guard let socketFD = connectWithRetry() else {
            markFinished()
            return
        }

        lock.lock()
        if cancelled {
            lock.unlock()
            close(socketFD)
            markFinished()
            return
        }
        fd = socketFD
        lock.unlock()

        // QMP delivers events only after capability negotiation. The greeting
        // and the command response are consumed by the read loop below.
        let handshake = "{\"execute\":\"qmp_capabilities\"}\n"
        _ = handshake.withCString { ptr in
            write(socketFD, ptr, strlen(ptr))
        }

        var buffer = Data()
        var chunk = [UInt8](repeating: 0, count: 4096)
        while true {
            let count = read(socketFD, &chunk, chunk.count)
            if count <= 0 {
                break
            }
            buffer.append(contentsOf: chunk[0..<count])
            while let newline = buffer.firstIndex(of: 0x0A) {
                let line = buffer.prefix(upTo: newline)
                buffer.removeSubrange(...newline)
                handle(line: line)
            }
        }

        close(socketFD)
        lock.lock()
        fd = -1
        lock.unlock()
        markFinished()
    }

    private func handle(line: Data) {
        guard
            let object = try? JSONSerialization.jsonObject(with: line) as? [String: Any],
            object["event"] as? String == "SHUTDOWN",
            let data = object["data"] as? [String: Any],
            let reason = data["reason"] as? String
        else {
            return
        }
        lock.lock()
        recordedReason = reason
        lock.unlock()
    }

    private func markFinished() {
        lock.lock()
        finished = true
        lock.unlock()
    }

    // QEMU creates the QMP server socket during startup, moments after the
    // process is spawned; retry briefly so the listener never misses it.
    private func connectWithRetry() -> Int32? {
        let deadline = Date().addingTimeInterval(5)
        while Date() < deadline {
            lock.lock()
            let stop = cancelled
            lock.unlock()
            if stop {
                return nil
            }
            if let socketFD = connectOnce() {
                return socketFD
            }
            usleep(100_000)
        }
        return nil
    }

    private func connectOnce() -> Int32? {
        let socketFD = socket(AF_UNIX, SOCK_STREAM, 0)
        if socketFD < 0 {
            return nil
        }

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let maxLen = MemoryLayout.size(ofValue: addr.sun_path) - 1
        if socketPath.utf8.count > maxLen {
            close(socketFD)
            return nil
        }
        withUnsafeMutablePointer(to: &addr.sun_path) { rawPtr in
            rawPtr.withMemoryRebound(to: CChar.self, capacity: maxLen + 1) { dest in
                _ = socketPath.withCString { src in
                    strncpy(dest, src, maxLen)
                }
            }
        }

        let length = socklen_t(MemoryLayout<sockaddr_un>.size)
        let connectResult = withUnsafePointer(to: &addr) { rawPtr in
            rawPtr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                connect(socketFD, sa, length)
            }
        }
        if connectResult != 0 {
            close(socketFD)
            return nil
        }
        return socketFD
    }
}
