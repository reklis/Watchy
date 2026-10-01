#ifndef WATCHY_CLOCK_TOOLS_UI_H
#define WATCHY_CLOCK_TOOLS_UI_H

#include <cstdint>
#include <cstdio>
#include <cstring>

// Rendering only: no RTC, buttons, radio, persistence, or display refreshes.
// The canvas needs the standard Adafruit GFX drawing primitives. Keeping this
// independent of Arduino also lets host tests compare pixels with the previews.
namespace watchy_ui {
namespace detail {
static const uint8_t SMALL_FONT[36][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
    {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1},
    {7,5,7,5,7}, {7,5,7,1,7},
    {2,5,7,5,5}, {6,5,6,5,6}, {3,4,4,4,3}, {6,5,5,5,6},
    {7,4,6,4,7}, {7,4,6,4,4}, {3,4,5,5,3}, {5,5,7,5,5},
    {7,2,2,2,7}, {1,1,1,5,2}, {5,5,6,5,5}, {4,4,4,4,7},
    {5,7,7,5,5}, {5,3,7,6,5}, {2,5,5,5,2}, {6,5,6,4,4},
    {2,5,5,3,1}, {6,5,6,5,5}, {3,4,2,1,6}, {7,2,2,2,2},
    {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5},
    {5,5,2,2,2}, {7,1,2,4,7}
};
static const uint8_t TIME_FONT[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {30,1,1,14,16,16,31}, {30,1,1,14,1,1,30},
    {18,18,18,31,2,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};
} // namespace detail

struct ClockToolsMenuSnapshot {
  uint32_t alarmMinutes;
  bool alarmEnabled;
  uint32_t timerMinutes;
  bool timerRunning;
  uint32_t stopwatchMinutes;
  bool stopwatchRunning;
};

template <typename Canvas> class ClockToolsUI {
public:
  explicit ClockToolsUI(Canvas &canvas, uint16_t white = 0xFFFF, uint16_t black = 0)
      : canvas(canvas), white(white), black(black) {}

  void menu(const ClockToolsMenuSnapshot &state, uint8_t selected) {
    selected %= 3;
    chrome("CLOCK TOOLS", "TOOLS/01");
    const char *labels[] = {"ALARM", "TIMER", "STOPWATCH"};
    const char *statuses[] = {state.alarmEnabled ? "ON" : "OFF",
        state.timerRunning ? "RUNNING" : "READY",
        state.stopwatchRunning ? "RUNNING" : "PAUSED"};
    const uint32_t minutes[] = {state.alarmMinutes, state.timerMinutes, state.stopwatchMinutes};
    for (uint8_t index = 0; index < 3; ++index) {
      const int16_t y = 45 + index * 42;
      const uint16_t ink = index == selected ? black : white;
      panel(10, y, 180, 36, index == selected);
      icon(index, 25, y + 18, ink);
      canvas.drawLine(39, y + 6, 39, y + 29, ink);
      text(labels[index], 45, y + 5, 1, ink);
      char value[16];
      formatTime(minutes[index], value);
      // Long-running stopwatches can exceed 99 hours. Shrink, never truncate
      // the value or let it collide with the status badge.
      const int16_t available = 182 - (textWidth(statuses[index]) + 8) + 1 - 45 - 5;
      const uint8_t scale = timeWidth(value, 2) <= available ? 2 : 1;
      timeText(value, 45, y + 15, scale, ink);
      badge(statuses[index], 182, y + 12, ink);
    }
    char indexLabel[8];
    std::snprintf(indexLabel, sizeof(indexLabel), "0%u / 03", static_cast<unsigned>(selected + 1));
    footer("UP/DOWN MOVE", "MENU OPEN", "BACK EXIT", indexLabel);
  }

  void alarm(uint8_t hour, uint8_t minute, bool enabled, uint8_t field) {
    chrome("ALARM", "EDIT/01");
    timePanel(static_cast<uint32_t>(hour) * 60 + minute, "SET WAKE TIME", "24H",
              field < 2 ? field : -1);
    const uint16_t ink = field == 2 ? black : white;
    panel(10, 144, 180, 22, field == 2);
    text("ENABLED", 18, 153, 1, ink);
    canvas.drawRect(141, 151, 7, 7, ink);
    if (enabled) {
      canvas.drawLine(142, 154, 144, 156, ink);
      canvas.drawLine(144, 156, 147, 152, ink);
    }
    badge(enabled ? "ON" : "OFF", 182, 148, ink);
    const char *fields[] = {"EDIT HOUR", "EDIT MIN", "EDIT STATE"};
    footer("UP/DOWN ADJUST", "BACK CANCEL", field == 2 ? "MENU SAVE" : "MENU NEXT",
           fields[field % 3]);
  }

  void timerEditor(uint8_t hours, uint8_t minutes, uint8_t field) {
    chrome("TIMER", "EDIT/02");
    timePanel(static_cast<uint32_t>(hours) * 60 + minutes, "SET DURATION", "HH:MM", field);
    statePanel("COUNTDOWN", "READY");
    footer("UP/DOWN ADJUST", "BACK CANCEL", field == 0 ? "MENU NEXT" : "MENU START",
           field == 0 ? "EDIT HOUR" : "EDIT MIN");
  }

  void timerRunning(uint32_t remainingMinutes) {
    chrome("TIMER", "TOOLS/02");
    timePanel(remainingMinutes, "TIME REMAINING", "HH:MM");
    statePanel("COUNTDOWN", "RUNNING");
    footer("MENU CANCEL", "BACK TOOLS", "HH:MM", "ROUNDS UP");
  }

  void stopwatch(uint32_t elapsedMinutes, bool running) {
    chrome("STOPWATCH", "TOOLS/03");
    timePanel(elapsedMinutes, "ELAPSED TIME", "HH:MM");
    statePanel("STATE", running ? "RUNNING" : "PAUSED");
    footer(running ? "MENU PAUSE" : "MENU START", "DOWN RESET", "BACK TOOLS", "HH:MM");
  }

  void alert(bool alarmTriggered, bool timerTriggered) {
    chrome("ALERT", "EVENT/01");
    panel(10, 46, 180, 90, true);
    icon(alarmTriggered ? 0 : 1, 100, 67, black);
    if (alarmTriggered) centeredText("ALARM", 92, 2, black);
    if (timerTriggered) centeredText("TIMER DONE", alarmTriggered ? 115 : 92, 2, black);
    statePanel("ACK REQUIRED", "ALERT");
    footer("ANY BUTTON DISMISS", "", "RETURN TO FACE", "ACK");
  }

private:
  Canvas &canvas;
  const uint16_t white, black;
  static constexpr int16_t RIGHT = 189;

  static void formatTime(uint32_t minutes, char (&value)[16]) {
    std::snprintf(value, sizeof(value), "%02lu:%02lu",
                  static_cast<unsigned long>(minutes / 60),
                  static_cast<unsigned long>(minutes % 60));
  }

  static int16_t textWidth(const char *value, uint8_t scale = 1) {
    const int16_t length = std::strlen(value);
    return length ? (length * (scale > 1 ? 5 : 4) - (scale > 1 ? 2 : 1)) * scale : 0;
  }

  static uint8_t glyphRow(char ch, uint8_t row) {
    if (ch >= '0' && ch <= '9') return detail::SMALL_FONT[ch - '0'][row];
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    if (ch >= 'A' && ch <= 'Z') return detail::SMALL_FONT[10 + ch - 'A'][row];
    static const uint8_t slash[] = {1,1,2,4,4}, colon[] = {0,2,0,2,0};
    static const uint8_t dash[] = {0,0,7,0,0}, period[] = {0,0,0,0,2};
    switch (ch) {
      case '/': return slash[row];
      case ':': return colon[row];
      case '-': return dash[row];
      case '.': return period[row];
      default: return 0;
    }
  }

  void text(const char *value, int16_t x, int16_t y, uint8_t scale, uint16_t ink) {
    while (*value) {
      for (uint8_t row = 0; row < 5; ++row) {
        const uint8_t bits = glyphRow(*value, row);
        for (uint8_t col = 0; col < 3; ++col) {
          if (bits & (1 << (2 - col)))
            canvas.fillRect(x + col * scale, y + row * scale, scale, scale, ink);
        }
      }
      x += (scale > 1 ? 5 : 4) * scale;
      ++value;
    }
  }

  void centeredText(const char *value, int16_t y, uint8_t scale, uint16_t ink) {
    text(value, (200 - textWidth(value, scale)) / 2, y, scale, ink);
  }

  static int16_t timeWidth(const char *value, uint8_t scale) {
    int16_t width = 0;
    while (*value) width += *value++ == ':' ? 3 : 6;
    return width ? (width - 1) * scale : 0;
  }

  void timeText(const char *value, int16_t x, int16_t y, uint8_t scale, uint16_t ink) {
    while (*value) {
      const char ch = *value++;
      if (ch == ':') {
        canvas.fillRect(x, y + 2 * scale, scale, scale, ink);
        canvas.fillRect(x, y + 4 * scale, scale, scale, ink);
        x += 3 * scale;
      } else {
        if (ch >= '0' && ch <= '9') {
          for (uint8_t row = 0; row < 7; ++row) {
            for (uint8_t col = 0; col < 5; ++col) {
              if (detail::TIME_FONT[ch - '0'][row] & (1 << (4 - col)))
                canvas.fillRect(x + col * scale, y + row * scale, scale, scale, ink);
            }
          }
        }
        x += 6 * scale;
      }
    }
  }

  void panel(int16_t x, int16_t y, int16_t width, int16_t height, bool selected = false) {
    if (selected) canvas.fillRect(x, y, width, height, white);
    else canvas.drawRect(x, y, width, height, white);
    const int16_t right = x + width - 1, bottom = y + height - 1;
    canvas.fillRect(x, y, 3, 3, black);
    canvas.fillRect(right - 2, y, 3, 3, black);
    canvas.fillRect(x, bottom - 2, 3, 3, black);
    canvas.fillRect(right - 2, bottom - 2, 3, 3, black);
    canvas.drawLine(x, y + 3, x + 3, y, white);
    canvas.drawLine(right - 3, y, right, y + 3, white);
    canvas.drawLine(x, bottom - 3, x + 3, bottom, white);
    canvas.drawLine(right - 3, bottom, right, bottom - 3, white);
  }

  void badge(const char *value, int16_t right, int16_t y, uint16_t ink) {
    const int16_t width = textWidth(value) + 8;
    canvas.drawRect(right - width + 1, y, width, 13, ink);
    text(value, right - width + 5, y + 4, 1, ink);
  }

  void chrome(const char *title, const char *section) {
    canvas.fillScreen(black);
    canvas.drawLine(6,2,57,2,white); canvas.drawLine(6,2,6,21,white);
    canvas.drawLine(143,2,193,2,white); canvas.drawLine(193,2,193,21,white);
    canvas.drawLine(6,178,6,197,white); canvas.drawLine(6,197,57,197,white);
    canvas.drawLine(193,178,193,197,white); canvas.drawLine(143,197,193,197,white);
    canvas.fillRect(61,1,3,3,white); canvas.fillRect(137,1,3,3,white);
    text("NEON//RIFT",10,8,1,white);
    text(section,RIGHT-textWidth(section),8,1,white);
    text(title,10,23,2,white);
    canvas.drawLine(160,27,174,27,white); canvas.drawLine(174,27,179,22,white);
    canvas.drawLine(179,22,189,22,white); canvas.drawRect(187,20,3,3,white);
    canvas.drawLine(10,38,RIGHT,38,white); canvas.fillRect(9,37,3,3,white);
  }

  void footer(const char *leftTop, const char *rightTop,
              const char *leftBottom, const char *rightBottom) {
    canvas.drawLine(10,176,RIGHT,176,white);
    text(leftTop,10,182,1,white);
    text(rightTop,RIGHT-textWidth(rightTop),182,1,white);
    text(leftBottom,10,191,1,white);
    text(rightBottom,RIGHT-textWidth(rightBottom),191,1,white);
  }

  void icon(uint8_t kind, int16_t cx, int16_t cy, uint16_t ink) {
    if (kind == 1) {
      canvas.drawLine(cx-6,cy-9,cx+6,cy-9,ink);
      canvas.drawLine(cx-6,cy+9,cx+6,cy+9,ink);
      canvas.drawLine(cx-5,cy-7,cx+5,cy+7,ink);
      canvas.drawLine(cx+5,cy-7,cx-5,cy+7,ink);
      canvas.drawLine(cx-4,cy-5,cx+4,cy-5,ink);
      canvas.drawLine(cx-4,cy+7,cx+4,cy+7,ink);
    } else {
      canvas.drawCircle(cx,cy,7,ink);
      canvas.drawLine(cx,cy,cx,cy-4,ink); canvas.drawLine(cx,cy,cx+3,cy,ink);
      if (kind == 0) {
        canvas.drawLine(cx-8,cy-7,cx-4,cy-10,ink);
        canvas.drawLine(cx+4,cy-10,cx+8,cy-7,ink);
        canvas.drawLine(cx-5,cy+6,cx-7,cy+9,ink);
        canvas.drawLine(cx+5,cy+6,cx+7,cy+9,ink);
      } else {
        canvas.drawLine(cx-2,cy-10,cx+2,cy-10,ink);
        canvas.drawLine(cx,cy-10,cx,cy-7,ink);
        canvas.drawLine(cx+5,cy-7,cx+8,cy-10,ink);
      }
    }
  }

  void timePanel(uint32_t minutes, const char *label, const char *unit, int8_t field = -1) {
    panel(10,46,180,90);
    text(label,18,54,1,white);
    badge(unit,181,50,white);
    char value[16];
    formatTime(minutes, value);
    uint8_t scale = 5;
    while (scale > 1 && timeWidth(value, scale) > 164) --scale;
    const int16_t start = (200 - timeWidth(value, scale)) / 2;
    timeText(value,start,75 + (35 - 7 * scale) / 2,scale,white);
    const int16_t hours = std::strchr(value, ':') - value;
    const int16_t hourWidth = (6 * hours - 1) * scale;
    const int16_t minuteStart = start + (6 * hours + 3) * scale;
    const int16_t minuteWidth = 11 * scale;
    if (field == 0) canvas.fillRect(start,113,hourWidth,2,white);
    if (field == 1) canvas.fillRect(minuteStart,113,minuteWidth,2,white);
    text("HOUR",start + (hourWidth-textWidth("HOUR"))/2,123,1,white);
    text("MINUTE",minuteStart + (minuteWidth-textWidth("MINUTE"))/2,123,1,white);
  }

  void statePanel(const char *label, const char *state) {
    panel(10,144,180,22);
    text(label,18,153,1,white);
    badge(state,182,148,white);
  }
};
} // namespace watchy_ui
#endif
