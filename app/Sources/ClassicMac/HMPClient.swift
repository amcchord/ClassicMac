import Foundation
import Darwin

// Minimal client for QEMU's human monitor protocol (HMP) over a unix socket.
// Used to send fire-and-forget control commands like stop / cont / system_reset.
enum HMPClient {
    @discardableResult
    static func send(_ command: String, socketPath: String) -> Bool {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        if fd < 0 {
            return false
        }
        defer { close(fd) }

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let maxLen = MemoryLayout.size(ofValue: addr.sun_path) - 1
        if socketPath.utf8.count > maxLen {
            return false
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
                connect(fd, sa, length)
            }
        }
        if connectResult != 0 {
            return false
        }

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
