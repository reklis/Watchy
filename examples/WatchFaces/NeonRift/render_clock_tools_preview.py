#!/usr/bin/env python3
"""Render the NeonRift clock-tools layouts using sample state, not live readings.

Python standard library only. Host tests compare these pixels with the firmware
renderer in src/ClockToolsUI.h. Run to write individual and paired 4x previews.
"""
from pathlib import Path

import render_preview as face

BLACK, WHITE = face.BLACK, face.WHITE
RIGHT = 189


def line(x0, y0, x1, y1, color=WHITE):
    """Match Adafruit GFX's Bresenham tie-breaking and endpoint ordering."""
    steep = abs(y1 - y0) > abs(x1 - x0)
    if steep:
        x0, y0, x1, y1 = y0, x0, y1, x1
    if x0 > x1:
        x0, x1, y0, y1 = x1, x0, y1, y0
    dx, dy = x1 - x0, abs(y1 - y0)
    error, step = dx // 2, 1 if y0 < y1 else -1
    for x in range(x0, x1 + 1):
        face.dot(y0, x, color) if steep else face.dot(x, y0, color)
        error -= dy
        if error < 0:
            y0 += step
            error += dx


def circle(cx, cy, radius, color=WHITE):
    """Match Adafruit GFX's circle rasterization."""
    error, dx, dy, x, y = 1 - radius, 1, -2 * radius, 0, radius
    for px, py in ((cx, cy + radius), (cx, cy - radius),
                   (cx + radius, cy), (cx - radius, cy)):
        face.dot(px, py, color)
    while x < y:
        if error >= 0:
            y -= 1
            dy += 2
            error += dy
        x += 1
        dx += 2
        error += dx
        for px, py in ((cx + x, cy + y), (cx - x, cy + y),
                       (cx + x, cy - y), (cx - x, cy - y),
                       (cx + y, cy + x), (cx - y, cy + x),
                       (cx + y, cy - x), (cx - y, cy - x)):
            face.dot(px, py, color)


def text(value, x, y, scale=1, color=WHITE):
    for ch in value.upper():
        for row, bits in enumerate(face.FONT.get(ch, face.FONT[" "])):
            for col in range(3):
                if bits & (1 << (2 - col)):
                    face.rect(x + col * scale, y + row * scale,
                              scale, scale, color, fill=True)
        x += (5 if scale > 1 else 4) * scale


def format_time(minutes):
    return f"{minutes // 60:02d}:{minutes % 60:02d}"


def time_width(value, scale):
    return (sum(3 if ch == ":" else 6 for ch in value) - 1) * scale


def time_text(value, x, y, scale=2, color=WHITE):
    for ch in value:
        if ch == ":":
            face.rect(x, y + 2 * scale, scale, scale, color, fill=True)
            face.rect(x, y + 4 * scale, scale, scale, color, fill=True)
            x += 3 * scale
        else:
            for row, bits in enumerate(face.DIGITS[int(ch)]):
                for col in range(5):
                    if bits & (1 << (4 - col)):
                        face.rect(x + col * scale, y + row * scale,
                                  scale, scale, color, fill=True)
            x += 6 * scale


def panel(x, y, width, height, selected=False):
    face.rect(x, y, width, height, WHITE, fill=selected)
    right, bottom = x + width - 1, y + height - 1
    for cx, cy in ((x, y), (right - 2, y), (x, bottom - 2), (right - 2, bottom - 2)):
        face.rect(cx, cy, 3, 3, BLACK, fill=True)
    line(x, y + 3, x + 3, y)
    line(right - 3, y, right, y + 3)
    line(x, bottom - 3, x + 3, bottom)
    line(right - 3, bottom, right, bottom - 3)


def badge(value, right, y, ink=WHITE):
    width = face.text_width(value) + 8
    face.rect(right - width + 1, y, width, 13, ink)
    text(value, right - width + 5, y + 4, color=ink)


def chrome(title, section):
    face.rect(0, 0, 200, 200, BLACK, fill=True)
    for x0, y0, x1, y1 in (
        (6, 2, 57, 2), (6, 2, 6, 21), (143, 2, 193, 2), (193, 2, 193, 21),
        (6, 178, 6, 197), (6, 197, 57, 197),
        (193, 178, 193, 197), (143, 197, 193, 197),
    ):
        line(x0, y0, x1, y1)
    for x in (62, 138):
        face.rect(x - 1, 1, 3, 3, fill=True)
    text("NEON//RIFT", 10, 8)
    text(section, RIGHT - face.text_width(section), 8)
    text(title, 10, 23, 2)
    line(160, 27, 174, 27)
    line(174, 27, 179, 22)
    line(179, 22, 189, 22)
    face.rect(187, 20, 3, 3)
    line(10, 38, RIGHT, 38)
    face.rect(9, 37, 3, 3, fill=True)


def footer(left_top, right_top, left_bottom, right_bottom):
    line(10, 176, RIGHT, 176)
    text(left_top, 10, 182)
    text(right_top, RIGHT - face.text_width(right_top), 182)
    text(left_bottom, 10, 191)
    text(right_bottom, RIGHT - face.text_width(right_bottom), 191)


def tool_icon(kind, cx, cy, ink=WHITE):
    if kind == "timer":
        line(cx - 6, cy - 9, cx + 6, cy - 9, ink)
        line(cx - 6, cy + 9, cx + 6, cy + 9, ink)
        line(cx - 5, cy - 7, cx + 5, cy + 7, ink)
        line(cx + 5, cy - 7, cx - 5, cy + 7, ink)
        line(cx - 4, cy - 5, cx + 4, cy - 5, ink)
        line(cx - 4, cy + 7, cx + 4, cy + 7, ink)
    else:
        circle(cx, cy, 7, ink)
        line(cx, cy, cx, cy - 4, ink)
        line(cx, cy, cx + 3, cy, ink)
        if kind == "alarm":
            line(cx - 8, cy - 7, cx - 4, cy - 10, ink)
            line(cx + 4, cy - 10, cx + 8, cy - 7, ink)
            line(cx - 5, cy + 6, cx - 7, cy + 9, ink)
            line(cx + 5, cy + 6, cx + 7, cy + 9, ink)
        else:
            line(cx - 2, cy - 10, cx + 2, cy - 10, ink)
            line(cx, cy - 10, cx, cy - 7, ink)
            line(cx + 5, cy - 7, cx + 8, cy - 10, ink)


def time_panel(minutes, label, unit, field=-1):
    panel(10, 46, 180, 90)
    text(label, 18, 54)
    badge(unit, 181, 50)
    value, scale = format_time(minutes), 5
    while scale > 1 and time_width(value, scale) > 164:
        scale -= 1
    start = (200 - time_width(value, scale)) // 2
    time_text(value, start, 75 + (35 - 7 * scale) // 2, scale)
    hours = value.index(":")
    hour_width = (6 * hours - 1) * scale
    minute_start, minute_width = start + (6 * hours + 3) * scale, 11 * scale
    if field == 0:
        face.rect(start, 113, hour_width, 2, fill=True)
    if field == 1:
        face.rect(minute_start, 113, minute_width, 2, fill=True)
    text("HOUR", start + int((hour_width - face.text_width("HOUR")) / 2), 123)
    text("MINUTE", minute_start + int((minute_width - face.text_width("MINUTE")) / 2), 123)


def state_panel(label, state):
    panel(10, 144, 180, 22)
    text(label, 18, 153)
    badge(state, 182, 148)


def snapshot():
    return [row[:] for row in face.pixels]


def render_menu(selected=0, alarm_minutes=450, alarm_enabled=True,
                timer_minutes=25, timer_running=False,
                stopwatch_minutes=0, stopwatch_running=False):
    selected %= 3
    chrome("CLOCK TOOLS", "TOOLS/01")
    tools = (
        ("ALARM", alarm_minutes, "ON" if alarm_enabled else "OFF", "alarm"),
        ("TIMER", timer_minutes, "RUNNING" if timer_running else "READY", "timer"),
        ("STOPWATCH", stopwatch_minutes, "RUNNING" if stopwatch_running else "PAUSED", "stopwatch"),
    )
    for index, (label, minutes, status, icon) in enumerate(tools):
        y = 45 + index * 42
        ink = BLACK if index == selected else WHITE
        panel(10, y, 180, 36, index == selected)
        tool_icon(icon, 25, y + 18, ink)
        line(39, y + 6, 39, y + 29, ink)
        text(label, 45, y + 5, color=ink)
        value = format_time(minutes)
        available = 182 - (face.text_width(status) + 8) + 1 - 45 - 5
        scale = 2 if time_width(value, 2) <= available else 1
        time_text(value, 45, y + 15, scale, ink)
        badge(status, 182, y + 12, ink)
    footer("UP/DOWN MOVE", "MENU OPEN", "BACK EXIT", f"0{selected + 1} / 03")
    return snapshot()


def render_alarm(hour=7, minute=30, enabled=True, field=0):
    chrome("ALARM", "EDIT/01")
    time_panel(hour * 60 + minute, "SET WAKE TIME", "24H", field if field < 2 else -1)
    ink = BLACK if field == 2 else WHITE
    panel(10, 144, 180, 22, field == 2)
    text("ENABLED", 18, 153, color=ink)
    face.rect(141, 151, 7, 7, ink)
    if enabled:
        line(142, 154, 144, 156, ink)
        line(144, 156, 147, 152, ink)
    badge("ON" if enabled else "OFF", 182, 148, ink)
    footer("UP/DOWN ADJUST", "BACK CANCEL", "MENU SAVE" if field == 2 else "MENU NEXT",
           ("EDIT HOUR", "EDIT MIN", "EDIT STATE")[field % 3])
    return snapshot()


def render_timer_editor(hours=0, minutes=25, field=0):
    chrome("TIMER", "EDIT/02")
    time_panel(hours * 60 + minutes, "SET DURATION", "HH:MM", field)
    state_panel("COUNTDOWN", "READY")
    footer("UP/DOWN ADJUST", "BACK CANCEL", "MENU NEXT" if field == 0 else "MENU START",
           "EDIT HOUR" if field == 0 else "EDIT MIN")
    return snapshot()


def render_timer_running(minutes=25):
    chrome("TIMER", "TOOLS/02")
    time_panel(minutes, "TIME REMAINING", "HH:MM")
    state_panel("COUNTDOWN", "RUNNING")
    footer("MENU CANCEL", "BACK TOOLS", "HH:MM", "ROUNDS UP")
    return snapshot()


def render_stopwatch(minutes=75, running=True):
    chrome("STOPWATCH", "TOOLS/03")
    time_panel(minutes, "ELAPSED TIME", "HH:MM")
    state_panel("STATE", "RUNNING" if running else "PAUSED")
    footer("MENU PAUSE" if running else "MENU START", "DOWN RESET", "BACK TOOLS", "HH:MM")
    return snapshot()


def render_alert(alarm=True, timer=False):
    chrome("ALERT", "EVENT/01")
    panel(10, 46, 180, 90, True)
    tool_icon("alarm" if alarm else "timer", 100, 67, BLACK)
    if alarm:
        text("ALARM", (200 - face.text_width("ALARM", 2)) // 2, 92, 2, BLACK)
    if timer:
        text("TIMER DONE", (200 - face.text_width("TIMER DONE", 2)) // 2,
             115 if alarm else 92, 2, BLACK)
    state_panel("ACK REQUIRED", "ALERT")
    footer("ANY BUTTON DISMISS", "", "RETURN TO FACE", "ACK")
    return snapshot()


def save_preview(path, canvas):
    previous = face.pixels, face.W, face.H
    try:
        face.pixels, face.W, face.H = canvas, len(canvas[0]), len(canvas)
        path.write_bytes(face.png_bytes(4))
    finally:
        face.pixels, face.W, face.H = previous


def paired(left, right):
    return [a + [BLACK] * 8 + b for a, b in zip(left, right)]


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    screens = {
        "menu": render_menu(), "alarm": render_alarm(),
        "timer-editor": render_timer_editor(), "timer-running": render_timer_running(),
        "stopwatch": render_stopwatch(), "alert": render_alert(),
    }
    for name, canvas in screens.items():
        save_preview(here / f"ClockTools-{name}-preview.png", canvas)
    pair = paired(screens["menu"], screens["alarm"])
    save_preview(here / "ClockTools-preview.png", pair)
    bottom = paired(screens["timer-running"], screens["stopwatch"])
    gallery = pair + [[BLACK] * len(pair[0]) for _ in range(8)] + bottom
    save_preview(here / "ClockTools-screens-preview.png", gallery)
    print(f"wrote clock-tools previews to {here}")
