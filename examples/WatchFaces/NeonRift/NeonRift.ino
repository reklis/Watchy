#include "Watchy_NeonRift.h"
#include "settings.h"

WatchyNeonRift watchy(settings, POSTAL_CODE, COUNTRY_CODE, NTP_SYNC_INTERVAL,
                       ENABLE_AUTO_WEATHER, ENABLE_AUTO_NTP);

#ifdef WATCHY_DIAGNOSTIC_AWAKE
extern RTC_DATA_ATTR bool displayFullInit;
#endif

void setup() {
#ifdef WATCHY_DIAGNOSTIC_AWAKE
    // Uploads and resets preserve RTC memory, including the display driver's
    // partial-init flag. Force a complete controller reset for this test.
    displayFullInit = true;
#endif
    watchy.init();
}

void loop() {
#ifdef WATCHY_DIAGNOSTIC_AWAKE
    static uint32_t frame = 0;
    const uint16_t background = frame % 2 == 0 ? GxEPD_WHITE : GxEPD_BLACK;
    const uint16_t foreground = frame % 2 == 0 ? GxEPD_BLACK : GxEPD_WHITE;
    Watchy::display.setFullWindow();
    Watchy::display.fillScreen(background);
    Watchy::display.setTextColor(foreground);
    Watchy::display.setFont(nullptr);
    Watchy::display.setTextSize(4);
    Watchy::display.setCursor(20, 105);
    Watchy::display.print("TEST ");
    Watchy::display.print(frame++);
    Watchy::display.display(false);
    delay(5000);
#endif
}
