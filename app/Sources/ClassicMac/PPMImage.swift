import Foundation
import AppKit

// Decodes the binary PPM (P6) images that QEMU's screendump command writes.
// PPM is the one format screendump always supports, but AppKit cannot read
// it, so this converts the raw RGB triplets into an NSImage.
enum PPMImage {
    static func load(_ url: URL) -> NSImage? {
        guard let data = try? Data(contentsOf: url) else { return nil }
        var index = 0

        func skipWhitespace() {
            while index < data.count {
                let byte = data[index]
                if byte == 0x20 || byte == 0x09 || byte == 0x0A || byte == 0x0D {
                    index += 1
                } else {
                    break
                }
            }
        }

        func readToken() -> String? {
            skipWhitespace()
            var bytes: [UInt8] = []
            while index < data.count {
                let byte = data[index]
                if byte == 0x20 || byte == 0x09 || byte == 0x0A || byte == 0x0D {
                    break
                }
                bytes.append(byte)
                index += 1
            }
            if bytes.isEmpty {
                return nil
            }
            return String(bytes: bytes, encoding: .ascii)
        }

        guard readToken() == "P6",
              let widthToken = readToken(), let width = Int(widthToken),
              let heightToken = readToken(), let height = Int(heightToken),
              let maxToken = readToken(), let maxValue = Int(maxToken),
              maxValue == 255,
              width > 0, height > 0, width <= 8192, height <= 8192 else {
            return nil
        }

        // Exactly one whitespace byte separates the header from pixel data.
        index += 1
        let pixelBytes = width * height * 3
        guard index + pixelBytes <= data.count else { return nil }

        guard let rep = NSBitmapImageRep(
            bitmapDataPlanes: nil,
            pixelsWide: width,
            pixelsHigh: height,
            bitsPerSample: 8,
            samplesPerPixel: 3,
            hasAlpha: false,
            isPlanar: false,
            colorSpaceName: .deviceRGB,
            bytesPerRow: width * 3,
            bitsPerPixel: 24
        ), let destination = rep.bitmapData else {
            return nil
        }

        data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
            let source = raw.baseAddress!.advanced(by: index)
            destination.update(from: source.assumingMemoryBound(to: UInt8.self), count: pixelBytes)
        }

        let image = NSImage(size: NSSize(width: width, height: height))
        image.addRepresentation(rep)
        return image
    }
}
