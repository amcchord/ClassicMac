import SwiftUI
import CoreGraphics
import Foundation
import ImageIO

/// Native Mac OS resource artwork, with original Platinum drawings for actions
/// without a matching system icon. Iconfactory/user overrides take precedence.
public enum PlatinumIcons {
    private static let lock = NSLock()
    nonisolated(unsafe) private static var cache: [String: CGImage] = [:]
    nonisolated(unsafe) private static var extra: [String: [String]] = [:]
    nonisolated(unsafe) private static var overrides: [String: CGImage] = [:]

    /// Replaces icons with images supplied at run time (for example from an
    /// icon collection the user owns), keyed name@size. Pass nil to remove.
    @MainActor
    public static func setOverride(_ image: CGImage?, name: String, size: Int) {
        lock.lock()
        overrides["\(name)@\(size)"] = image
        lock.unlock()
        PlatinumIconTheme.shared.revision += 1
    }

    @MainActor
    public static func removeAllOverrides() {
        lock.lock()
        overrides.removeAll()
        lock.unlock()
        PlatinumIconTheme.shared.revision += 1
    }

    public static func hasOverride(_ name: String, size: Int) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        return overrides["\(name)@\(size)"] != nil
    }

    /// Adds icons (name@size -> rows) drawn with the shared palette.
    public static func register(_ icons: [String: [String]]) {
        lock.lock()
        defer { lock.unlock() }
        for (key, rows) in icons { extra[key] = rows }
    }

    /// The icon image, or nil when no icon has that name and size.
    public static func image(_ name: String, size: Int) -> CGImage? {
        let key = "\(name)@\(size)"
        lock.lock()
        defer { lock.unlock() }
        if let override = overrides[key] { return override }
        if let cached = cache[key] { return cached }
        if let encoded = MacOSIconAssets.png[key], let data = Data(base64Encoded: encoded),
           let source = CGImageSourceCreateWithData(data as CFData, nil),
           let image = CGImageSourceCreateImageAtIndex(source, 0, nil) {
            cache[key] = image
            return image
        }
        guard let rows = extra[key] ?? PlatinumAssets.icons[key],
              let image = render(rows) else { return nil }
        cache[key] = image
        return image
    }

    public static func has(_ name: String, size: Int) -> Bool {
        let key = "\(name)@\(size)"
        lock.lock()
        defer { lock.unlock() }
        return MacOSIconAssets.png[key] != nil || extra[key] != nil || PlatinumAssets.icons[key] != nil
    }

    /// Renders palette rows ("." transparent) to a premultiplied RGBA image.
    public static func render(_ rows: [String]) -> CGImage? {
        let height = rows.count
        let width = rows.map(\.count).max() ?? 0
        guard width > 0, height > 0 else { return nil }
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        for (y, row) in rows.enumerated() {
            for (x, ch) in row.enumerated() {
                guard let argb = PlatinumAssets.palette[ch] else { continue }
                let a = (argb >> 24) & 0xFF
                let r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF
                let i = (y * width + x) * 4
                pixels[i] = UInt8(r * a / 255)
                pixels[i + 1] = UInt8(g * a / 255)
                pixels[i + 2] = UInt8(b * a / 255)
                pixels[i + 3] = UInt8(a)
            }
        }
        guard let provider = CGDataProvider(data: Data(pixels) as CFData) else { return nil }
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                       bytesPerRow: width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }
}

/// Publishes icon changes so icons redraw when an override arrives.
@MainActor
public final class PlatinumIconTheme: ObservableObject {
    public static let shared = PlatinumIconTheme()
    @Published public var revision = 0
}

/// A Platinum icon at one Mac pixel per point (or an integer multiple).
public struct PlatinumIcon: View {
    let name: String
    let size: Int
    let scale: Int
    let dimmed: Bool
    let selected: Bool
    @ObservedObject private var theme = PlatinumIconTheme.shared

    /// - Parameters:
    ///   - selected: draws the icon darkened, as the Finder shows a selected icon.
    public init(_ name: String, size: Int = 32, scale: Int = 1, dimmed: Bool = false, selected: Bool = false) {
        self.name = name
        self.size = size
        self.scale = scale
        self.dimmed = dimmed
        self.selected = selected
    }

    public var body: some View {
        let _ = theme.revision
        Group {
            if let image = PlatinumIcons.image(name, size: size) {
                Image(decorative: image, scale: 1)
                    .resizable()
                    .interpolation(.none)
                    .antialiased(false)
                    .colorMultiply(selected ? Color(white: 0.55) : .white)
                    .opacity(dimmed ? 0.45 : 1)
            } else {
                Color.clear
            }
        }
        .frame(width: CGFloat(size * scale), height: CGFloat(size * scale))
        .accessibilityHidden(true)
    }
}
