"""Build a multi-resolution Windows .ico from a source PNG.

Usage:
    python tools/build_ico.py assets/icon.png app/winui/scanwise.ico

Requires Pillow:
    pip install Pillow
"""

import argparse
import struct
import sys

from PIL import Image


def make_ico(png_path, ico_path):
    img = Image.open(png_path).convert("RGBA")
    sizes = [256, 128, 64, 48, 32, 24, 16]

    images_data = []

    for size in sizes:
        resized = img.resize((size, size), Image.Resampling.LANCZOS)

        # Windows 32-bit BMP requires BGRA format
        r, g, b, a = resized.split()
        bgra = Image.merge("RGBA", (b, g, r, a))

        # Bottom-up pixel data
        pixels = bgra.tobytes()
        lines = [pixels[i:i + size * 4] for i in range(0, len(pixels), size * 4)]
        pixels_bottom_up = b"".join(reversed(lines))

        # DIB Header (BITMAPINFOHEADER)
        # Height is doubled for the XOR + AND masks
        dib_header = struct.pack("<IiiHHIIiiII",
            40,                     # biSize
            size,                   # biWidth
            size * 2,               # biHeight
            1,                      # biPlanes
            32,                     # biBitCount
            0,                      # biCompression
            len(pixels_bottom_up),  # biSizeImage
            0, 0, 0, 0
        )

        # AND mask (1bpp, row padded to 4 bytes)
        row_bytes = ((size + 31) // 32) * 4
        and_mask = b"\x00" * (row_bytes * size)

        data = dib_header + pixels_bottom_up + and_mask
        images_data.append((size, data))

    with open(ico_path, "wb") as f:
        # ICONDIR
        f.write(struct.pack("<HHH", 0, 1, len(sizes)))

        offset = 6 + 16 * len(sizes)
        for size, data in images_data:
            w = size if size < 256 else 0
            h = size if size < 256 else 0
            # ICONDIRENTRY
            f.write(struct.pack("<BBBBHHII",
                w, h, 0, 0, 1, 32, len(data), offset
            ))
            offset += len(data)

        for size, data in images_data:
            f.write(data)


def main():
    parser = argparse.ArgumentParser(
        description="Convert a PNG image into a multi-resolution Windows .ico file."
    )
    parser.add_argument("png", help="Source PNG file")
    parser.add_argument("ico", help="Output .ico file")
    args = parser.parse_args()

    make_ico(args.png, args.ico)
    print(f"Created {args.ico}")


if __name__ == "__main__":
    main()
