#!/usr/bin/env python3
"""Render the NeonRift example at 23:47, Friday March 14.

Only the Python standard library is required. The renderer writes the native
200x200 catalog image and a nearest-neighbor 4x preview.
"""

from pathlib import Path
import math
import struct
import zlib

W = H = 200
RIGHT_TEXT_EDGE = 189
BLACK, WHITE = 0, 1
pixels = [[BLACK for _ in range(W)] for _ in range(H)]

# Monospaced 3x5 glyphs shared with firmware, including the mirrored N.
FONT = {
    "0": (7, 5, 5, 5, 7), "1": (2, 6, 2, 2, 7),
    "2": (7, 1, 7, 4, 7), "3": (7, 1, 7, 1, 7),
    "4": (5, 5, 7, 1, 1), "5": (7, 4, 7, 1, 7),
    "6": (7, 4, 7, 5, 7), "7": (7, 1, 1, 1, 1),
    "8": (7, 5, 7, 5, 7), "9": (7, 5, 7, 1, 7),
    "A": (2, 5, 7, 5, 5), "B": (6, 5, 6, 5, 6),
    "C": (3, 4, 4, 4, 3), "D": (6, 5, 5, 5, 6),
    "E": (7, 4, 6, 4, 7), "F": (7, 4, 6, 4, 4),
    "G": (3, 4, 5, 5, 3), "H": (5, 5, 7, 5, 5),
    "I": (7, 2, 2, 2, 7), "J": (1, 1, 1, 5, 2),
    "K": (5, 5, 6, 5, 5), "L": (4, 4, 4, 4, 7),
    "M": (5, 7, 7, 5, 5), "N": (5, 3, 7, 6, 5),
    "O": (2, 5, 5, 5, 2), "P": (6, 5, 6, 4, 4),
    "Q": (2, 5, 5, 3, 1), "R": (6, 5, 6, 5, 5),
    "S": (3, 4, 2, 1, 6), "T": (7, 2, 2, 2, 2),
    "U": (5, 5, 5, 5, 7), "V": (5, 5, 5, 5, 2),
    "W": (5, 5, 7, 7, 5), "X": (5, 5, 2, 5, 5),
    "Y": (5, 5, 2, 2, 2), "Z": (7, 1, 2, 4, 7),
    "/": (1, 1, 2, 4, 4), ":": (0, 2, 0, 2, 0),
    "%": (5, 1, 2, 4, 5), "-": (0, 0, 7, 0, 0),
    ".": (0, 0, 0, 0, 2), " ": (0, 0, 0, 0, 0),
}

DIGITS = (
    (14, 17, 19, 21, 25, 17, 14), (4, 12, 4, 4, 4, 4, 14),
    (30, 1, 1, 14, 16, 16, 31), (30, 1, 1, 14, 1, 1, 30),
    (18, 18, 18, 31, 2, 2, 2), (31, 16, 16, 30, 1, 1, 30),
    (14, 16, 16, 30, 17, 17, 14), (31, 1, 2, 4, 8, 8, 8),
    (14, 17, 17, 14, 17, 17, 14), (14, 17, 17, 15, 1, 1, 14),
)


def dot(x, y, color=WHITE):
    if 0 <= x < W and 0 <= y < H:
        pixels[y][x] = color


def rect(x, y, w, h, color=WHITE, fill=False):
    if fill:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                dot(xx, yy, color)
    else:
        line(x, y, x + w - 1, y, color)
        line(x, y + h - 1, x + w - 1, y + h - 1, color)
        line(x, y, x, y + h - 1, color)
        line(x + w - 1, y, x + w - 1, y + h - 1, color)


def line(x0, y0, x1, y1, color=WHITE):
    dx, sx = abs(x1 - x0), 1 if x0 < x1 else -1
    dy, sy = -abs(y1 - y0), 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        dot(x0, y0, color)
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def circle(cx, cy, radius, color=WHITE):
    x, y, error = -radius, 0, 2 - 2 * radius
    while x < 0:
        dot(cx - x, cy + y, color); dot(cx - y, cy - x, color)
        dot(cx + x, cy - y, color); dot(cx + y, cy + x, color)
        old = error
        if old <= y:
            y += 1; error += y * 2 + 1
        if old > x or error > y:
            x += 1; error += x * 2 + 1


def glyph_width(ch):
    return 3


def text_width(value, scale=1):
    gap = 2 if scale > 1 else 1
    return (sum(glyph_width(ch) + gap for ch in value) - gap) * scale if value else 0


def text(value, x, y, scale=1):
    for ch in value.upper():
        rows = FONT.get(ch, FONT[" "])
        columns = glyph_width(ch)
        for row, bits in enumerate(rows):
            for col in range(columns):
                if bits & (1 << (columns - 1 - col)):
                    rect(x + col * scale, y + row * scale, scale, scale, fill=True)
        # Match firmware: a wider gap for enlarged text only.
        x += (columns + (2 if scale > 1 else 1)) * scale


def digit(value, x, y):
    for row, bits in enumerate(DIGITS[value]):
        for col in range(5):
            if bits & (1 << (4 - col)):
                rect(x + col * 6, y + row * 6, 5, 5, fill=True)


def frame():
    # Broken perimeter and restrained circuit accents.
    line(6, 2, 57, 2); line(6, 2, 6, 21); line(193, 2, 143, 2)
    line(193, 2, 193, 21); line(6, 178, 6, 197); line(6, 197, 57, 197)
    line(193, 178, 193, 197); line(193, 197, 143, 197)
    for x, y in ((62, 2), (138, 2), (6, 26), (193, 26)):
        rect(x - 1, y - 1, 3, 3, fill=True)

    text("NEON//RIFT", 10, 8)
    text("SYSTEM 24H", 10, 17)
    line(75, 26, 127, 26); rect(73, 24, 3, 3, fill=True); rect(127, 24, 3, 3, fill=True)


def lunar_cycle(year, month, day, hour, minute):
    a = (14 - month) // 12
    y = year + 4800 - a
    m = month + 12 * a - 3
    jdn = day + (153 * m + 2) // 5 + 365 * y + y // 4 - y // 100 + y // 400 - 32045
    julian_date = jdn - 0.5 + hour / 24 + minute / 1440
    phase = ((julian_date - 2451550.25972) % 29.530588853) / 29.530588853
    illumination = round((1 - math.cos(2 * math.pi * phase)) * 50)
    names = ("NEW", "WAX CRES", "FIRST Q", "WAX GIB", "FULL", "WAN GIB", "LAST Q", "WAN CRES")
    phase_name = names[int(phase * 8 + 0.5) % 8]

    cx, cy, radius = 143, 15, 8
    terminator_factor = math.cos(2 * math.pi * phase)
    for py in range(-radius, radius + 1):
        half_width = int(math.sqrt(radius * radius - py * py))
        terminator = terminator_factor * half_width
        for px in range(-half_width, half_width + 1):
            lit = px >= terminator if phase < 0.5 else px <= -terminator
            if lit:
                dot(cx + px, cy + py)
    circle(cx, cy, radius)
    light = f"{illumination}% LIT"
    text(light, RIGHT_TEXT_EDGE - text_width(light), 8)
    text(phase_name, 158, 17)


def clock(hour, minute):
    rect(7, 34, 186, 64)
    digit(hour // 10, 26, 45); digit(hour % 10, 60, 45)
    rect(95, 56, 5, 5, fill=True); rect(95, 74, 5, 5, fill=True)
    digit(minute // 10, 109, 45); digit(minute % 10, 143, 45)


def data_panel():
    footer_y = 143
    line(7, 103, 193, 103)
    line(73, 106, 73, 152)
    line(134, 106, 134, 152)
    text("DATE", 10, 109)
    text("FRI", 10, 121, 2)
    text("MAR 14 2025", 10, footer_y)
    text("STEPS", 80, 109)
    text("08421", 80, 121, 2)
    text("DAILY", 80, footer_y)
    text("WEATHER", 141, 109)
    text("21C", 141, 121, 2)
    text("CLEAR", 141, footer_y)


def battery_label(voltage, adc_saturated=False):
    if not math.isfinite(voltage) or voltage <= 0:
        return "UNKNOWN"
    if adc_saturated or voltage >= 3.90:
        return "HIGH"
    return "LOW" if voltage < 3.60 else "NOMINAL"


def status_bar(voltage, adc_saturated=False, charging=False, usb=False):
    line(7, 157, 193, 157)
    text("BATTERY", 10, 163)
    text(battery_label(voltage, adc_saturated), 10, 174, 2)
    if usb and charging:
        text("CHG", RIGHT_TEXT_EDGE - text_width("CHG", 2), 174, 2)
    statuses = (("WIFI ON", 34), ("BLE ON", 100), ("USB ON" if usb else "USB OFF", 166))
    for status, center_x in statuses:
        text(status, center_x - text_width(status) // 2, 190)


def render(voltage=3.78, adc_saturated=False, charging=True, usb=True):
    rect(0, 0, W, H, BLACK, fill=True)
    frame(); lunar_cycle(2025, 3, 14, 23, 47); clock(23, 47); data_panel()
    status_bar(voltage, adc_saturated, charging, usb)


def png_bytes(scale):
    width, height = W * scale, H * scale
    raw = bytearray()
    for row in pixels:
        expanded = [channel for px in row for channel in ((255,) * scale if px else (0,) * scale)]
        scanline = bytes(expanded)
        for _ in range(scale):
            raw.append(0)
            raw.extend(scanline)
    def chunk(name, data):
        return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    render()
    here = Path(__file__).resolve().parent
    (here / "NeonRift-preview.png").write_bytes(png_bytes(4))
    catalog = here.parents[2] / "extras" / "WatchFaces" / "9_NeonRift.png"
    catalog.write_bytes(png_bytes(1))
    print(f"wrote {catalog} and {here / 'NeonRift-preview.png'}")
