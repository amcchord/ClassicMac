/// Preserve subpixel trackpad motion between events while a game owns the mouse.
struct RelativePointerMotion {
    private var x = 0.0
    private var y = 0.0

    mutating func take(
        dx: Double,
        dy: Double,
        scale: Double,
        legacyNative: Bool = false,
        dpi: Int? = nil
    ) -> (x: Int, y: Int) {
        if legacyNative {
            let clampedDPI = dpi.map { min(max($0, 100), 100_000) } ?? 1000
            let ratio = Double(clampedDPI) / 200.0
            x += dx / ratio
            y += dy / ratio
            let outX = max(-32_767, min(32_767, Int(x)))
            let outY = max(-32_767, min(32_767, Int(y)))
            x -= Double(outX)
            y -= Double(outY)
            return (x: outX, y: outY)
        }
        x += dx / max(scale, 1)
        y += dy / max(scale, 1)
        let result = (x: Int(x), y: Int(y))
        x -= Double(result.x)
        y -= Double(result.y)
        return result
    }
}
