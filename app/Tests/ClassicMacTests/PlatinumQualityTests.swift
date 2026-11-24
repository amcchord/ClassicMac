import XCTest
import AppKit
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
        XCTAssertTrue((8...16).contains(session.refreshInterval))
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
