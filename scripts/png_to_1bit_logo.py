#!/usr/bin/env python3
"""
Convert a PNG to a 1-bit row-major C array for TFT_eSPI drawBitmap.
Output: Horizontal 1 bit per pixel (row-major, 8 pixels per byte, MSB = left).
Usage: py png_to_1bit_logo.py [path_to.png]
Default input: ../assets/logo_icon.png
"""
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Install Pillow: py -m pip install Pillow", file=sys.stderr)
    sys.exit(1)

# Target height; width scales proportionally, then we round to multiple of 8 for clean bytes
TARGET_H = 28
MAX_W = 56
MIN_W = 40

def main():
    script_dir = Path(__file__).resolve().parent
    default_input = script_dir.parent / "assets" / "logo_icon.png"
    in_path = Path(sys.argv[1]) if len(sys.argv) > 1 else default_input
    if not in_path.is_file():
        print(f"Error: not found: {in_path}", file=sys.stderr)
        sys.exit(2)

    img = Image.open(in_path).convert("L")  # grayscale
    w, h = img.size
    # Scale to height TARGET_H, proportional width
    new_w = max(MIN_W, min(MAX_W, int(w * TARGET_H / h)))
    # Optional: keep aspect exactly
    new_w = int(round(w * TARGET_H / h))
    new_w = max(MIN_W, min(MAX_W, new_w))

    img = img.resize((new_w, TARGET_H), Image.Resampling.LANCZOS)
    pixels = img.load()

    # 1-bit: non-black = foreground (1). Blue on black is dark in grayscale, so use low threshold.
    THRESHOLD = 50
    rows = []
    for y in range(TARGET_H):
        row_bytes = []
        for x_start in range(0, new_w, 8):
            byte_val = 0
            for i in range(8):
                x = x_start + i
                if x < new_w and pixels[x, y] > THRESHOLD:
                    byte_val |= 0x80 >> i
            row_bytes.append(byte_val)
        rows.append(row_bytes)

    bytes_per_row = len(rows[0])
    total_bytes = bytes_per_row * TARGET_H

    print(f"// Converted from {in_path.name}: {new_w}x{TARGET_H}, {bytes_per_row} bytes/row")
    print(f"#define LOGO_W  {new_w}")
    print(f"#define LOGO_H  {TARGET_H}")
    print(f"const uint8_t logoBitmap[] PROGMEM = {{")
    for row in rows:
        hex_str = ", ".join(f"0x{b:02X}" for b in row)
        print(f"  {hex_str},")
    print("};")

if __name__ == "__main__":
    main()
