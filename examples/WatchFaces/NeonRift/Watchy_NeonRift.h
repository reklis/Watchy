#ifndef WATCHY_NEON_RIFT_H
#define WATCHY_NEON_RIFT_H

#include <Watchy.h>

class WatchyNeonRift : public Watchy {
    using Watchy::Watchy;

public:
    void drawWatchFace() override;

private:
    void drawFrame();
    void drawLunarCycle();
    void drawClock();
    void drawDataPanel();
    void drawStatusBar();
    void drawDigit(uint8_t value, int16_t x, int16_t y);
    void drawTinyText(const char *value, int16_t x, int16_t y, uint8_t scale = 1);
    uint8_t glyphRow(char value, uint8_t row);
};

#endif
