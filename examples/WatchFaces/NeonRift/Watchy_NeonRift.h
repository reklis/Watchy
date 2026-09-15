#ifndef WATCHY_NEON_RIFT_H
#define WATCHY_NEON_RIFT_H

#include <Watchy.h>

class WatchyNeonRift : public Watchy {
public:
    WatchyNeonRift(const watchySettings &settings, const char *postalCode,
                   const char *countryCode, uint16_t ntpSyncInterval);
    void drawWatchFace() override;

private:
    const char *postalCode;
    const char *countryCode;
    uint16_t ntpSyncInterval;

    void refreshWeather();
    bool loadCachedLocation(uint32_t locationKey);
    bool geocodePostalCode(uint32_t locationKey);
    bool fetchOpenMeteoWeather(int32_t &utcOffset);
    uint32_t getLocationKey() const;
    int64_t getMinuteStamp() const;
    String urlEncode(const char *value) const;
    const char *getWeatherLabel(uint8_t code) const;
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
