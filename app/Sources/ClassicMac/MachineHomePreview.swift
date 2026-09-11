import AppKit
import SwiftUI

// Ignore an almost uniform, neutral shutdown/boot frame. Keep the original
// image untouched: this is only a presentation decision, never a disk edit.
enum MachinePreviewQuality {
    static func isUseful(_ image: NSImage) -> Bool {
        guard let source = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
            return false
        }
        let width = 48
        let height = 36
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        return pixels.withUnsafeMutableBytes { buffer in
            guard let context = CGContext(
                data: buffer.baseAddress, width: width, height: height,
                bitsPerComponent: 8, bytesPerRow: width * 4,
                space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
            ) else { return true }
            context.interpolationQuality = .low
            context.draw(source, in: CGRect(x: 0, y: 0, width: width, height: height))
            let bytes = buffer.bindMemory(to: UInt8.self)
            var neutralLevels = [Int](repeating: 0, count: 256)
            for offset in stride(from: 0, to: bytes.count, by: 4) {
                let red = Int(bytes[offset])
                let green = Int(bytes[offset + 1])
                let blue = Int(bytes[offset + 2])
                if max(red, green, blue) - min(red, green, blue) <= 10 {
                    neutralLevels[(red + green + blue) / 3] += 1
                }
            }
            // A small tolerance handles dithering and scaling. An actual
            // desktop's menu bar and window details keep it above this limit.
            let dominantCount = neutralLevels.indices.map { level in
                neutralLevels[max(0, level - 3)...min(255, level + 3)].reduce(0, +)
            }.max() ?? 0
            return Double(dominantCount) / Double(width * height) < 0.985
        }
    }
}

struct MachineHomePreview: View {
    let image: NSImage?
    let running: Bool
    let paused: Bool
    let browserDisplay: Bool
    let showMac: () -> Void

    var body: some View {
        Group {
            if running {
                Button(action: showMac) { previewContent }
                    .buttonStyle(.plain)
                    .accessibilityLabel("Show Mac screen")
                    .help(browserDisplay ? "Open the Mac in your browser" : "Show the Mac window")
            } else {
                previewContent
                    .accessibilityElement(children: .ignore)
                    .accessibilityLabel(image == nil ? "No saved screen preview" : "Last useful saved screen")
                    .accessibilityAddTraits(.isImage)
            }
        }
    }

    private var previewContent: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 16)
                .fill(Color(nsColor: .controlBackgroundColor))

            if let image {
                Image(nsImage: image)
                    .resizable()
                    .interpolation(.high)
                    .scaledToFit()
                    .padding(12)
            } else {
                emptyPreview
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .clipShape(RoundedRectangle(cornerRadius: 16))
        .overlay {
            RoundedRectangle(cornerRadius: 16)
                .strokeBorder(.separator.opacity(0.6), lineWidth: 1)
        }
    }

    private var emptyPreview: some View {
        VStack(spacing: 16) {
            ZStack {
                RoundedRectangle(cornerRadius: 22)
                    .fill(Color.primary.opacity(0.035))
                    .frame(width: 94, height: 94)
                Image(systemName: "desktopcomputer")
                    .font(.system(size: 42, weight: .light))
                    .foregroundStyle(.secondary)
            }
            VStack(spacing: 5) {
                Text(running ? (paused ? "Your Mac is paused" : "Your Mac is running") : "Your Mac, ready when you are")
                    .font(.headline)
                    .foregroundStyle(.primary)
                Text(running ? "Open your Mac to see its screen." : "A preview will appear after you start this Mac.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }
        }
        .padding(24)
        .accessibilityElement(children: .combine)
    }
}
