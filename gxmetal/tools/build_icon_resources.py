#!/usr/bin/env python3
"""Convert the GXMetal RGBA master into a classic Mac icon-family Rez file."""

from __future__ import annotations

import argparse
from pathlib import Path

try:
    from PIL import Image, ImageFilter
except ImportError as error:  # pragma: no cover - developer-facing failure
    raise SystemExit(
        "Pillow is required to regenerate GXMetalIcon.r (python3 -m pip install pillow)"
    ) from error


MAC_4BIT_PALETTE = (
    (255, 255, 255),  # white
    (255, 255, 0),    # yellow
    (255, 102, 0),    # orange
    (221, 0, 0),      # red
    (255, 0, 255),    # magenta
    (102, 0, 153),    # purple
    (0, 0, 204),      # blue
    (0, 255, 255),    # cyan
    (0, 204, 0),      # green
    (0, 102, 0),      # dark green
    (153, 102, 51),   # brown
    (255, 204, 153),  # tan
    (204, 204, 204),  # light gray
    (153, 153, 153),  # medium gray
    (85, 85, 85),     # dark gray
    (0, 0, 0),        # black
)


def mac_8bit_palette() -> tuple[tuple[int, int, int], ...]:
    """Return the standard Mac 6x6x6 color cube at indices 0 through 215."""
    colors = []
    for red_offset in range(6):
        for green_offset in range(6):
            for blue_offset in range(6):
                colors.append(
                    (255 - 51 * red_offset,
                     255 - 51 * green_offset,
                     255 - 51 * blue_offset)
                )
    return tuple(colors)


MAC_8BIT_PALETTE = mac_8bit_palette()


def fitted_icon(master: Image.Image, size: int) -> Image.Image:
    """Crop transparent padding and fit the artwork into a classic icon grid."""
    if master.size == (32, 32):
        if size == 32:
            return master.copy()
        return master.resize((size, size), Image.Resampling.LANCZOS).filter(
            ImageFilter.UnsharpMask(radius=0.4, percent=90, threshold=3)
        )

    alpha = master.getchannel("A")
    bounds = alpha.point(lambda value: 255 if value >= 16 else 0).getbbox()
    if bounds is None:
        raise ValueError("icon master contains no visible pixels")

    artwork = master.crop(bounds)
    inner_size = size - (4 if size == 32 else 2)
    artwork.thumbnail((inner_size, inner_size), Image.Resampling.LANCZOS)
    artwork = artwork.filter(
        ImageFilter.UnsharpMask(radius=0.55, percent=120, threshold=3)
    )
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    origin = ((size - artwork.width) // 2, (size - artwork.height) // 2)
    canvas.alpha_composite(artwork, origin)
    return canvas


def visible_rgb(pixel: tuple[int, int, int, int]) -> tuple[int, int, int]:
    """Composite a partially transparent edge over white before quantization."""
    red, green, blue, alpha = pixel
    return tuple(
        (component * alpha + 255 * (255 - alpha) + 127) // 255
        for component in (red, green, blue)
    )


def rgba_pixels(icon: Image.Image) -> list[tuple[int, int, int, int]]:
    raw = icon.tobytes()
    return [tuple(raw[offset:offset + 4])
            for offset in range(0, len(raw), 4)]


def nearest_color(
    color: tuple[int, int, int], palette: tuple[tuple[int, int, int], ...]
) -> int:
    red, green, blue = color
    return min(
        range(len(palette)),
        key=lambda index: (
            (red - palette[index][0]) ** 2
            + (green - palette[index][1]) ** 2
            + (blue - palette[index][2]) ** 2
        ),
    )


def indexed_pixels(
    icon: Image.Image, palette: tuple[tuple[int, int, int], ...]
) -> bytes:
    cache: dict[tuple[int, int, int], int] = {}
    result = bytearray()
    for pixel in rgba_pixels(icon):
        if pixel[3] < 48:
            result.append(0)
            continue
        color = visible_rgb(pixel)
        index = cache.get(color)
        if index is None:
            index = nearest_color(color, palette)
            cache[color] = index
        result.append(index)
    return bytes(result)


def pack_nibbles(values: bytes) -> bytes:
    if len(values) % 2:
        raise ValueError("4-bit icon has an odd pixel count")
    return bytes(
        (values[index] << 4) | values[index + 1]
        for index in range(0, len(values), 2)
    )


def monochrome_icon(icon: Image.Image) -> tuple[bytes, bytes]:
    width, height = icon.size
    mask = [pixel[3] >= 48 for pixel in rgba_pixels(icon)]
    image_bits = []
    for y in range(height):
        for x in range(width):
            offset = y * width + x
            if not mask[offset]:
                image_bits.append(False)
                continue
            edge = any(
                nx < 0 or nx >= width or ny < 0 or ny >= height
                or not mask[ny * width + nx]
                for nx, ny in ((x - 1, y), (x + 1, y),
                               (x, y - 1), (x, y + 1))
            )
            red, green, blue = visible_rgb(icon.getpixel((x, y)))
            luminance = (299 * red + 587 * green + 114 * blue) // 1000
            image_bits.append(edge or luminance < 118)

    def pack(bits: list[bool]) -> bytes:
        output = bytearray()
        for index in range(0, len(bits), 8):
            value = 0
            for bit in range(8):
                if bits[index + bit]:
                    value |= 1 << (7 - bit)
            output.append(value)
        return bytes(output)

    return pack(image_bits), pack(mask)


def rez_hex(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16].hex().upper()
        grouped = " ".join(chunk[index:index + 4]
                           for index in range(0, len(chunk), 4))
        rows.append(f'    $"{grouped}"')
    return "\n".join(rows)


def resource(resource_type: str, data: bytes) -> str:
    return (
        f"data '{resource_type}' (GXMETAL_ICON_RESOURCE_ID, purgeable) {{\n"
        f"{rez_hex(data)}\n"
        "};\n"
    )


def build_rez(master: Image.Image) -> tuple[str, Image.Image, Image.Image]:
    large = fitted_icon(master, 32)
    small = fitted_icon(master, 16)
    large_image, large_mask = monochrome_icon(large)
    small_image, small_mask = monochrome_icon(small)

    generated = [
        "/* Generated by gxmetal/tools/build_icon_resources.py. */",
        "/* Source: gxmetal/guest/art/GXMetalIcon-master.gif. */",
        "#ifndef GXMETAL_ICON_RESOURCE_ID",
        "#define GXMETAL_ICON_RESOURCE_ID 128",
        "#endif",
        "",
        resource("icl8", indexed_pixels(large, MAC_8BIT_PALETTE)),
        resource("icl4", pack_nibbles(indexed_pixels(large, MAC_4BIT_PALETTE))),
        resource("ICN#", large_image + large_mask),
        resource("ics8", indexed_pixels(small, MAC_8BIT_PALETTE)),
        resource("ics4", pack_nibbles(indexed_pixels(small, MAC_4BIT_PALETTE))),
        resource("ics#", small_image + small_mask),
        resource("ICON", large_image),
    ]
    return "\n".join(generated), large, small


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--preview-dir", type=Path)
    args = parser.parse_args()

    master = Image.open(args.source).convert("RGBA")
    rez, large, small = build_rez(master)
    args.output.write_text(rez, encoding="utf-8")
    if args.preview_dir is not None:
        args.preview_dir.mkdir(parents=True, exist_ok=True)
        large.save(args.preview_dir / "GXMetal-32.png")
        small.save(args.preview_dir / "GXMetal-16.png")


if __name__ == "__main__":
    main()
