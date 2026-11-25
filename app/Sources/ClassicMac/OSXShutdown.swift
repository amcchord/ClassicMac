import CoreGraphics
import Foundation

/// Verified pixel-diff detection for Mac OS X Apple-menu shutdown and restart.
/// Thresholds and geometry match RetroBoot's `power_osx.py` and `powerdlg.py`:
/// unknown pixels or unexpected dialogs never authorize pressing Return.
enum OSXShutdownDetector {
    struct RGBFrame: Equatable, Sendable {
        let width: Int
        let height: Int
        let rgb: Data

        var isValid: Bool {
            width > 0 && height > 0 && rgb.count == width * height * 3
        }

        static func from(cgImage: CGImage) -> RGBFrame? {
            let width = cgImage.width
            let height = cgImage.height
            guard width > 0, height > 0 else { return nil }
            let bytesPerRow = width * 4
            var rgba = Data(count: bytesPerRow * height)
            let space = CGColorSpace(name: CGColorSpace.sRGB) ?? CGColorSpaceCreateDeviceRGB()
            let bitmapInfo = CGImageAlphaInfo.noneSkipLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue
            let rendered = rgba.withUnsafeMutableBytes { raw -> Bool in
                guard let base = raw.baseAddress,
                      let context = CGContext(
                          data: base,
                          width: width,
                          height: height,
                          bitsPerComponent: 8,
                          bytesPerRow: bytesPerRow,
                          space: space,
                          bitmapInfo: bitmapInfo
                      ) else {
                    return false
                }
                context.interpolationQuality = .none
                context.draw(cgImage, in: CGRect(x: 0, y: 0, width: width, height: height))
                return true
            }
            guard rendered else { return nil }
            var rgb = Data(count: width * height * 3)
            rgba.withUnsafeBytes { srcRaw in
                rgb.withUnsafeMutableBytes { dstRaw in
                    let src = srcRaw.bindMemory(to: UInt8.self)
                    let dst = dstRaw.bindMemory(to: UInt8.self)
                    let pixels = width * height
                    for i in 0..<pixels {
                        let s = i * 4
                        let d = i * 3
                        dst[d] = src[s]
                        dst[d + 1] = src[s + 1]
                        dst[d + 2] = src[s + 2]
                    }
                }
            }
            return RGBFrame(width: width, height: height, rgb: rgb)
        }

        static func from(ppmURL: URL) -> RGBFrame? {
            guard let data = try? Data(contentsOf: ppmURL) else { return nil }
            return from(ppmData: data)
        }

        static func from(ppmData data: Data) -> RGBFrame? {
            var index = 0

            func isWhitespace(_ byte: UInt8) -> Bool {
                byte == 0x20 || byte == 0x09 || byte == 0x0A || byte == 0x0D
            }

            func skipWhitespaceAndComments() {
                while index < data.count {
                    let byte = data[index]
                    if isWhitespace(byte) {
                        index += 1
                    } else if byte == 0x23 { // '#'
                        while index < data.count && data[index] != 0x0A && data[index] != 0x0D {
                            index += 1
                        }
                    } else {
                        break
                    }
                }
            }

            func readToken() -> String? {
                skipWhitespaceAndComments()
                var bytes: [UInt8] = []
                while index < data.count {
                    let byte = data[index]
                    if isWhitespace(byte) || byte == 0x23 {
                        break
                    }
                    bytes.append(byte)
                    index += 1
                }
                return bytes.isEmpty ? nil : String(bytes: bytes, encoding: .ascii)
            }

            guard readToken() == "P6",
                  let widthToken = readToken(), let width = Int(widthToken),
                  let heightToken = readToken(), let height = Int(heightToken),
                  let maxToken = readToken(), let maxValue = Int(maxToken),
                  maxValue == 255,
                  width > 0, height > 0, width <= 8192, height <= 8192,
                  index < data.count, isWhitespace(data[index]) else {
                return nil
            }
            index += 1
            let pixelBytes = width * height * 3
            guard index + pixelBytes <= data.count else { return nil }
            let rgb = Data(data[index..<(index + pixelBytes)])
            return RGBFrame(width: width, height: height, rgb: rgb)
        }
    }

    struct Box: Equatable {
        let x0: Int
        let y0: Int
        let x1: Int
        let y1: Int

        var width: Int { x1 - x0 }
        var height: Int { y1 - y0 }
        var area: Int { width * height }
    }

    private static let menuBarHeight = 22
    private static let boxWidthRange = 240...760
    private static let boxHeightRange = 64...320

    static func boxDiffStats(
        width: Int,
        height: Int,
        before: Data,
        current: Data,
        box: Box,
        threshold: Int = 24
    ) -> (changed: Int, total: Int) {
        let x0 = max(0, box.x0)
        let y0 = max(0, box.y0)
        let x1 = min(width, box.x1)
        let y1 = min(height, box.y1)
        let expectedBytes = width * height * 3
        guard x1 > x0, y1 > y0,
              before.count == expectedBytes,
              current.count == expectedBytes else {
            return (0, 0)
        }
        var changed = 0
        let total = (x1 - x0) * (y1 - y0)
        before.withUnsafeBytes { bRaw in
            current.withUnsafeBytes { cRaw in
                let b = bRaw.bindMemory(to: UInt8.self)
                let c = cRaw.bindMemory(to: UInt8.self)
                for y in y0..<y1 {
                    let row = y * width * 3
                    for x in x0..<x1 {
                        let off = row + x * 3
                        let diff = abs(Int(b[off]) - Int(c[off]))
                            + abs(Int(b[off + 1]) - Int(c[off + 1]))
                            + abs(Int(b[off + 2]) - Int(c[off + 2]))
                        if diff > threshold {
                            changed += 1
                        }
                    }
                }
            }
        }
        return (changed, total)
    }

    /// Verify Ctrl+F2 + Down visibly opened the Apple menu dropdown cleanly.
    static func detectMenuOpened(width: Int, height: Int, before: Data, current: Data) -> Bool {
        let expectedBytes = width * height * 3
        guard width >= 640, height >= 480,
              before.count == expectedBytes,
              current.count == expectedBytes,
              before != current else {
            return false
        }
        let menu = boxDiffStats(
            width: width, height: height, before: before, current: current,
            box: Box(x0: 0, y0: 22, x1: min(260, width), y1: min(340, height))
        )
        let rest = boxDiffStats(
            width: width, height: height, before: before, current: current,
            box: Box(x0: min(300, width), y0: 22, x1: width, y1: height)
        )
        guard menu.total > 0, menu.changed >= 300 else { return false }
        let menuRatio = Double(menu.changed) / Double(menu.total)
        let restRatio = rest.total == 0 ? 0.0 : Double(rest.changed) / Double(rest.total)
        return menuRatio >= 0.03 && restRatio < 0.20
    }

    /// Verify End + Up moved the selection highlight inside the open Apple menu.
    static func detectMenuSelectionMoved(width: Int, height: Int, prevMenu: Data, current: Data) -> Bool {
        let expectedBytes = width * height * 3
        guard width >= 640, height >= 480,
              prevMenu.count == expectedBytes,
              current.count == expectedBytes,
              prevMenu != current else {
            return false
        }
        let selection = boxDiffStats(
            width: width, height: height, before: prevMenu, current: current,
            box: Box(x0: 4, y0: 180, x1: min(250, width), y1: min(340, height))
        )
        let rest = boxDiffStats(
            width: width, height: height, before: prevMenu, current: current,
            box: Box(x0: min(300, width), y0: 22, x1: width, y1: height)
        )
        let restRatio = rest.total == 0 ? 0.0 : Double(rest.changed) / Double(rest.total)
        return selection.total > 0 && selection.changed >= 80 && restRatio < 0.15
    }

    /// Verify a centered OS X Shut Down / Restart confirmation modal appeared.
    static func detectConfirmationDialog(width: Int, height: Int, before: Data, current: Data) -> Bool {
        let expectedBytes = width * height * 3
        guard width >= 640, height >= 480,
              before.count == expectedBytes,
              current.count == expectedBytes,
              before != current else {
            return false
        }
        let beforeFrame = RGBFrame(width: width, height: height, rgb: before)
        let currentFrame = RGBFrame(width: width, height: height, rgb: current)
        if let modalBox = modalChangeBox(before: beforeFrame, after: currentFrame) {
            let centerX = Double(modalBox.x0 + modalBox.x1) / 2.0
            if modalBox.width >= 160,
               modalBox.height >= 60,
               centerX >= Double(width) * 0.2,
               centerX <= Double(width) * 0.8 {
                return true
            }
        }
        // Also accept a centered dialog change even if desktop menu bar closed at the same frame.
        let center = boxDiffStats(
            width: width, height: height, before: before, current: current,
            box: Box(x0: width / 2 - 220, y0: 40, x1: width / 2 + 220, y1: height - 80)
        )
        let edge = boxDiffStats(
            width: width, height: height, before: before, current: current,
            box: Box(x0: width - 140, y0: 22, x1: width, y1: height)
        )
        let edgeRatio = edge.total == 0 ? 0.0 : Double(edge.changed) / Double(edge.total)
        return center.total > 0 && center.changed >= 2000 && edgeRatio < 0.25
    }

    // MARK: Modal change detection (ported from RetroBoot powerdlg.py)

    static func modalChangeBox(before: RGBFrame, after: RGBFrame) -> Box? {
        guard before.isValid, after.isValid,
              before.width == after.width, before.height == after.height,
              menuTitlesChanged(before: before, after: after) else {
            return nil
        }
        let boxes = changedBoxes(before: before, after: after)
        return boxes.max { $0.area < $1.area }
    }

    private static func menuTitlesChanged(before: RGBFrame, after: RGBFrame) -> Bool {
        let w = before.width
        let h = before.height
        guard h >= menuBarHeight, w >= 200 else { return false }
        if let local = evaluateMenuState(before: before, after: after, wholeRow: false) {
            if local { return true }
            if let whole = evaluateMenuState(before: before, after: after, wholeRow: true), whole {
                return true
            }
            return false
        }
        return false
    }

    private static func evaluateMenuState(before: RGBFrame, after: RGBFrame, wholeRow: Bool) -> Bool? {
        let w = before.width
        let x0 = 30
        let x1 = w * 45 / 100
        guard x1 > x0 else { return nil }
        var inkB = 0
        var recoloured = 0
        var colsB: Set<Int> = []
        var colsA: Set<Int> = []

        return before.rgb.withUnsafeBytes { bRaw in
            after.rgb.withUnsafeBytes { aRaw in
                let b = bRaw.bindMemory(to: UInt8.self)
                let a = aRaw.bindMemory(to: UInt8.self)

                for y in 2..<17 {
                    let bgB: UInt32
                    let bgA: UInt32
                    if wholeRow {
                        bgB = mostCommonColor(b, row: y, width: w, xStart: 0, xEnd: w, step: 2)
                        bgA = mostCommonColor(a, row: y, width: w, xStart: 0, xEnd: w, step: 2)
                    } else {
                        bgB = mostCommonColor(b, row: y, width: w, xStart: x0, xEnd: x1, step: 1)
                        bgA = mostCommonColor(a, row: y, width: w, xStart: x0, xEnd: x1, step: 1)
                    }
                    let rowBase = y * w * 3
                    for i in 0..<(x1 - x0) {
                        let off = rowBase + (x0 + i) * 3
                        let pb = packRGB(b[off], b[off + 1], b[off + 2])
                        let pa = packRGB(a[off], a[off + 1], a[off + 2])
                        if farRGB(pb, bgB) {
                            inkB += 1
                            colsB.insert(i / 4)
                            if farRGB(pb, pa) {
                                recoloured += 1
                            }
                        }
                        if farRGB(pa, bgA) {
                            colsA.insert(i / 4)
                        }
                    }
                }
                guard inkB >= 40 else { return nil }
                let unionCount = max(1, colsB.union(colsA).count)
                let iou = Double(colsB.intersection(colsA).count) / Double(unionCount)
                let share = Double(recoloured) / Double(inkB)
                return iou >= 0.7 && share >= 0.4
            }
        }
    }

    private static func changedBoxes(before: RGBFrame, after: RGBFrame, cw: Int = 8, step: Int = 2) -> [Box] {
        let w = before.width
        let h = before.height
        guard w >= boxWidthRange.lowerBound else { return [] }
        let gw = (w + cw - 1) / cw
        struct Cell: Hashable { let gx: Int; let gy: Int }
        var cells: Set<Cell> = []

        before.rgb.withUnsafeBytes { bRaw in
            after.rgb.withUnsafeBytes { aRaw in
                let b = bRaw.bindMemory(to: UInt8.self)
                let a = aRaw.bindMemory(to: UInt8.self)
                for y in stride(from: menuBarHeight, to: h, by: step) {
                    let rowStart = y * w * 3
                    let gy = y / step
                    for gx in 0..<gw {
                        let pxStart = gx * cw
                        let pxEnd = min(pxStart + cw, w)
                        var differed = false
                        for x in pxStart..<pxEnd {
                            let off = rowStart + x * 3
                            if b[off] != a[off] || b[off + 1] != a[off + 1] || b[off + 2] != a[off + 2] {
                                differed = true
                                break
                            }
                        }
                        if differed {
                            cells.insert(Cell(gx: gx, gy: gy))
                        }
                    }
                }
            }
        }

        let maxCells = gw * (h / step) / 3
        guard cells.count <= maxCells else { return [] }

        var out: [Box] = []
        while let first = cells.first {
            cells.remove(first)
            var stack = [first]
            var minX = first.gx, maxX = first.gx
            var minY = first.gy, maxY = first.gy
            while let cur = stack.popLast() {
                if cur.gx < minX { minX = cur.gx }
                if cur.gx > maxX { maxX = cur.gx }
                if cur.gy < minY { minY = cur.gy }
                if cur.gy > maxY { maxY = cur.gy }
                for dx in -1...1 {
                    for dy in -1...1 {
                        let nb = Cell(gx: cur.gx + dx, gy: cur.gy + dy)
                        if cells.remove(nb) != nil {
                            stack.append(nb)
                        }
                    }
                }
            }
            let box = Box(
                x0: minX * cw,
                y0: minY * step,
                x1: min(w, (maxX + 1) * cw),
                y1: min(h, (maxY + 1) * step)
            )
            if (boxWidthRange.lowerBound...(boxWidthRange.upperBound + 16)).contains(box.width),
               (boxHeightRange.lowerBound...(boxHeightRange.upperBound + 8)).contains(box.height) {
                out.append(box)
            }
        }
        return out
    }

    @inline(__always)
    private static func packRGB(_ r: UInt8, _ g: UInt8, _ b: UInt8) -> UInt32 {
        (UInt32(r) << 16) | (UInt32(g) << 8) | UInt32(b)
    }

    @inline(__always)
    private static func farRGB(_ lhs: UInt32, _ rhs: UInt32) -> Bool {
        let lr = Int((lhs >> 16) & 0xFF), lg = Int((lhs >> 8) & 0xFF), lb = Int(lhs & 0xFF)
        let rr = Int((rhs >> 16) & 0xFF), rg = Int((rhs >> 8) & 0xFF), rb = Int(rhs & 0xFF)
        return abs(lr - rr) + abs(lg - rg) + abs(lb - rb) > 60
    }

    private static func mostCommonColor(
        _ ptr: UnsafeBufferPointer<UInt8>,
        row: Int,
        width: Int,
        xStart: Int,
        xEnd: Int,
        step: Int
    ) -> UInt32 {
        var counts: [UInt32: Int] = [:]
        var bestColor: UInt32 = 0
        var bestCount = 0
        let rowBase = row * width * 3
        for x in stride(from: xStart, to: xEnd, by: step) {
            let off = rowBase + x * 3
            let color = packRGB(ptr[off], ptr[off + 1], ptr[off + 2])
            let count = (counts[color] ?? 0) + 1
            counts[color] = count
            if count > bestCount {
                bestCount = count
                bestColor = color
            }
        }
        return bestColor
    }
}

/// Async coordinator that drives QEMU over HMP to perform a visually verified
/// Mac OS X Apple-menu Shut Down or Restart.
enum OSXShutdownController {
    enum Action: Sendable {
        case shutDown
        case restart
    }

    enum Outcome: Equatable, Sendable {
        case confirmed
        case unverified(String)
        case monitorFailed
    }

    struct Transport: Sendable {
        var sendKey: @Sendable (String) -> Bool
        var unpause: @Sendable () -> Bool
        var captureFrame: @Sendable () async -> OSXShutdownDetector.RGBFrame?
        var sleepMillis: @Sendable (UInt64) async -> Void

        static func qemu(
            vmID: UUID,
            socketPath: String,
            sessionProvider: @escaping @Sendable () async -> GuestDisplaySession?
        ) -> Transport {
            Transport(
                sendKey: { key in
                    HMPClient.send("sendkey \(key)", socketPath: socketPath)
                },
                unpause: {
                    HMPClient.send("cont", socketPath: socketPath)
                },
                captureFrame: {
                    if let session = await sessionProvider(),
                       let cgImage = session.takeFrame(force: true) {
                        return OSXShutdownDetector.RGBFrame.from(cgImage: cgImage)
                    }
                    let dumpURL = AppPaths.runtimeSocketDir
                        .appendingPathComponent("\(vmID.uuidString).osx-shutdown.ppm")
                    return await Task.detached(priority: .userInitiated) {
                        defer { try? FileManager.default.removeItem(at: dumpURL) }
                        guard HMPClient.command("screendump \(dumpURL.path)", socketPath: socketPath) != nil else {
                            return nil
                        }
                        return OSXShutdownDetector.RGBFrame.from(ppmURL: dumpURL)
                    }.value
                },
                sleepMillis: { ms in
                    try? await Task.sleep(nanoseconds: ms * 1_000_000)
                }
            )
        }
    }

    static func perform(
        action: Action,
        wasPaused: Bool,
        transport: Transport
    ) async -> Outcome {
        if wasPaused {
            _ = transport.unpause()
            await transport.sleepMillis(100)
        }

        guard let before = await transport.captureFrame(), before.isValid else {
            return .unverified("Could not capture the Mac OS X screen before shutting down.")
        }

        guard transport.sendKey("ctrl-f2 100"),
              transport.sendKey("down 100") else {
            return .monitorFailed
        }
        await transport.sleepMillis(250)

        guard let menuFrame = await transport.captureFrame(),
              OSXShutdownDetector.detectMenuOpened(
                width: before.width,
                height: before.height,
                before: before.rgb,
                current: menuFrame.rgb
              ) else {
            return .unverified("The Apple menu did not open cleanly.")
        }

        guard transport.sendKey("end 100") else {
            return .monitorFailed
        }
        await transport.sleepMillis(150)

        guard transport.sendKey("up 100") else {
            return .monitorFailed
        }
        if action == .restart {
            await transport.sleepMillis(150)
            guard transport.sendKey("up 100") else {
                return .monitorFailed
            }
        }
        await transport.sleepMillis(250)

        guard let selectionFrame = await transport.captureFrame(),
              OSXShutdownDetector.detectMenuSelectionMoved(
                width: menuFrame.width,
                height: menuFrame.height,
                prevMenu: menuFrame.rgb,
                current: selectionFrame.rgb
              ) else {
            return .unverified("Could not select Shut Down in the Apple menu.")
        }

        guard transport.sendKey("ret 100") else {
            return .monitorFailed
        }
        await transport.sleepMillis(350)

        guard let confirmationFrame = await transport.captureFrame(),
              OSXShutdownDetector.detectConfirmationDialog(
                width: before.width,
                height: before.height,
                before: before.rgb,
                current: confirmationFrame.rgb
              ) else {
            return .unverified("The Mac OS X confirmation dialog did not appear.")
        }

        guard transport.sendKey("ret 100") else {
            return .monitorFailed
        }
        return .confirmed
    }
}
