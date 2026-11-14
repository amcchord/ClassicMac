import Foundation

/// Decides when the startup curtain can fade into the guest's own boot screen.
enum GuestBootFrame {
    static func isReady(_ base: UnsafeRawPointer, width: Int, height: Int,
                        stride: Int, family: MachineFamily) -> Bool {
        guard width >= 2, height >= 2, stride >= width * 4 else { return false }
        var macLike = 0, samples = 0
        for gy in 1...9 {
            for gx in 1...12 {
                let x = width * gx / 13, y = height * gy / 10
                let px = base.advanced(by: y * stride + x * 4)
                let b = Int(px.load(fromByteOffset: 0, as: UInt8.self))
                let g = Int(px.load(fromByteOffset: 1, as: UInt8.self))
                let r = Int(px.load(fromByteOffset: 2, as: UInt8.self))
                samples += 1
                let black = r < 24 && g < 24 && b < 24
                let white = r > 240 && g > 240 && b > 240
                let firmwareYellow = r > 230 && g > 230 && b > 150 && b < 235
                if family == .quadra800 ? !black : !(black || white || firmwareYellow) { macLike += 1 }
            }
        }
        guard macLike * 10 >= samples * 3 else { return false }

        // A white/gray clear is not a boot screen. Average adjacent pixels so
        // the ROM's empty checkerboard also stays covered. A denser grid sees
        // the small centered Happy Mac that the coarse background test misses.
        var histogram = [Int](repeating: 0, count: 256)
        for y in Swift.stride(from: 0, to: height - 1, by: max(2, height / 120)) {
            for x in Swift.stride(from: 0, to: width - 1, by: max(2, width / 160)) {
                var brightness = 0
                for dy in 0...1 {
                    for dx in 0...1 {
                        let px = base.advanced(by: (y + dy) * stride + (x + dx) * 4)
                        for channel in 0...2 { brightness += Int(px.load(fromByteOffset: channel, as: UInt8.self)) }
                    }
                }
                histogram[brightness / 12] += 1
            }
        }
        // Ignore a few isolated pixels, including a small firmware pointer.
        var low = 0, high = 255, count = 0
        for value in 0...255 {
            count += histogram[value]
            if count >= 8 { low = value; break }
        }
        count = 0
        for value in (0...255).reversed() {
            count += histogram[value]
            if count >= 8 { high = value; break }
        }
        return high - low >= 40
    }
}
