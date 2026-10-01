// Host canvas for the actual firmware renderer. Every drawing operation must
// remain inside the 200x200 screen; emit one byte per monochrome pixel for tests.
#include "ClockToolsUI.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>

struct Canvas {
  uint8_t pixels[200][200] = {};
  void drawPixel(int x, int y, uint16_t color) {
    assert(x >= 0 && x < 200 && y >= 0 && y < 200);
    pixels[y][x] = color != 0;
  }
  void fillScreen(uint16_t color) { std::memset(pixels, color != 0, sizeof(pixels)); }
  void fillRect(int x, int y, int width, int height, uint16_t color) {
    for (int yy = y; yy < y + height; ++yy)
      for (int xx = x; xx < x + width; ++xx) drawPixel(xx, yy, color);
  }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    const bool steep = std::abs(y1-y0) > std::abs(x1-x0);
    if (steep) { std::swap(x0,y0); std::swap(x1,y1); }
    if (x0 > x1) { std::swap(x0,x1); std::swap(y0,y1); }
    const int dx = x1-x0, dy = std::abs(y1-y0), step = y0 < y1 ? 1 : -1;
    int error = dx/2;
    for (; x0 <= x1; ++x0) {
      if (steep) drawPixel(y0,x0,color); else drawPixel(x0,y0,color);
      error -= dy;
      if (error < 0) { y0 += step; error += dx; }
    }
  }
  void drawRect(int x, int y, int width, int height, uint16_t color) {
    drawLine(x,y,x+width-1,y,color);
    drawLine(x,y+height-1,x+width-1,y+height-1,color);
    drawLine(x,y,x,y+height-1,color);
    drawLine(x+width-1,y,x+width-1,y+height-1,color);
  }
  void drawCircle(int cx, int cy, int radius, uint16_t color) {
    int error = 1-radius, dx = 1, dy = -2*radius, x = 0, y = radius;
    drawPixel(cx,cy+radius,color); drawPixel(cx,cy-radius,color);
    drawPixel(cx+radius,cy,color); drawPixel(cx-radius,cy,color);
    while (x < y) {
      if (error >= 0) { --y; dy += 2; error += dy; }
      ++x; dx += 2; error += dx;
      drawPixel(cx+x,cy+y,color); drawPixel(cx-x,cy+y,color);
      drawPixel(cx+x,cy-y,color); drawPixel(cx-x,cy-y,color);
      drawPixel(cx+y,cy+x,color); drawPixel(cx-y,cy+x,color);
      drawPixel(cx+y,cy-x,color); drawPixel(cx-y,cy-x,color);
    }
  }
};

int main(int argc, char **argv) {
  assert(argc >= 2);
  auto number = [&](int index, uint32_t fallback) -> uint32_t {
    return index < argc ? static_cast<uint32_t>(std::strtoul(argv[index], nullptr, 10)) : fallback;
  };
  Canvas canvas;
  watchy_ui::ClockToolsUI<Canvas> ui(canvas);
  const char *screen = argv[1];
  if (std::strcmp(screen,"menu") == 0) {
    const watchy_ui::ClockToolsMenuSnapshot state = {
        450, number(3,1) != 0, 25, number(4,0) != 0,
        number(5,0), number(6,0) != 0};
    ui.menu(state, number(2,0));
  } else if (std::strcmp(screen,"alarm") == 0) {
    const uint32_t minutes = number(4,450);
    ui.alarm(minutes/60, minutes%60, number(3,1) != 0, number(2,0));
  } else if (std::strcmp(screen,"timer-editor") == 0) {
    const uint32_t minutes = number(3,25);
    ui.timerEditor(minutes/60, minutes%60, number(2,0));
  } else if (std::strcmp(screen,"timer-running") == 0) {
    ui.timerRunning(number(2,25));
  } else if (std::strcmp(screen,"stopwatch") == 0) {
    ui.stopwatch(number(2,75), number(3,1) != 0);
  } else if (std::strcmp(screen,"alert") == 0) {
    ui.alert(number(2,1) != 0, number(3,0) != 0);
  } else {
    return 2;
  }
  const std::size_t written = std::fwrite(canvas.pixels, 1, sizeof(canvas.pixels), stdout);
  return written == sizeof(canvas.pixels) ? 0 : 1;
}
