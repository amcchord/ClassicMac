import XCTest
import AppKit
import Darwin
import IOSurface
import SwiftUI
@testable import ClassicMac
@testable import PlatinumKit

@MainActor
final class PlatinumQualityTests: XCTestCase {

    // MARK: - PlatinumWindow modal close policy

    func testWindowValidateMenuItemRespectsCloseBox() {
        let docClosable = PlatinumWindow(title: "Doc", kind: .document, boxes: .document,
                                         contentSize: NSSize(width: 200, height: 150)) { EmptyView() }
        let docNoClose = PlatinumWindow(title: "NoClose", kind: .document, boxes: .none,
                                        contentSize: NSSize(width: 200, height: 150)) { EmptyView() }
        let modal = PlatinumWindow(title: "", kind: .modal, boxes: .none,
                                   contentSize: NSSize(width: 200, height: 120)) { EmptyView() }
        let movable = PlatinumWindow(title: "Info", kind: .movableModal, boxes: .none,
                                     contentSize: NSSize(width: 200, height: 120)) { EmptyView() }

        let closeItem = NSMenuItem(title: "Close Window", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        XCTAssertTrue(docClosable.validateMenuItem(closeItem))
        XCTAssertFalse(docNoClose.validateMenuItem(closeItem))
        XCTAssertFalse(modal.validateMenuItem(closeItem))
        XCTAssertFalse(movable.validateMenuItem(closeItem))

        var modalClosed = false
        modal.onClose = { modalClosed = true }
        modal.performClose(nil)
        XCTAssertFalse(modalClosed, "Modal alert must ignore performClose when boxes.close is false")

        var movableClosed = false
        movable.onClose = { movableClosed = true }
        movable.performClose(nil)
        XCTAssertFalse(movableClosed, "Movable modal without close box must ignore performClose")
    }

    func testModalDialogBlocksParentAndUnblocksOnClose() {
        let parent = PlatinumWindow(title: "Main", kind: .document, boxes: .document,
                                    contentSize: NSSize(width: 400, height: 300)) { EmptyView() }
        let dialog = PlatinumWindow(title: "", kind: .modal, boxes: .none,
                                    contentSize: NSSize(width: 220, height: 120)) { EmptyView() }
        parent.presentModal(dialog)
        XCTAssertTrue(parent.state.isBlockedByModal)
        XCTAssertTrue(parent.hasModalDialog)

        var didClose = false
        dialog.onClose = { didClose = true }
        dialog.close()
        XCTAssertTrue(didClose)
        XCTAssertFalse(parent.state.isBlockedByModal)
        XCTAssertFalse(parent.hasModalDialog)
        parent.close()
    }

    func testWindowManagerQueuesConcurrentErrorsAndResetsOnClose() {
        let wm = WindowManager.shared
        var cleared1 = false
        var cleared2 = false
        wm.presentError(AppError("Error 1", "First message")) { cleared1 = true }
        XCTAssertTrue(wm.showingError)
        wm.presentError(AppError("Error 2", "Second message")) { cleared2 = true }
        XCTAssertTrue(wm.showingError)
        XCTAssertFalse(cleared1)
        XCTAssertFalse(cleared2)

        // Close the active error window programmatically (simulating dismissal)
        let firstWindow = NSApp.windows.reversed().first { $0 is PlatinumWindow } as? PlatinumWindow
        firstWindow?.close()
        XCTAssertTrue(cleared1, "First error clear callback should run when its alert closes")
        XCTAssertTrue(wm.showingError, "Queued second error should immediately present")

        let secondWindow = NSApp.windows.reversed().first { $0 is PlatinumWindow } as? PlatinumWindow
        secondWindow?.close()
        XCTAssertTrue(cleared2, "Second error clear callback should run when its alert closes")
        XCTAssertFalse(wm.showingError, "showingError must reset after all queued errors close")
    }

    // MARK: - PlatinumScrollBar auto-repeat

    func testScrollBarRepeaterReadsLiveOffsetInsteadOfFrozenValue() {
        let repeater = ScrollBarRepeater()
        repeater.metrics = ScrollMetrics(offset: 0, content: 500, visible: 100)
        repeater.line = 20

        var scrolledTo: [CGFloat] = []
        repeater.scrollTo = { next in
            scrolledTo.append(next)
            repeater.metrics.offset = next
        }

        repeater.applyStep { _, line in line }
        repeater.applyStep { _, line in line }
        repeater.applyStep { _, line in line }
        XCTAssertEqual(scrolledTo, [20, 40, 60])

        repeater.metrics.offset = 390
        repeater.applyStep { _, line in line }
        XCTAssertEqual(scrolledTo.last, 400, "Must clamp to maxOffset (content - visible)")

        repeater.metrics.offset = 10
        repeater.applyStep { _, line in -line }
        XCTAssertEqual(scrolledTo.last, 0, "Must clamp downward to 0")

        repeater.start { _, line in line }
        XCTAssertTrue(repeater.isActive)
        repeater.stop()
        XCTAssertFalse(repeater.isActive)
    }

    // MARK: - PlatinumMenu tall menu clamping & accessibility

    func testTallMenuClampsToVisibleScreenAndSupportsScrollAndAccessibility() {
        let items = (0..<80).map { PlatinumMenu.Item(title: "Item \($0)", enabled: true, checked: $0 == 5) }
        let view = PlatinumMenu.MenuView(items: items)
        XCTAssertGreaterThan(view.naturalHeight, 1000)

        let visible = NSRect(x: 0, y: 0, width: 800, height: 300)
        let frame = PlatinumMenu.clampedFrame(naturalWidth: view.naturalWidth,
                                              naturalHeight: view.naturalHeight,
                                              idealOrigin: NSPoint(x: 100, y: -500),
                                              visible: visible)
        XCTAssertEqual(frame.height, 300)
        XCTAssertGreaterThanOrEqual(frame.minY, visible.minY)
        XCTAssertLessThanOrEqual(frame.maxY, visible.maxY)

        view.frame = NSRect(origin: .zero, size: frame.size)
        XCTAssertGreaterThan(view.maxScrollY, 0)
        view.ensureVisible(79)
        XCTAssertGreaterThan(view.scrollY, 0)
        view.scroll(by: 10_000)
        XCTAssertEqual(view.scrollY, view.maxScrollY)
        view.scroll(by: -10_000)
        XCTAssertEqual(view.scrollY, 0)

        XCTAssertEqual(view.accessibilityRole(), .menu)
        XCTAssertEqual(view.accessibilityTitle(), "Menu")
        XCTAssertEqual(view.accessibilityChildren()?.count, items.count)
    }

    // MARK: - GuestDisplaySession bounds & reset

    func testGuestDisplaySessionSurfaceGeometryBounds() {
        XCTAssertTrue(GuestDisplaySession.isValidSurfaceGeometry(width: 1024, height: 768, stride: 4096, byteSize: 4096 * 768))
        XCTAssertTrue(GuestDisplaySession.isValidSurfaceGeometry(width: 16_384, height: 16_384, stride: 65_536, byteSize: 65_536 * 16_384))

        XCTAssertFalse(GuestDisplaySession.isValidSurfaceGeometry(width: 0, height: 480, stride: 2560, byteSize: 2560 * 480))
        XCTAssertFalse(GuestDisplaySession.isValidSurfaceGeometry(width: 640, height: 0, stride: 2560, byteSize: 2560 * 480))
        XCTAssertFalse(GuestDisplaySession.isValidSurfaceGeometry(width: 16_385, height: 480, stride: 16_385 * 4, byteSize: 16_385 * 4 * 480))
        XCTAssertFalse(GuestDisplaySession.isValidSurfaceGeometry(width: 640, height: 16_385, stride: 2560, byteSize: 2560 * 16_385))
        XCTAssertFalse(GuestDisplaySession.isValidSurfaceGeometry(width: 640, height: 480, stride: 65_537, byteSize: 65_537 * 480))
        XCTAssertFalse(GuestDisplaySession.isValidSurfaceGeometry(width: 640, height: 480, stride: 640 * 4 - 4, byteSize: 640 * 4 * 480))
        XCTAssertFalse(GuestDisplaySession.isValidSurfaceGeometry(width: 640, height: 480, stride: 2560, byteSize: 2560 * 480 - 1))
        XCTAssertFalse(GuestDisplaySession.isValidSurfaceGeometry(width: 16_384, height: 16_384, stride: 65_536, byteSize: UInt64.max))
    }

    func testGuestDisplaySessionResetsPointerAndCursorOnHelloAndDrop() async {
        let session = GuestDisplaySession(vmID: UUID(), family: .powerMacG4)
        let color = CGColorSpace(name: CGColorSpace.sRGB)!
        let ctx = CGContext(data: nil, width: 4, height: 4, bitsPerComponent: 8, bytesPerRow: 16,
                            space: color, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        let cursor = ctx.makeImage()!

        session.state.pointerRelative = true
        session.state.cursorVisible = false
        session.state.cursorImage = cursor
        session.handle(type: DisplayMessage.hello.rawValue, payload: ArraySlice([]))
        try? await Task.sleep(nanoseconds: 50_000_000)

        XCTAssertTrue(session.state.connected)
        XCTAssertFalse(session.state.pointerRelative)
        XCTAssertTrue(session.state.cursorVisible)
        XCTAssertNil(session.state.cursorImage)

        session.state.pointerRelative = true
        session.state.cursorVisible = false
        session.state.cursorImage = cursor
        session.dropConnection()
        try? await Task.sleep(nanoseconds: 50_000_000)

        XCTAssertFalse(session.state.connected)
        XCTAssertFalse(session.state.pointerRelative)
        XCTAssertTrue(session.state.cursorVisible)
        XCTAssertNil(session.state.cursorImage)
    }

    func testGuestDisplaySessionRefreshIntervalSynchronization() async {
        let session = GuestDisplaySession(vmID: UUID(), family: .quadra800)
        await withTaskGroup(of: Void.self) { group in
            for i in 0..<20 {
                group.addTask {
                    session.refreshInterval = UInt32(8 + (i % 9))
                    _ = session.refreshInterval
                }
            }
        }
        session.refreshInterval = 12
        XCTAssertTrue((8...16).contains(session.refreshInterval))
    }

    func testGuestDisplaySessionDamageRectParsingAndPartialIOSurfaceUpdates() async throws {
        // 1. parseDamageRects clamping and invalid fallback behavior.
        func le32(_ v: Int32) -> [UInt8] {
            let u = UInt32(bitPattern: v)
            return [UInt8(u & 0xff), UInt8((u >> 8) & 0xff), UInt8((u >> 16) & 0xff), UInt8((u >> 24) & 0xff)]
        }
        var payload = le32(1) + le32(2) // seq=1, count=2
        payload += le32(-5) + le32(4) + le32(15) + le32(10)   // clamped to x:0,y:4,w:10,h:10
        payload += le32(25) + le32(20) + le32(20) + le32(15) // clamped to x:25,y:20,w:7,h:4 for 32x24
        let parsed = GuestDisplaySession.parseDamageRects(payload, width: 32, height: 24)
        XCTAssertEqual(parsed, [DamageRect(x: 0, y: 4, w: 10, h: 10), DamageRect(x: 25, y: 20, w: 7, h: 4)])
        XCTAssertNil(GuestDisplaySession.parseDamageRects(le32(1) + le32(0), width: 32, height: 24))
        XCTAssertNil(GuestDisplaySession.parseDamageRects([1, 2, 3], width: 32, height: 24))

        // 2. End-to-end surface handoff, independent generations, partial copy, and non-destructive snapshotImage.
        let session = GuestDisplaySession(vmID: UUID(), family: .powerMacG4)
        try session.start()
        defer { session.stop() }

        let clientFD = Darwin.socket(AF_UNIX, SOCK_STREAM, 0)
        XCTAssertGreaterThanOrEqual(clientFD, 0)
        defer { if clientFD >= 0 { Darwin.close(clientFD) } }

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let maxLen = MemoryLayout.size(ofValue: addr.sun_path) - 1
        _ = withUnsafeMutablePointer(to: &addr.sun_path) {
            $0.withMemoryRebound(to: CChar.self, capacity: maxLen + 1) { dst in
                session.socketPath.withCString { src in strncpy(dst, src, maxLen) }
            }
        }
        let connRC = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.connect(clientFD, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        XCTAssertEqual(connRC, 0)

        let shmDir = FileManager.default.temporaryDirectory
        let shmPath = shmDir.appendingPathComponent("cm-surf-\(UUID().uuidString).shm").path
        let w = 32, h = 24, strideBytes = w * 4
        let size = max(16_384, strideBytes * h)
        let shmFD = Darwin.open(shmPath, O_RDWR | O_CREAT | O_EXCL, 0o600)
        XCTAssertGreaterThanOrEqual(shmFD, 0)
        Darwin.unlink(shmPath)
        XCTAssertEqual(Darwin.ftruncate(shmFD, off_t(size)), 0)
        defer { Darwin.close(shmFD) }

        let ptr = Darwin.mmap(nil, size, PROT_READ | PROT_WRITE, MAP_SHARED, shmFD, 0)
        XCTAssertNotEqual(ptr, MAP_FAILED)
        defer { Darwin.munmap(ptr, size) }
        let pixels = ptr!.assumingMemoryBound(to: UInt32.self)
        for i in 0..<(w * h) { pixels[i] = 0x00112233 }

        // Send SURFACE message with shmFD via SCM_RIGHTS.
        var surfPayload = le32(Int32(w)) + le32(Int32(h)) + le32(Int32(strideBytes)) + le32(0x34325258)
        surfPayload += le32(Int32(size)) + le32(0)
        var surfMsg = le32(2) + le32(Int32(surfPayload.count)) + surfPayload
        surfMsg.withUnsafeMutableBytes { buf in
            var iov = iovec(iov_base: buf.baseAddress, iov_len: buf.count)
            let cmsgSpace = 64
            let cbuf = UnsafeMutableRawPointer.allocate(byteCount: cmsgSpace, alignment: MemoryLayout<cmsghdr>.alignment)
            defer { cbuf.deallocate() }
            memset(cbuf, 0, cmsgSpace)
            var msg = msghdr()
            msg.msg_iov = withUnsafeMutablePointer(to: &iov) { $0 }
            msg.msg_iovlen = 1
            msg.msg_control = cbuf
            msg.msg_controllen = socklen_t(MemoryLayout<cmsghdr>.size + MemoryLayout<Int32>.size)
            let chdr = cbuf.assumingMemoryBound(to: cmsghdr.self)
            chdr.pointee.cmsg_len = socklen_t(MemoryLayout<cmsghdr>.size + MemoryLayout<Int32>.size)
            chdr.pointee.cmsg_level = SOL_SOCKET
            chdr.pointee.cmsg_type = SCM_RIGHTS
            cbuf.advanced(by: MemoryLayout<cmsghdr>.size).assumingMemoryBound(to: Int32.self).pointee = shmFD
            XCTAssertGreaterThan(Darwin.sendmsg(clientFD, &msg, 0), 0)
        }

        for _ in 0..<50 where session.currentFrameGeneration == 0 {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        XCTAssertGreaterThan(session.currentFrameGeneration, 0)

        var genA: UInt64 = 0
        var genB: UInt64 = 0
        let rawA = try XCTUnwrap(session.presentFrame(lastGeneration: &genA))
        let rawB = try XCTUnwrap(session.presentFrame(lastGeneration: &genB))
        XCTAssertEqual(CFGetTypeID(rawA), IOSurfaceGetTypeID())
        XCTAssertEqual(CFGetTypeID(rawB), IOSurfaceGetTypeID())
        XCTAssertTrue(rawA === rawB, "Second viewer for same generation should reuse front IOSurface")
        XCTAssertNil(session.presentFrame(lastGeneration: &genA))

        // snapshotImage() must not affect viewer presentation generation.
        XCTAssertNotNil(session.snapshotImage())
        XCTAssertNil(session.presentFrame(lastGeneration: &genB))

        // Paint a partial damage rectangle and send DAMAGE; double-buffered IOSurfaces
        // keep both front & back updated and force alpha opaque even when X == 0.
        pixels[5 * w + 7] = 0x00445566
        let dmgPayload = le32(2) + le32(1) + le32(7) + le32(5) + le32(2) + le32(2)
        let dmgMsg = le32(3) + le32(Int32(dmgPayload.count)) + dmgPayload
        let prevGen = session.currentFrameGeneration
        dmgMsg.withUnsafeBytes { buf in
            XCTAssertEqual(Darwin.send(clientFD, buf.baseAddress, buf.count, 0), buf.count)
        }
        for _ in 0..<50 where session.currentFrameGeneration == prevGen {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        let rawNextA = try XCTUnwrap(session.presentFrame(lastGeneration: &genA))
        XCTAssertEqual(CFGetTypeID(rawNextA), IOSurfaceGetTypeID())
        let nextA = unsafeDowncast(rawNextA, to: IOSurfaceRef.self)
        IOSurfaceLock(nextA, .readOnly, nil)
        let rowStrideA = IOSurfaceGetBytesPerRow(nextA) / 4
        let addrA = IOSurfaceGetBaseAddress(nextA).assumingMemoryBound(to: UInt32.self)
        XCTAssertEqual(addrA[5 * rowStrideA + 7], 0xFF445566)
        XCTAssertEqual(addrA[0], 0xFF112233)
        IOSurfaceUnlock(nextA, .readOnly, nil)

        // Verify AppKit's cacheDisplay composites the IOSurface opaquely over a contrasting backdrop.
        _ = NSApplication.shared
        let hostWin = NSWindow(contentRect: NSRect(x: 0, y: 0, width: w, height: h),
                               styleMask: [.borderless], backing: .buffered, defer: false)
        hostWin.isReleasedWhenClosed = false
        let hostView = NSView(frame: NSRect(x: 0, y: 0, width: w, height: h))
        hostView.wantsLayer = true
        hostView.layer?.backgroundColor = NSColor.red.cgColor
        let sub = CALayer()
        sub.frame = hostView.bounds
        sub.isOpaque = true
        sub.contents = nextA
        hostView.layer?.addSublayer(sub)
        hostWin.contentView = hostView
        let rep = try XCTUnwrap(hostView.bitmapImageRepForCachingDisplay(in: hostView.bounds))
        hostView.cacheDisplay(in: hostView.bounds, to: rep)
        let c = try XCTUnwrap(rep.colorAt(x: 1, y: 1))
        XCTAssertLessThan(c.redComponent, 0.3, "Expected opaque IOSurface pixel rather than underlying red backdrop")
        hostWin.contentView = nil
        hostWin.close()
    }

    func testGuestScreenSuspendedPausesDisplayActive() {
        _ = NSApplication.shared
        let session = GuestDisplaySession(vmID: UUID(), family: .powerMacG4)
        let view = GuestScreenNSView(session: session)
        XCTAssertFalse(view.isDisplayActive)
        let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 200, height: 150),
                           styleMask: [.titled], backing: .buffered, defer: false)
        win.isReleasedWhenClosed = false
        win.contentView = view
        let hostVisible = win.occlusionState.contains(.visible)
        XCTAssertEqual(view.isDisplayActive, hostVisible)
        view.isSuspended = true
        XCTAssertFalse(view.isDisplayActive)
        win.contentView = nil
        win.close()
    }

    // MARK: - GuestScreenNSView pointer bounds, scale modes, cursor & pause

    func testGuestScreenIgnoresPointsOutsideImageRect() {
        let session = GuestDisplaySession(vmID: UUID(), family: .quadra800)
        let view = GuestScreenNSView(session: session)
        view.setTestGeometry(surfaceSize: CGSize(width: 640, height: 480),
                             imageRect: CGRect(x: 100, y: 50, width: 640, height: 480),
                             scale: 1)

        XCTAssertNil(view.guestPoint(forViewPoint: CGPoint(x: 99, y: 100)))
        XCTAssertNil(view.guestPoint(forViewPoint: CGPoint(x: 740, y: 100)))
        XCTAssertNil(view.guestPoint(forViewPoint: CGPoint(x: 200, y: 49)))
        XCTAssertNil(view.guestPoint(forViewPoint: CGPoint(x: 200, y: 530)))

        let topLeft = view.guestPoint(forViewPoint: CGPoint(x: 100, y: 50))
        XCTAssertEqual(topLeft?.0, 0)
        XCTAssertEqual(topLeft?.1, 0)

        let bottomRight = view.guestPoint(forViewPoint: CGPoint(x: 739.5, y: 529.5))
        XCTAssertEqual(bottomRight?.0, 639)
        XCTAssertEqual(bottomRight?.1, 479)
    }

    func testGuestScreenScaleModesAndScrollbarActivation() {
        XCTAssertEqual(GuestScreenNSView.scaleFactor(mode: .sharp, surface: CGSize(width: 1024, height: 768),
                                                     viewport: CGSize(width: 600, height: 400)), 1)
        XCTAssertEqual(GuestScreenNSView.scaleFactor(mode: .oneX, surface: CGSize(width: 1024, height: 768),
                                                     viewport: CGSize(width: 600, height: 400)), 1)
        XCTAssertEqual(GuestScreenNSView.scaleFactor(mode: .twoX, surface: CGSize(width: 1024, height: 768),
                                                     viewport: CGSize(width: 600, height: 400)), 2)
        XCTAssertEqual(GuestScreenNSView.scaleFactor(mode: .fit, surface: CGSize(width: 1024, height: 768),
                                                     viewport: CGSize(width: 600, height: 400)),
                       400.0 / 768.0, accuracy: 0.0001)
        XCTAssertEqual(GuestScreenNSView.scaleFactor(mode: .sharp, surface: CGSize(width: 640, height: 480),
                                                     viewport: CGSize(width: 1400, height: 1000)), 2)

        let session = GuestDisplaySession(vmID: UUID(), family: .powerMacG4)
        let container = GuestScreenContainerView(session: session)
        container.frame = NSRect(x: 0, y: 0, width: 600, height: 400)
        container.screenView.setTestGeometry(surfaceSize: CGSize(width: 1024, height: 768),
                                             imageRect: CGRect(x: 0, y: 0, width: 1024, height: 768),
                                             scale: 1)

        container.screenView.scaleMode = .sharp
        container.updateScrollbarsAndDocumentFrame()
        XCTAssertTrue(container.hasHorizontalScroller)
        XCTAssertTrue(container.hasVerticalScroller)

        container.screenView.scaleMode = .twoX
        container.updateScrollbarsAndDocumentFrame()
        XCTAssertTrue(container.hasHorizontalScroller)
        XCTAssertTrue(container.hasVerticalScroller)

        container.screenView.scaleMode = .fit
        container.updateScrollbarsAndDocumentFrame()
        XCTAssertFalse(container.hasHorizontalScroller)
        XCTAssertFalse(container.hasVerticalScroller)
    }

    func testGuestScreenCursorScalesBelowOneXAndPauseClearsMouseState() {
        let session = GuestDisplaySession(vmID: UUID(), family: .quadra800)
        let view = GuestScreenNSView(session: session)
        view.setTestGeometry(surfaceSize: CGSize(width: 1024, height: 768),
                             imageRect: CGRect(x: 0, y: 0, width: 512, height: 384),
                             scale: 0.5)
        let color = CGColorSpace(name: CGColorSpace.sRGB)!
        let ctx = CGContext(data: nil, width: 16, height: 16, bitsPerComponent: 8, bytesPerRow: 64,
                            space: color, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        let image = ctx.makeImage()!

        view.updateGuestCursor(image, hotSpot: CGPoint(x: 4, y: 6))
        XCTAssertEqual(view.currentGuestCursor?.image.size, NSSize(width: 8, height: 8))
        XCTAssertEqual(view.currentGuestCursor?.hotSpot, NSPoint(x: 2, y: 3))

        // mouseUp without prior mouseDown does nothing
        view.mouseUp(with: NSEvent())
        XCTAssertFalse(view.hasActiveMouseButtons)

        // Pause clears held button0 and rightButtonAsControl
        view.setTestButtonState(button0: true, rightAsControl: true)
        XCTAssertTrue(view.hasActiveMouseButtons)
        view.isPaused = true
        XCTAssertFalse(view.hasActiveMouseButtons)
    }

    // MARK: - PlatinumWell preserveFillWhenDisabled & Resource Fallbacks

    func testPlatinumWellPreservesFillWhenDisabled() throws {
        let view = PlatinumWell(fill: Platinum.desktop, preserveFillWhenDisabled: true) {
            Color.clear.frame(width: 40, height: 20)
        }
        .disabled(true)
        .frame(width: 48, height: 28)
        .environment(\.platinumStaticRender, true)
        let renderer = ImageRenderer(content: view)
        renderer.scale = 1
        let cg = try XCTUnwrap(renderer.cgImage)
        var pixel = [UInt8](repeating: 0, count: 4)
        let ctx = try XCTUnwrap(CGContext(data: &pixel, width: 1, height: 1, bitsPerComponent: 8, bytesPerRow: 4,
                                          space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue))
        ctx.draw(cg, in: CGRect(x: -24, y: -14, width: CGFloat(cg.width), height: CGFloat(cg.height)))
        XCTAssertEqual(pixel[0], 0x63)
        XCTAssertEqual(pixel[1], 0x63)
        XCTAssertEqual(pixel[2], 0x9C)
    }

    func testNoticesAndLicensesLookupDoesNotCrash() {
        _ = AboutView.thirdPartyNoticesURL()
        _ = IconPacksDialog.licensesDirectoryURL()
    }
}
