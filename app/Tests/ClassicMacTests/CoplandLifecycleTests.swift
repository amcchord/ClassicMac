import XCTest
import AppKit
@testable import ClassicMac

// Opt-in real-engine qualification; ordinary test runs need no Apple assets.
@MainActor final class CoplandLifecycleTests: XCTestCase {
    func testNativeBootPauseRestartAndStop() async throws {
        guard let path = ProcessInfo.processInfo.environment["COPLAND_TEST_MACHINE"] else {
            throw XCTSkip("Set COPLAND_TEST_MACHINE to a disposable imported machine")
        }
        let folder = URL(fileURLWithPath: path)
        var config = try JSONDecoder().decode(VMConfig.self, from: Data(contentsOf: folder.appendingPathComponent("config.json")))
        config.bundleURL = folder
        let manager = QEMUManager()
        manager.start(config)
        XCTAssertNil(manager.lastError)
        XCTAssertTrue(manager.isRunning(config.id))
        defer { manager.requestStop(config.id); manager.confirmStop() }
        // Copland's known initial desktop is purple. Use the actual guest VRAM,
        // not merely process liveness or a pre-existing preview from the template.
        func reachedDesktop() -> Bool {
            guard let image = PPMImage.load(QEMUManager.screenDumpURL(for: config.id)), let tiff = image.tiffRepresentation,
                  let bitmap = NSBitmapImageRep(data: tiff),
                  let color = bitmap.colorAt(x: 300, y: 200)?.usingColorSpace(.deviceRGB) else { return false }
            return color.blueComponent > 0.4 && color.redComponent > 0.2 && color.greenComponent < 0.5
        }
        for _ in 0..<60 {
            if reachedDesktop() { break }
            try await Task.sleep(for: .seconds(1))
        }
        XCTAssertTrue(reachedDesktop(), "Copland did not reach its desktop")
        XCTAssertTrue(manager.coplandHaltedIDs.isEmpty)
        manager.pause(config.id)
        XCTAssertTrue(manager.isPaused(config.id))
        try await Task.sleep(for: .seconds(1))
        let before = try config.diskImageURL.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate
        try await Task.sleep(for: .seconds(2))
        XCTAssertEqual(before, try config.diskImageURL.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate)
        manager.resume(config.id)
        XCTAssertFalse(manager.isPaused(config.id))
        manager.reboot(config.id)
        // Restart exits the helper with 75 and relaunches a fresh process.
        var continuedAssertions = 0
        // A hard reset leaves Copland's unfinished filesystem recovery path
        // capable of asserting. Exercise the same explicit Continue operation
        // exposed to users, with a strict bound so a regression still fails.
        try await Task.sleep(for: .seconds(15))
        for _ in 0..<60 {
            if manager.coplandHaltedIDs.contains(config.id), continuedAssertions < 5 {
                manager.continueCopland(config.id)
                continuedAssertions += 1
                try await Task.sleep(for: .seconds(3))
            }
            if reachedDesktop() && manager.coplandHaltedIDs.isEmpty { break }
            try await Task.sleep(for: .seconds(1))
        }
        print("Copland restart required \(continuedAssertions) explicit debugger continuations")
        XCTAssertNil(manager.lastError)
        XCTAssertTrue(manager.isRunning(config.id))
        XCTAssertTrue(reachedDesktop())
        XCTAssertTrue(manager.coplandHaltedIDs.isEmpty)
        manager.requestStop(config.id); manager.confirmStop()
        for _ in 0..<20 {
            if !manager.isRunning(config.id) { break }
            try await Task.sleep(for: .milliseconds(250))
        }
        XCTAssertFalse(manager.isRunning(config.id))
        XCTAssertNil(manager.lastError)
        XCTAssertTrue(FileManager.default.fileExists(atPath: config.previewURL.path))
    }
}
