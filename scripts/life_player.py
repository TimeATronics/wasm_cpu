"""Game of Life animation player — reads frames from sim_s32 stdout.

Usage:
    python scripts/life_player.py programs/gol.bin
"""

import subprocess
import sys
import os
import threading
import tkinter as tk
from PIL import Image, ImageTk

FRAME_SIZE = 1024
W, H = 128, 64
SCALE = 4


class LifePlayer:
    def __init__(self, bin_path):
        sim_dir = os.path.dirname(os.path.abspath(__file__))
        sim_exe = os.path.join(sim_dir, '..', 'sim_s32.exe')
        bin_full = os.path.abspath(bin_path)

        if not os.path.exists(sim_exe):
            sim_exe = os.path.join(sim_dir, 'sim_s32.exe')
        if not os.path.exists(sim_exe):
            sim_exe = os.path.join(os.getcwd(), 'sim_s32.exe')

        self.proc = subprocess.Popen(
            [sim_exe, bin_full],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        self.current_frame = None
        self.frame_count = 0
        self.done = False

        self.reader = threading.Thread(target=self._read_frames, daemon=True)
        self.reader.start()

        self.root = tk.Tk()
        self.root.title("Game of Life - WasmCPU")
        self.root.resizable(False, False)
        self.root.configure(bg='#0A0A0A')

        self.img_label = tk.Label(self.root, bg='#0A0A0A')
        self.img_label.pack()

        self.info_label = tk.Label(
            self.root, text="Waiting for first frame...",
            fg='#666', bg='#0A0A0A', font=('Consolas', 10))
        self.info_label.pack()

        self._update()
        self.root.mainloop()

    def _read_frames(self):
        while True:
            data = self.proc.stdout.read(FRAME_SIZE)
            if len(data) < FRAME_SIZE:
                break
            self.current_frame = data
            self.frame_count += 1
        self.done = True

    def _update(self):
        if self.current_frame:
            frame = self.current_frame
            rgba = bytearray(W * H * 4)
            for i in range(FRAME_SIZE):
                page, col = i // W, i % W
                byte_val = frame[i]
                for bit in range(8):
                    y = page * 8 + bit
                    x = col
                    alive = (byte_val >> bit) & 1
                    idx = (y * W + x) * 4
                    if alive:
                        rgba[idx:idx+4] = (50, 255, 50, 255)
                    else:
                        rgba[idx:idx+4] = (10, 10, 10, 255)

            img = Image.frombytes('RGBA', (W, H), bytes(rgba))
            img = img.resize((W * SCALE, H * SCALE), Image.NEAREST)
            photo = ImageTk.PhotoImage(img)
            self.img_label.configure(image=photo)
            self.img_label.image = photo

            status = f"Frame: {self.frame_count}" + (" (done)" if self.done else "")
            self.info_label.configure(text=status)

        if not self.done or self.proc.poll() is None:
            self.root.after(100, self._update)
        else:
            self.root.after(2000, self.root.destroy)


def main():
    if len(sys.argv) < 2:
        print("Usage: python life_player.py <program.bin>")
        sys.exit(1)
    LifePlayer(sys.argv[1])


if __name__ == '__main__':
    main()