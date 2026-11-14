import AppKit
import CDisplaySocket

/// Message types of the ClassicMac display protocol (display/README.md): the
/// RetroBoot presenter protocol over a stream socket, plus cursor messages.
enum DisplayMessage: UInt32 {
    case hello = 1, surface = 2, damage = 3, guestEvent = 4, cursor = 5, mouse = 6
    case key = 16, pointerAbsolute = 17, button = 18, resize = 19, releaseAll = 20
    case refreshRate = 21, pointerRelative = 22
}

/// What the guest window shows, published on the main thread.
@MainActor
final class GuestDisplayState: ObservableObject {
    @Published var connected = false
    @Published var surfaceSize = CGSize.zero
    /// Mac OS has drawn its own startup screen: the startup curtain lifts.
    @Published var revealed = false
    /// The guest's active pointing device takes relative motion (games).
    @Published var pointerRelative = false
    @Published var cursorImage: CGImage?
    @Published var cursorHotSpot = CGPoint.zero
    @Published var cursorVisible = true
    /// Bumped on every new guest connection (each start or restart).
    @Published var bootCount = 0
}

/// One running Mac's screen and input, between ClassicMac and its QEMU
/// process. QEMU connects to this session's socket and hands over its
/// framebuffer as shared memory; ClassicMac sends keyboard and pointer input.
final class GuestDisplaySession: @unchecked Sendable {
    let vmID: UUID
    let socketPath: String
    let family: MachineFamily
    @MainActor let state = GuestDisplayState()

    private let queue = DispatchQueue(label: "com.classicmac.display.io", qos: .userInteractive)
    private var listenFD: Int32 = -1
    private var connectionFD: Int32 = -1
    private var listenSource: DispatchSourceRead?
    private var readSource: DispatchSourceRead?
    private var inbox: [UInt8] = []
    private var receivedFDs: [Int32] = []
    private var revealDeadline: DispatchWorkItem?
    /// Milliseconds between frames QEMU sends: the display's refresh rate.
    var refreshInterval: UInt32 = 16 {
        didSet { if refreshInterval != oldValue { send(.refreshRate, [refreshInterval]) } }
    }

    // The shared framebuffer, guarded by frameLock.
    private let frameLock = NSLock()
    private var mapping: UnsafeRawPointer?
    private var mappingSize = 0
    private var width = 0
    private var height = 0
    private var stride = 0
    private var frameDirty = false
    private var revealed = false

    static func socketPath(for id: UUID) -> String {
        let dir = URL(fileURLWithPath: "/tmp/ClassicMac", isDirectory: true)
        AppPaths.ensureDirectory(dir)
        return dir.appendingPathComponent("\(id.uuidString).display.sock").path
    }

    init(vmID: UUID, family: MachineFamily) {
        self.vmID = vmID
        self.family = family
        self.socketPath = Self.socketPath(for: vmID)
    }

    // MARK: Lifecycle

    /// Starts listening for QEMU. Call before launching the emulator.
    func start() throws {
        try queue.sync {
            guard listenFD < 0 else { return }
            let fd = cds_listen(socketPath)
            guard fd >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: -fd) ?? .EIO) }
            listenFD = fd
            let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
            source.setEventHandler { [weak self] in self?.acceptConnection() }
            source.resume()
            listenSource = source
        }
    }

    func stop() {
        queue.sync {
            dropConnection()
            listenSource?.cancel()
            listenSource = nil
            if listenFD >= 0 { cds_close(listenFD) }
            listenFD = -1
            unlink(socketPath)
            frameLock.lock()
            cds_unmap(mapping, mappingSize)
            mapping = nil
            mappingSize = 0
            frameLock.unlock()
        }
    }

    private func acceptConnection() {
        let fd = cds_accept(listenFD)
        guard fd >= 0 else { return }
        dropConnection()
        connectionFD = fd
        inbox.removeAll(keepingCapacity: true)
        let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler { [weak self] in self?.readAvailable() }
        source.resume()
        readSource = source
    }

    private func dropConnection() {
        readSource?.cancel()
        readSource = nil
        if connectionFD >= 0 { cds_close(connectionFD) }
        connectionFD = -1
        for fd in receivedFDs { cds_close(fd) }
        receivedFDs.removeAll()
        Task { @MainActor [state] in state.connected = false }
    }

    // MARK: Receiving

    private func readAvailable() {
        var buffer = [UInt8](repeating: 0, count: 64 * 1024)
        var fds = [Int32](repeating: -1, count: 8)
        var count: Int32 = 0
        let n = buffer.withUnsafeMutableBytes { raw in
            cds_recv(connectionFD, raw.baseAddress, raw.count, &fds, 8, &count)
        }
        if n == -Int(EAGAIN) || n == -Int(EINTR) { return }
        guard n > 0 else {
            dropConnection()
            return
        }
        receivedFDs += fds.prefix(Int(count))
        inbox += buffer.prefix(n)
        var offset = 0
        while inbox.count - offset >= 8 {
            let type = read32(inbox, offset)
            let length = Int(read32(inbox, offset + 4))
            guard length <= 64 * 1024 * 1024 else {
                dropConnection()
                return
            }
            guard inbox.count - offset - 8 >= length else { break }
            handle(type: type, payload: inbox[(offset + 8)..<(offset + 8 + length)])
            offset += 8 + length
        }
        if offset > 0 { inbox.removeFirst(offset) }
    }

    private func handle(type: UInt32, payload: ArraySlice<UInt8>) {
        let p = Array(payload)
        switch DisplayMessage(rawValue: type) {
        case .hello:
            frameLock.lock()
            revealed = false
            frameLock.unlock()
            scheduleRevealDeadline()
            send(.refreshRate, [refreshInterval])
            Task { @MainActor [state] in
                state.connected = true
                state.revealed = false
                state.bootCount += 1
            }
        case .surface:
            guard p.count >= 24, !receivedFDs.isEmpty else { return }
            let fd = receivedFDs.removeFirst()
            defer { cds_close(fd) }
            let w = Int(read32(p, 0)), h = Int(read32(p, 4)), s = Int(read32(p, 8))
            let size = Int(read64(p, 16))
            guard w > 0, h > 0, s >= w * 4, size >= s * h, let map = cds_map(fd, size) else { return }
            frameLock.lock()
            cds_unmap(mapping, mappingSize)
            mapping = map
            mappingSize = size
            width = w
            height = h
            stride = s
            frameDirty = true
            frameLock.unlock()
            checkReveal()
            Task { @MainActor [state] in state.surfaceSize = CGSize(width: w, height: h) }
        case .damage:
            frameLock.lock()
            frameDirty = true
            frameLock.unlock()
            checkReveal()
        case .guestEvent:
            guard let json = try? JSONSerialization.jsonObject(with: Data(p)) as? [String: Any],
                  let event = json["event"] as? String else { return }
            if event == "pointer-mode", let relative = json["relative"] as? Bool {
                Task { @MainActor [state] in state.pointerRelative = relative }
            }
        case .cursor:
            guard p.count >= 16 else { return }
            let hotX = Int(Int32(bitPattern: read32(p, 0))), hotY = Int(Int32(bitPattern: read32(p, 4)))
            let w = Int(read32(p, 8)), h = Int(read32(p, 12))
            guard w > 0, h > 0, w <= 256, h <= 256, p.count >= 16 + w * h * 4 else { return }
            let image = Self.cursorImage(Array(p[16..<(16 + w * h * 4)]), width: w, height: h)
            Task { @MainActor [state] in
                state.cursorImage = image
                state.cursorHotSpot = CGPoint(x: hotX, y: hotY)
            }
        case .mouse:
            guard p.count >= 12 else { return }
            let visible = read32(p, 8) != 0
            Task { @MainActor [state] in state.cursorVisible = visible }
        default:
            break
        }
    }

    /// QEMUCursor pixels: 0xAARRGGBB words, not premultiplied.
    private static func cursorImage(_ bytes: [UInt8], width: Int, height: Int) -> CGImage? {
        guard let provider = CGDataProvider(data: Data(bytes) as CFData) else { return nil }
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                       bytesPerRow: width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.first.rawValue |
                                                CGBitmapInfo.byteOrder32Little.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }

    // MARK: The startup curtain

    /// Firmware screens (black, OpenBIOS yellow or white) stay behind the
    /// startup curtain; the first frame Mac OS draws itself lifts it.
    private func checkReveal() {
        frameLock.lock()
        guard !revealed, let base = mapping, width > 0, height > 0 else {
            frameLock.unlock()
            return
        }
        let reveal = GuestBootFrame.isReady(base, width: width, height: height, stride: stride, family: family)
        if reveal { revealed = true }
        frameLock.unlock()
        if reveal { publishReveal() }
    }

    private func scheduleRevealDeadline() {
        revealDeadline?.cancel()
        // Never hide a Mac that stays in firmware (for example an empty disk).
        let item = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.frameLock.lock()
            let wasRevealed = self.revealed
            self.revealed = true
            self.frameLock.unlock()
            if !wasRevealed { self.publishReveal() }
        }
        revealDeadline = item
        queue.asyncAfter(deadline: .now() + 75, execute: item)
    }

    private func publishReveal() {
        Task { @MainActor [self, state] in
            // Install the ready frame before the fade, including when the
            // display link paused while the guest was covered or occluded.
            NotificationCenter.default.post(name: GuestScreenNSView.refreshNotification, object: nil)
            DebugSnapshots.recordStartup(self)
            state.revealed = true
        }
    }

    // MARK: Frames

    /// A copy of the current frame when it changed since the last call.
    func takeFrame(force: Bool = false) -> CGImage? {
        frameLock.lock()
        defer { frameLock.unlock() }
        guard let base = mapping, width > 0, height > 0, frameDirty || force else { return nil }
        frameDirty = false
        let data = Data(bytes: base, count: stride * height)
        guard let provider = CGDataProvider(data: data as CFData) else { return nil }
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                       bytesPerRow: stride, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipFirst.rawValue |
                                                CGBitmapInfo.byteOrder32Little.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }

    // MARK: Sending

    func send(_ type: DisplayMessage, _ words: [UInt32] = []) {
        var message = [UInt8]()
        message.reserveCapacity(8 + words.count * 4)
        append32(&message, type.rawValue)
        append32(&message, UInt32(words.count * 4))
        for word in words { append32(&message, word) }
        queue.async { [weak self] in
            guard let self, self.connectionFD >= 0 else { return }
            let result = message.withUnsafeBytes { cds_send_all(self.connectionFD, $0.baseAddress, $0.count) }
            if result < 0 && result != -Int(EAGAIN) { self.dropConnection() }
        }
    }

    func key(_ qcode: UInt32, down: Bool) { send(.key, [qcode, down ? 1 : 0]) }
    func pointer(x: Int, y: Int) { send(.pointerAbsolute, [UInt32(max(0, x)), UInt32(max(0, y))]) }
    func button(_ button: UInt32, down: Bool) { send(.button, [button, down ? 1 : 0]) }
    func resize(width: Int, height: Int) { send(.resize, [UInt32(width), UInt32(height)]) }
    func releaseAll() { send(.releaseAll) }

    func pointerDelta(dx: Int, dy: Int) {
        let clampedX = Int32(max(-32767, min(32767, dx))), clampedY = Int32(max(-32767, min(32767, dy)))
        send(.pointerRelative, [UInt32(bitPattern: clampedX), UInt32(bitPattern: clampedY)])
    }

    // MARK: Bytes

    private func read32(_ bytes: [UInt8], _ offset: Int) -> UInt32 {
        UInt32(bytes[offset]) | UInt32(bytes[offset + 1]) << 8 | UInt32(bytes[offset + 2]) << 16 | UInt32(bytes[offset + 3]) << 24
    }

    private func read64(_ bytes: [UInt8], _ offset: Int) -> UInt64 {
        UInt64(read32(bytes, offset)) | UInt64(read32(bytes, offset + 4)) << 32
    }

    private func append32(_ bytes: inout [UInt8], _ value: UInt32) {
        bytes += [UInt8(value & 0xFF), UInt8(value >> 8 & 0xFF), UInt8(value >> 16 & 0xFF), UInt8(value >> 24)]
    }
}
