import AppKit
import IOSurface
import CDisplaySocket

/// Message types of the ClassicMac display protocol (display/README.md): the
/// RetroBoot presenter protocol over a stream socket, plus cursor messages.
enum DisplayMessage: UInt32 {
    case hello = 1, surface = 2, damage = 3, guestEvent = 4, cursor = 5, mouse = 6
    case key = 16, pointerAbsolute = 17, button = 18, resize = 19, releaseAll = 20
    case refreshRate = 21, pointerRelative = 22, gamepad = 23
}

struct DamageRect: Equatable {
    let x: Int
    let y: Int
    let w: Int
    let h: Int
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

    private static let sRGBColorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
    static let maxSurfaceDimension = 16_384
    static let maxSurfaceStride = 65_536

    private static let maxOutboxBytes = 256 * 1024
    private static let maxDamageRects = 64

    private let queue = DispatchQueue(label: "com.classicmac.display.io", qos: .userInteractive)
    private var listenFD: Int32 = -1
    private var connectionFD: Int32 = -1
    private var listenSource: DispatchSourceRead?
    private var readSource: DispatchSourceRead?
    private var writeSource: DispatchSourceWrite?
    private var writeSourceResumed = false
    private var inbox: [UInt8] = []
    private var outbox: [UInt8] = []
    private var outboxOffset = 0
    private var pendingPointer: (UInt32, UInt32)?
    private var pendingDelta: (Int, Int)?
    private var pointerFlushScheduled = false
    private var receivedFDs: [Int32] = []
    private var revealDeadline: DispatchWorkItem?
    private var _refreshInterval: UInt32 = 16
    /// Milliseconds between frames QEMU sends: the display's refresh rate.
    var refreshInterval: UInt32 {
        get {
            frameLock.lock()
            defer { frameLock.unlock() }
            return _refreshInterval
        }
        set {
            frameLock.lock()
            let changed = newValue != _refreshInterval
            _refreshInterval = newValue
            frameLock.unlock()
            if changed { send(.refreshRate, [newValue]) }
        }
    }

    // The shared framebuffer and double-buffered IOSurfaces, guarded by frameLock.
    private let frameLock = NSLock()
    private var mapping: UnsafeRawPointer?
    private var mappingSize = 0
    private var width = 0
    private var height = 0
    private var stride = 0
    private var frameGeneration: UInt64 = 0
    private var presentedGeneration: UInt64 = 0
    private var surfaces: [IOSurfaceRef] = []
    private var frontIndex = 0
    private var dirtyFull: [Bool] = [true, true]
    private var dirtyRects: [[DamageRect]] = [[], []]
    private var revealed = false

    var currentFrameGeneration: UInt64 {
        frameLock.lock()
        defer { frameLock.unlock() }
        return frameGeneration
    }

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
            revealDeadline?.cancel()
            revealDeadline = nil
            frameLock.lock()
            cds_unmap(mapping, mappingSize)
            mapping = nil
            mappingSize = 0
            width = 0
            height = 0
            stride = 0
            surfaces.removeAll()
            dirtyFull = [true, true]
            dirtyRects = [[], []]
            frameLock.unlock()
        }
    }

    private func acceptConnection() {
        let fd = cds_accept(listenFD)
        guard fd >= 0 else { return }
        dropConnection()
        connectionFD = fd
        inbox.removeAll(keepingCapacity: true)
        let readSrc = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        readSrc.setEventHandler { [weak self] in self?.readAvailable() }
        readSrc.resume()
        readSource = readSrc

        let writeSrc = DispatchSource.makeWriteSource(fileDescriptor: fd, queue: queue)
        writeSrc.setEventHandler { [weak self] in self?.flushOutbox() }
        writeSource = writeSrc
        writeSourceResumed = false
    }

    func dropConnection() {
        readSource?.cancel()
        readSource = nil
        if let writeSource {
            if !writeSourceResumed { writeSource.resume() }
            writeSource.cancel()
        }
        writeSource = nil
        writeSourceResumed = false
        outbox.removeAll(keepingCapacity: true)
        outboxOffset = 0
        pendingPointer = nil
        pendingDelta = nil
        pointerFlushScheduled = false
        if connectionFD >= 0 { cds_close(connectionFD) }
        connectionFD = -1
        for fd in receivedFDs { cds_close(fd) }
        receivedFDs.removeAll()
        Task { @MainActor [state] in
            state.connected = false
            state.pointerRelative = false
            state.cursorVisible = true
            state.cursorImage = nil
            state.cursorHotSpot = .zero
        }
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

    static func isValidSurfaceGeometry(width w: Int, height h: Int, stride s: Int, byteSize: UInt64) -> Bool {
        guard w > 0, h > 0,
              w <= maxSurfaceDimension, h <= maxSurfaceDimension,
              s >= w * 4, s <= maxSurfaceStride else { return false }
        let (requiredBytes, overflow) = s.multipliedReportingOverflow(by: h)
        guard !overflow, byteSize >= UInt64(requiredBytes), Int(exactly: byteSize) != nil else { return false }
        return true
    }

    private static func makeIOSurface(width: Int, height: Int) -> IOSurfaceRef? {
        let alignedStride = (width * 4 + 15) & ~15
        let props: [CFString: Any] = [
            kIOSurfaceWidth: width,
            kIOSurfaceHeight: height,
            kIOSurfaceBytesPerElement: 4,
            kIOSurfaceBytesPerRow: alignedStride,
            kIOSurfaceAllocSize: alignedStride * height,
            kIOSurfacePixelFormat: UInt32(0x42475241), // 'BGRA'
        ]
        guard let surface = IOSurfaceCreate(props as CFDictionary) else { return nil }
        IOSurfaceSetValue(surface, "IOSurfaceColorSpace" as CFString, CGColorSpace.sRGB)
        return surface
    }

    static func parseDamageRects(_ payload: [UInt8], width: Int, height: Int) -> [DamageRect]? {
        guard width > 0, height > 0, payload.count >= 8 else { return nil }
        let count = Int(read32Static(payload, 4))
        guard count > 0, count <= maxDamageRects, payload.count >= 8 + count * 16 else { return nil }
        var rects: [DamageRect] = []
        rects.reserveCapacity(count)
        for i in 0..<count {
            let base = 8 + i * 16
            let rx = Int(Int32(bitPattern: read32Static(payload, base)))
            let ry = Int(Int32(bitPattern: read32Static(payload, base + 4)))
            let rw = Int(Int32(bitPattern: read32Static(payload, base + 8)))
            let rh = Int(Int32(bitPattern: read32Static(payload, base + 12)))
            guard rw > 0, rh > 0 else { continue }
            let x0 = max(0, rx)
            let y0 = max(0, ry)
            let x1 = min(width, rx.saturatingAdd(rw))
            let y1 = min(height, ry.saturatingAdd(rh))
            if x1 > x0 && y1 > y0 {
                rects.append(DamageRect(x: x0, y: y0, w: x1 - x0, h: y1 - y0))
            }
        }
        return rects.isEmpty ? nil : rects
    }

    func handle(type: UInt32, payload: ArraySlice<UInt8>) {
        let p = Array(payload)
        switch DisplayMessage(rawValue: type) {
        case .hello:
            frameLock.lock()
            revealed = false
            let interval = _refreshInterval
            frameLock.unlock()
            scheduleRevealDeadline()
            send(.refreshRate, [interval])
            Task { @MainActor [state] in
                state.connected = true
                state.revealed = false
                state.pointerRelative = false
                state.cursorVisible = true
                state.cursorImage = nil
                state.cursorHotSpot = .zero
                state.bootCount += 1
            }
        case .surface:
            guard p.count >= 24, !receivedFDs.isEmpty else { return }
            let fd = receivedFDs.removeFirst()
            defer { cds_close(fd) }
            let w = Int(read32(p, 0)), h = Int(read32(p, 4)), s = Int(read32(p, 8))
            let rawSize = read64(p, 16)
            guard Self.isValidSurfaceGeometry(width: w, height: h, stride: s, byteSize: rawSize),
                  let size = Int(exactly: rawSize),
                  let map = cds_map(fd, size) else { return }
            var newSurfaces: [IOSurfaceRef] = []
            if let s0 = Self.makeIOSurface(width: w, height: h),
               let s1 = Self.makeIOSurface(width: w, height: h) {
                newSurfaces = [s0, s1]
            }
            frameLock.lock()
            cds_unmap(mapping, mappingSize)
            mapping = map
            mappingSize = size
            width = w
            height = h
            stride = s
            surfaces = newSurfaces
            frontIndex = 0
            dirtyFull = [true, true]
            dirtyRects = [[], []]
            frameGeneration &+= 1
            frameLock.unlock()
            checkReveal()
            Task { @MainActor [state] in state.surfaceSize = CGSize(width: w, height: h) }
        case .damage:
            frameLock.lock()
            if let rects = Self.parseDamageRects(p, width: width, height: height) {
                for idx in 0..<2 where !dirtyFull[idx] {
                    dirtyRects[idx].append(contentsOf: rects)
                    if dirtyRects[idx].count > Self.maxDamageRects {
                        var minX = Int.max, minY = Int.max, maxX = 0, maxY = 0
                        for r in dirtyRects[idx] {
                            minX = min(minX, r.x)
                            minY = min(minY, r.y)
                            maxX = max(maxX, r.x + r.w)
                            maxY = max(maxY, r.y + r.h)
                        }
                        dirtyRects[idx].removeAll(keepingCapacity: true)
                        if (maxX - minX) * (maxY - minY) * 10 >= width * height * 7 {
                            dirtyFull[idx] = true
                        } else {
                            dirtyRects[idx].append(DamageRect(x: minX, y: minY, w: maxX - minX, h: maxY - minY))
                        }
                    }
                }
            } else {
                dirtyFull = [true, true]
                dirtyRects = [[], []]
            }
            frameGeneration &+= 1
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
                       bytesPerRow: width * 4, space: sRGBColorSpace,
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

    /// Presents the latest frame for display rendering (zero-copy `IOSurface` when
    /// another view already updated this generation, otherwise partial damage copy
    /// into the back `IOSurface`). Does not affect unrelated viewers' generations.
    func presentFrame(lastGeneration: inout UInt64, force: Bool = false) -> AnyObject? {
        frameLock.lock()
        defer { frameLock.unlock() }
        guard let base = mapping, width > 0, height > 0 else { return nil }
        guard force || lastGeneration != frameGeneration else { return nil }
        if surfaces.count == 2 {
            if presentedGeneration == frameGeneration {
                lastGeneration = frameGeneration
                return surfaces[frontIndex]
            }
            let nextIndex = 1 - frontIndex
            let surface = surfaces[nextIndex]
            let dstStride = IOSurfaceGetBytesPerRow(surface)
            IOSurfaceLock(surface, [], nil)
            let dstBase = IOSurfaceGetBaseAddress(surface)
            if dirtyFull[nextIndex] {
                cds_copy_xrgb_opaque(dstBase, dstStride, base, stride, width, height)
            } else {
                for r in dirtyRects[nextIndex] {
                    let xOffset = r.x * 4
                    cds_copy_xrgb_opaque(
                        dstBase.advanced(by: r.y * dstStride + xOffset),
                        dstStride,
                        base.advanced(by: r.y * stride + xOffset),
                        stride,
                        r.w,
                        r.h
                    )
                }
            }
            IOSurfaceUnlock(surface, [], nil)
            dirtyFull[nextIndex] = false
            dirtyRects[nextIndex].removeAll(keepingCapacity: true)
            frontIndex = nextIndex
            presentedGeneration = frameGeneration
            lastGeneration = frameGeneration
            return surface
        }
        lastGeneration = frameGeneration
        return makeCGImageLocked(base: base)
    }

    /// Returns a standalone `CGImage` copy of the current framebuffer without
    /// altering display presentation state.
    func snapshotImage() -> CGImage? {
        frameLock.lock()
        defer { frameLock.unlock() }
        guard let base = mapping, width > 0, height > 0 else { return nil }
        return makeCGImageLocked(base: base)
    }

    /// Compatibility wrapper returning a `CGImage` snapshot of the current frame.
    func takeFrame(force: Bool = false) -> CGImage? {
        _ = force
        return snapshotImage()
    }

    private func makeCGImageLocked(base: UnsafeRawPointer) -> CGImage? {
        let data = Data(bytes: base, count: stride * height)
        guard let provider = CGDataProvider(data: data as CFData) else { return nil }
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                       bytesPerRow: stride, space: Self.sRGBColorSpace,
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipFirst.rawValue |
                                                CGBitmapInfo.byteOrder32Little.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }

    // MARK: Sending

    func send(_ type: DisplayMessage, _ words: [UInt32] = []) {
        queue.async { [weak self] in
            guard let self, self.connectionFD >= 0 else { return }
            if type == .button || type == .key || type == .gamepad || type == .releaseAll {
                self.enqueuePendingMotion()
            }
            self.enqueueMessage(type, words)
            self.flushOutbox()
        }
    }

    func key(_ qcode: UInt32, down: Bool) { send(.key, [qcode, down ? 1 : 0]) }
    func gamepad(buttons: UInt32) { send(.gamepad, [buttons]) }

    func pointer(x: Int, y: Int) {
        let px = UInt32(max(0, x))
        let py = UInt32(max(0, y))
        queue.async { [weak self] in
            guard let self, self.connectionFD >= 0 else { return }
            self.pendingPointer = (px, py)
            self.schedulePointerFlush()
        }
    }

    func button(_ button: UInt32, down: Bool) { send(.button, [button, down ? 1 : 0]) }
    func resize(width: Int, height: Int) { send(.resize, [UInt32(width), UInt32(height)]) }
    func releaseAll() { send(.releaseAll) }

    func pointerDelta(dx: Int, dy: Int) {
        queue.async { [weak self] in
            guard let self, self.connectionFD >= 0 else { return }
            let prev = self.pendingDelta ?? (0, 0)
            self.pendingDelta = (prev.0 + dx, prev.1 + dy)
            self.schedulePointerFlush()
        }
    }

    private func schedulePointerFlush() {
        guard !pointerFlushScheduled else { return }
        pointerFlushScheduled = true
        queue.async { [weak self] in
            guard let self else { return }
            self.pointerFlushScheduled = false
            guard self.connectionFD >= 0 else { return }
            self.enqueuePendingMotion()
            self.flushOutbox()
        }
    }

    private func enqueuePendingMotion() {
        if let (px, py) = pendingPointer {
            pendingPointer = nil
            enqueueMessage(.pointerAbsolute, [px, py])
        }
        if let (dx, dy) = pendingDelta {
            pendingDelta = nil
            let clampedX = Int32(max(-32767, min(32767, dx)))
            let clampedY = Int32(max(-32767, min(32767, dy)))
            enqueueMessage(.pointerRelative, [UInt32(bitPattern: clampedX), UInt32(bitPattern: clampedY)])
        }
    }

    private func enqueueMessage(_ type: DisplayMessage, _ words: [UInt32]) {
        if outboxOffset == outbox.count {
            outbox.removeAll(keepingCapacity: true)
            outboxOffset = 0
        } else if outboxOffset > 4096 && outboxOffset * 2 >= outbox.count {
            outbox.removeFirst(outboxOffset)
            outboxOffset = 0
        }
        append32(&outbox, type.rawValue)
        append32(&outbox, UInt32(words.count * 4))
        for word in words { append32(&outbox, word) }
        if outbox.count - outboxOffset > Self.maxOutboxBytes {
            dropConnection()
        }
    }

    private func flushOutbox() {
        while connectionFD >= 0 && outboxOffset < outbox.count {
            let remaining = outbox.count - outboxOffset
            let sent = outbox.withUnsafeBytes { raw -> Int in
                guard let base = raw.baseAddress else { return 0 }
                return cds_send_nonblocking(connectionFD, base.advanced(by: outboxOffset), remaining)
            }
            if sent > 0 {
                outboxOffset += sent
                continue
            }
            if sent == -Int(EAGAIN) {
                armWriteSource(true)
                return
            }
            dropConnection()
            return
        }
        outbox.removeAll(keepingCapacity: true)
        outboxOffset = 0
        armWriteSource(false)
    }

    private func armWriteSource(_ active: Bool) {
        guard let writeSource else { return }
        if active && !writeSourceResumed {
            writeSourceResumed = true
            writeSource.resume()
        } else if !active && writeSourceResumed {
            writeSourceResumed = false
            writeSource.suspend()
        }
    }

    // MARK: Bytes

    private static func read32Static(_ bytes: [UInt8], _ offset: Int) -> UInt32 {
        UInt32(bytes[offset]) | UInt32(bytes[offset + 1]) << 8 | UInt32(bytes[offset + 2]) << 16 | UInt32(bytes[offset + 3]) << 24
    }

    private func read32(_ bytes: [UInt8], _ offset: Int) -> UInt32 {
        Self.read32Static(bytes, offset)
    }

    private func read64(_ bytes: [UInt8], _ offset: Int) -> UInt64 {
        UInt64(read32(bytes, offset)) | UInt64(read32(bytes, offset + 4)) << 32
    }

    private func append32(_ bytes: inout [UInt8], _ value: UInt32) {
        bytes += [UInt8(value & 0xFF), UInt8(value >> 8 & 0xFF), UInt8(value >> 16 & 0xFF), UInt8(value >> 24)]
    }
}

private extension Int {
    func saturatingAdd(_ other: Int) -> Int {
        let (result, overflow) = addingReportingOverflow(other)
        if !overflow { return result }
        return other >= 0 ? Int.max : Int.min
    }
}
