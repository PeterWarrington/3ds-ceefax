#!/usr/bin/env python3
"""Generate icon.png, banner.png and banner.wav using the teletext font.

    python3 tools/gen_assets.py source/font.h
"""
import math
import re
import struct
import sys
import wave
import zlib

BLACK, WHITE = (0, 0, 0), (255, 255, 255)
BLUE, YELLOW = (0, 0, 255), (255, 255, 0)
FASTEXT = [(255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 255, 255)]


def load_font(path):
    rows = re.findall(r"\{((?:0x[0-9a-f]{2},?){9})\}", open(path).read())
    return [[int(v, 16) for v in r.split(",")] for r in rows]


class Image:
    def __init__(self, w, h, color=BLACK):
        self.w, self.h = w, h
        self.px = [[color] * w for _ in range(h)]

    def fill(self, x, y, w, h, color):
        for j in range(max(y, 0), min(y + h, self.h)):
            for i in range(max(x, 0), min(x + w, self.w)):
                self.px[j][i] = color

    def text(self, font, s, x0, y0, color, scale):
        for n, ch in enumerate(s):
            for r, bits in enumerate(font[ord(ch) - 0x20]):
                for c in range(5):
                    if bits & (0x10 >> c):
                        self.fill(x0 + (n * 6 + 1 + c) * scale, y0 + r * scale, scale, scale, color)

    def save(self, path):
        raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in self.px)

        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
        open(path, "wb").write(png)


def text_w(s, scale):
    return len(s) * 6 * scale


def icon(font):
    img = Image(48, 48, BLUE)
    img.text(font, "CEE", 5, 3, YELLOW, 2)
    img.text(font, "FAX", 5, 22, YELLOW, 2)
    img.fill(0, 40, 48, 1, BLACK)
    for i, c in enumerate(FASTEXT):
        img.fill(i * 12, 41, 12, 7, c)
    img.save("icon.png")


def banner(font):
    img = Image(256, 128, BLACK)
    img.text(font, "P100  CEEFAX 1 100", 8, 6, WHITE, 2)
    img.fill(0, 30, 256, 52, BLUE)
    title, url = "3DS CEEFAX", "lilpete.me/3dsceefax"
    img.text(font, title, (256 - text_w(title, 4)) // 2, 42, YELLOW, 4)
    img.text(font, url, (256 - text_w(url, 2)) // 2, 90, WHITE, 2)
    for i, c in enumerate(FASTEXT):
        img.fill(i * 64, 114, 64, 14, c)
    img.save("banner.png")


def chime():
    # 16-bit stereo 44.1kHz; the HOME menu won't play mono banner audio.
    rate, frames = 44100, []
    for freq, dur in ((880, 0.18), (1320, 0.32)):
        n = int(rate * dur)
        for i in range(n):
            env = min(1.0, i / 200) * (1 - i / n) ** 2
            frames.append(int(9000 * env * math.sin(2 * math.pi * freq * i / rate)))
    with wave.open("banner.wav", "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<hh", s, s) for s in frames))


if __name__ == "__main__":
    font = load_font(sys.argv[1])
    icon(font)
    banner(font)
    chime()
