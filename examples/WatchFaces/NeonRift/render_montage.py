#!/usr/bin/env python3
"""Make a shareable 1920x1080 PNG of the watch face and all clock-tools screens.

Standard library only. All screen pixels use the existing preview renderers;
labels and build notes are outside the screenshots. No live watch data is used.
"""
from pathlib import Path

import render_preview as face
import render_clock_tools_preview as tools

WIDTH, HEIGHT, SCALE = 960, 540, 2
COL_X = (32, 264, 496, 728)
ROW_Y = (72, 304)


def watch_face():
    face.render()
    return tools.snapshot()


SCREENS = (
    ("WATCH FACE", watch_face),
    ("CLOCK TOOLS", tools.render_menu),
    ("ALARM EDITOR", tools.render_alarm),
    ("TIMER SETUP", tools.render_timer_editor),
    ("TIMER RUNNING", tools.render_timer_running),
    ("STOPWATCH", tools.render_stopwatch),
    ("ALERT", tools.render_alert),
)


def centered(value, x, y, scale=1):
    tools.text(value, x + (200 - face.text_width(value, scale)) // 2, y, scale)


def build_notes(x, y):
    # This eighth tile is explicitly labeled as notes, not a firmware screen.
    for x0, y0, x1, y1 in (
        (x + 6, y + 2, x + 30, y + 2), (x + 6, y + 2, x + 6, y + 26),
        (x + 169, y + 2, x + 193, y + 2), (x + 193, y + 2, x + 193, y + 26),
        (x + 6, y + 173, x + 6, y + 197), (x + 6, y + 197, x + 30, y + 197),
        (x + 193, y + 173, x + 193, y + 197), (x + 169, y + 197, x + 193, y + 197),
    ):
        tools.line(x0, y0, x1, y1)
    centered("NEON//RIFT", x, y + 28, 2)
    centered("WATCHY / V3", x, y + 47)
    tools.line(x + 30, y + 63, x + 166, y + 63)
    face.rect(x + 29, y + 62, 3, 3, fill=True)
    face.rect(x + 165, y + 62, 3, 3, fill=True)
    centered("07 SCREENS", x, y + 83, 2)
    centered("200 X 200 PIXELS", x, y + 108)
    centered("MONOCHROME E-PAPER", x, y + 123)
    centered("ALARM / TIMER / STOPWATCH", x, y + 146)
    centered("ALL TOOLS ON YOUR WRIST", x, y + 174)


def render_montage():
    previous = face.pixels, face.W, face.H
    try:
        # Isolate generation so it does not mutate another renderer's canvas.
        face.pixels = [[face.BLACK] * 200 for _ in range(200)]
        face.W = face.H = 200
        screens = [(label, renderer()) for label, renderer in SCREENS]
        face.pixels = [[face.BLACK] * WIDTH for _ in range(HEIGHT)]
        face.W, face.H = WIDTH, HEIGHT

        tools.text("NEON//RIFT", 32, 16, 3)
        tools.text("WATCHY V3 / WATCHFACE / CLOCK TOOLS", 32, 39)
        heading = "SCREEN COLLECTION"
        tools.text(heading, 928 - face.text_width(heading, 2), 19, 2)
        subheading = "MONOCHROME / 200X200 / SAMPLE DATA"
        tools.text(subheading, 928 - face.text_width(subheading), 39)
        tools.line(32, 49, 928, 49)

        for index, (label, pixels) in enumerate(screens):
            x, y = COL_X[index % 4], ROW_Y[index // 4]
            face.rect(x, y - 17, 14, 11)
            tools.text(f"{index + 1:02d}", x + 3, y - 14)
            tools.text(label, x + 22, y - 14)
            for row, source in enumerate(pixels):
                face.pixels[y + row][x:x + 200] = source
        tools.text("BUILD NOTES", COL_X[3] + 22, ROW_Y[1] - 14)
        build_notes(COL_X[3], ROW_Y[1])

        tools.line(32, 515, 928, 515)
        tools.text("NEONRIFT / CLOCK TOOLS", 32, 524)
        note = "RENDERED UI PREVIEWS / SAMPLE VALUES"
        tools.text(note, 928 - face.text_width(note), 524)
        return face.pixels
    finally:
        face.pixels, face.W, face.H = previous


def save_montage(path, canvas):
    previous = face.pixels, face.W, face.H
    try:
        face.pixels, face.W, face.H = canvas, WIDTH, HEIGHT
        path.write_bytes(face.png_bytes(SCALE))
    finally:
        face.pixels, face.W, face.H = previous


if __name__ == "__main__":
    path = Path(__file__).resolve().parent / "NeonRift-montage.png"
    save_montage(path, render_montage())
    print(f"wrote {path} ({WIDTH * SCALE}x{HEIGHT * SCALE})")
