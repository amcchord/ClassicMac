import Foundation
import Darwin

// Minimal client for QEMU's human monitor protocol (HMP) over a unix socket.
// Used to send fire-and-forget control commands like stop / cont / system_reset.
enum HMPClient {
    private static let prompt = Data("(qemu) ".utf8)

    private static func connectedSocket(_ socketPath: String) -> Int32? {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        if fd < 0 {
            return nil
        }

        var noSignal: Int32 = 1
        _ = setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                       &noSignal, socklen_t(MemoryLayout.size(ofValue: noSignal)))
        var timeout = timeval(tv_sec: 2, tv_usec: 0)
        _ = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                       &timeout, socklen_t(MemoryLayout.size(ofValue: timeout)))
        _ = setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                       &timeout, socklen_t(MemoryLayout.size(ofValue: timeout)))

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let maxLen = MemoryLayout.size(ofValue: addr.sun_path) - 1
        if socketPath.utf8.count > maxLen {
            close(fd)
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
        let result = withUnsafePointer(to: &addr) { rawPtr in
            rawPtr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                connect(fd, sa, length)
            }
        }
        if result != 0 {
            close(fd)
            return nil
        }
        return fd
    }

    private static func receivePrompt(_ fd: Int32) -> Data? {
        var response = Data()
        var buffer = [UInt8](repeating: 0, count: 4096)
        while response.count < 1_048_576 {
            let count = recv(fd, &buffer, buffer.count, 0)
            if count <= 0 {
                return nil
            }
            response.append(buffer, count: count)
            if response.count >= prompt.count && response.suffix(prompt.count) == prompt {
                return response
            }
        }
        return nil
    }

    // Sends a command and waits until QEMU confirms completion with its HMP
    // prompt. This is used for screendump, whose output file is complete when
    // the response arrives, and for the startup-clock handoff.
    static func command(_ command: String, socketPath: String) -> String? {
        guard let fd = connectedSocket(socketPath) else { return nil }
        defer { close(fd) }
        guard receivePrompt(fd) != nil else { return nil }

        var line = command
        if !line.hasSuffix("\n") {
            line += "\n"
        }
        let bytes = Array(line.utf8)
        var sent = 0
        while sent < bytes.count {
            let count = bytes.withUnsafeBytes { rawBuffer in
                write(fd, rawBuffer.baseAddress!.advanced(by: sent), bytes.count - sent)
            }
            if count <= 0 {
                return nil
            }
            sent += count
        }
        guard let response = receivePrompt(fd) else { return nil }
        return String(data: response, encoding: .utf8)
    }

    @discardableResult
    static func send(_ command: String, socketPath: String) -> Bool {
        guard let fd = connectedSocket(socketPath) else { return false }
        defer { close(fd) }

        var line = command
        if !line.hasSuffix("\n") {
            line += "\n"
        }
        let written = line.withCString { ptr -> Int in
            write(fd, ptr, strlen(ptr))
        }
        return written > 0
    }
}
