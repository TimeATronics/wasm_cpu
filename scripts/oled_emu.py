"""SSD1306 128x64 OLED emulator for WasmCPU simulator.

Usage:
    python scripts/oled_emu.py framebuffer.bin         # tkinter render
    python scripts/oled_emu.py --ascii framebuffer.bin  # ASCII debug dump
    python scripts/oled_emu.py --hex framebuffer.bin    # hex dump
"""

import sys


WIDTH = 128
HEIGHT = 64
FB_SIZE = 1024
SCALE = 4


def bytes_to_pixels(data):
    pixels = [[False] * WIDTH for _ in range(HEIGHT)]
    for i in range(min(FB_SIZE, len(data))):
        page = i // WIDTH
        col = i % WIDTH
        byte_val = data[i]
        for bit in range(8):
            row = page * 8 + bit
            if row < HEIGHT and (byte_val & (1 << bit)):
                pixels[row][col] = True
    return pixels


def render_ascii(data):
    """Print framebuffer as ASCII art (each pixel = 2 chars)."""
    pixels = bytes_to_pixels(data)
    # Print every other row to fit in terminal (64/2 = 32 rows)
    print(f"OLED 128x64 (rendered at half vertical resolution, 2x horizontal):")
    print("+" + "-" * 128 + "+")
    for y in range(0, HEIGHT, 2):
        line = "|"
        for x in range(WIDTH):
            top = pixels[y][x] if y < HEIGHT else False
            bot = pixels[y + 1][x] if y + 1 < HEIGHT else False
            if top and bot:
                line += "#"
            elif top:
                line += "'"
            elif bot:
                line += "."
            else:
                line += " "
        line += "|"
        print(line)
    print("+" + "-" * 128 + "+")

    # Also print stats
    total_on = sum(1 for y in range(HEIGHT) for x in range(WIDTH) if pixels[y][x])
    print(f"Pixels on: {total_on} / {WIDTH * HEIGHT}")


def render_hex(data):
    """Hex dump of framebuffer organized by pages."""
    print("Framebuffer hex dump (8 pages × 128 columns):")
    print("Each row shows first 16 columns of each page")
    print("         |", end="")
    for c in range(0, 129, 16):
        print(f" col{c:3d}  |", end="")
    print()
    for page in range(8):
        print(f"Page {page}: |", end="")
        for cbase in range(0, 128, 16):
            for col in range(cbase, min(cbase+16, 128)):
                idx = page * WIDTH + col
                print(f" {data[idx]:02x}", end="")
            print(" |", end="")
        print()


def render_tkinter(data):
    import tkinter as tk
    pixels = bytes_to_pixels(data)
    cw, ch = WIDTH * SCALE, HEIGHT * SCALE

    root = tk.Tk()
    root.title("OLED Emulator")
    root.resizable(False, False)

    canvas = tk.Canvas(root, width=cw, height=ch,
                       bg="#0A0A0A", highlightthickness=0)
    canvas.pack()

    for y in range(HEIGHT):
        for x in range(WIDTH):
            if pixels[y][x]:
                x1, y1 = x * SCALE, y * SCALE
                x2, y2 = x1 + SCALE - 1, y1 + SCALE - 1
                canvas.create_rectangle(x1, y1, x2, y2,
                                        fill="#33FF33", outline='', width=0)

    root.mainloop()


def main():
    mode = "gui"
    filepath = None

    for arg in sys.argv[1:]:
        if arg == "--ascii":
            mode = "ascii"
        elif arg == "--hex":
            mode = "hex"
        elif not arg.startswith("--"):
            filepath = arg

    if filepath:
        with open(filepath, 'rb') as f:
            data = f.read()
    else:
        data = sys.stdin.buffer.read()

    if len(data) < FB_SIZE:
        print(f"Need {FB_SIZE} bytes, got {len(data)}", file=sys.stderr)
        sys.exit(1)

    data = data[-FB_SIZE:]

    if mode == "ascii":
        render_ascii(data)
    elif mode == "hex":
        render_hex(data)
    else:
        render_tkinter(data)


if __name__ == '__main__':
    main()