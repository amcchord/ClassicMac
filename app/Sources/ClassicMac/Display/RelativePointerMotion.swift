/// Preserve subpixel trackpad motion between events while a game owns the mouse.
struct RelativePointerMotion {
    private var x = 0.0
    private var y = 0.0

    mutating func take(dx: Double, dy: Double, scale: Double) -> (x: Int, y: Int) {
        x += dx / max(scale, 1)
        y += dy / max(scale, 1)
        let result = (x: Int(x), y: Int(y))
        x -= Double(result.x)
        y -= Double(result.y)
        return result
    }
}
