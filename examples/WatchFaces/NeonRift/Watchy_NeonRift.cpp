#include "Watchy_NeonRift.h"

// Compact 3x5 font: 0-9 followed by A-Z. It keeps every label crisp at the
// Watchy's native 200x200 resolution and avoids bundling another font.
static const uint8_t TINY_FONT[36][5] PROGMEM = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
    {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
    {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
    {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6},
    {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4}, {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5},
    {7, 2, 2, 2, 7}, {1, 1, 1, 5, 2}, {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7},
    {5, 7, 7, 5, 5}, {5, 7, 7, 7, 5}, {2, 5, 5, 5, 2}, {6, 5, 6, 4, 4},
    {2, 5, 5, 3, 1}, {6, 5, 6, 5, 5}, {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2},
    {5, 5, 5, 5, 7}, {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5},
    {5, 5, 2, 2, 2}, {7, 1, 2, 4, 7}
};

static const uint8_t LARGE_DIGITS[10][7] PROGMEM = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {30, 1, 1, 14, 16, 16, 31},   {30, 1, 1, 14, 1, 1, 30},
    {18, 18, 18, 31, 2, 2, 2},    {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}
};

void WatchyNeonRift::drawWatchFace() {
    display.fillScreen(GxEPD_BLACK);
    drawFrame();
    drawLunarCycle();
    drawClock();
    drawDataPanel();
    drawStatusBar();
}

void WatchyNeonRift::drawFrame() {
    display.drawLine(6, 2, 57, 2, GxEPD_WHITE);
    display.drawLine(6, 2, 6, 21, GxEPD_WHITE);
    display.drawLine(193, 2, 143, 2, GxEPD_WHITE);
    display.drawLine(193, 2, 193, 21, GxEPD_WHITE);
    display.drawLine(6, 178, 6, 197, GxEPD_WHITE);
    display.drawLine(6, 197, 57, 197, GxEPD_WHITE);
    display.drawLine(193, 178, 193, 197, GxEPD_WHITE);
    display.drawLine(193, 197, 143, 197, GxEPD_WHITE);

    const int16_t nodes[][2] = {
        {62, 2}, {138, 2}, {6, 26}, {193, 26}
    };
    for (const auto &node : nodes) {
        display.fillRect(node[0] - 1, node[1] - 1, 3, 3, GxEPD_WHITE);
    }

    drawTinyText("NEON//RIFT", 10, 8);
    drawTinyText("SYSTEM 24H", 10, 17);
    display.drawLine(75, 26, 127, 26, GxEPD_WHITE);
    display.fillRect(73, 24, 3, 3, GxEPD_WHITE);
    display.fillRect(127, 24, 3, 3, GxEPD_WHITE);
}

void WatchyNeonRift::drawLunarCycle() {
    // Calculate phase from the known new moon on 2000-01-06 18:14 UTC.
    const int32_t month = currentTime.Month;
    const int32_t a = (14 - month) / 12;
    const int32_t year = tmYearToCalendar(currentTime.Year) + 4800 - a;
    const int32_t adjustedMonth = month + 12 * a - 3;
    const int32_t julianDayNumber = currentTime.Day + (153 * adjustedMonth + 2) / 5 +
        365 * year + year / 4 - year / 100 + year / 400 - 32045;
    const double julianDate = julianDayNumber - 0.5 + currentTime.Hour / 24.0 +
        currentTime.Minute / 1440.0;
    const double lunarMonth = 29.530588853;
    double age = fmod(julianDate - 2451550.25972, lunarMonth);
    if (age < 0) age += lunarMonth;
    const double phase = age / lunarMonth;
    const double angle = 6.28318530718 * phase;
    const uint8_t illumination = round((1.0 - cos(angle)) * 50.0);

    static const char *const PHASE_NAMES[] = {
        "NEW", "WAX CRES", "FIRST Q", "WAX GIB",
        "FULL", "WAN GIB", "LAST Q", "WAN CRES"
    };
    const uint8_t phaseIndex = static_cast<uint8_t>(phase * 8.0 + 0.5) % 8;

    const int16_t cx = 143;
    const int16_t cy = 15;
    const int16_t radius = 8;
    const double terminatorFactor = cos(angle);
    for (int16_t y = -radius; y <= radius; ++y) {
        const int16_t halfWidth = sqrt(radius * radius - y * y);
        const double terminator = terminatorFactor * halfWidth;
        for (int16_t x = -halfWidth; x <= halfWidth; ++x) {
            const bool lit = phase < 0.5 ? x >= terminator : x <= -terminator;
            if (lit) display.drawPixel(cx + x, cy + y, GxEPD_WHITE);
        }
    }
    display.drawCircle(cx, cy, radius, GxEPD_WHITE);

    char light[9];
    snprintf(light, sizeof(light), "%d%% LIT", illumination);
    drawTinyText(light, 158, 8);
    drawTinyText(PHASE_NAMES[phaseIndex], 158, 17);
}

void WatchyNeonRift::drawClock() {
    display.drawRect(7, 34, 186, 64, GxEPD_WHITE);

    drawDigit(currentTime.Hour / 10, 26, 45);
    drawDigit(currentTime.Hour % 10, 60, 45);
    display.fillRect(95, 56, 5, 5, GxEPD_WHITE);
    display.fillRect(95, 74, 5, 5, GxEPD_WHITE);
    drawDigit(currentTime.Minute / 10, 109, 45);
    drawDigit(currentTime.Minute % 10, 143, 45);

}

void WatchyNeonRift::drawDataPanel() {
    static const char *const DAYS[] = {"???", "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *const MONTHS[] = {"???", "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const uint8_t weekday = currentTime.Wday <= 7 ? currentTime.Wday : 0;
    const uint8_t month = currentTime.Month <= 12 ? currentTime.Month : 0;

    display.drawLine(7, 103, 193, 103, GxEPD_WHITE);
    display.drawLine(73, 106, 73, 152, GxEPD_WHITE);
    display.drawLine(134, 106, 134, 152, GxEPD_WHITE);
    drawTinyText("DATE", 10, 109);
    drawTinyText(DAYS[weekday], 10, 119, 2);
    char date[12];
    snprintf(date, sizeof(date), "%s %02d %04d", MONTHS[month], currentTime.Day,
             tmYearToCalendar(currentTime.Year));
    drawTinyText(date, 10, 141);

    if (currentTime.Hour == 0 && currentTime.Minute == 0) {
        sensor.resetStepCounter();
    }
    char steps[6];
    snprintf(steps, sizeof(steps), "%05lu", static_cast<unsigned long>(sensor.getCounter() % 100000));
    drawTinyText("STEPS", 80, 109);
    drawTinyText(steps, 80, 121, 2);
    drawTinyText("DAILY", 80, 143);

    weatherData weather{};
    if (settings.weatherAPIKey.length() > 0) {
        weather = getWeatherData();
    } else {
        int16_t localTemperature = sensor.readTemperature();
        weather.isMetric = settings.weatherUnit == "metric";
        if (!weather.isMetric) {
            localTemperature = localTemperature * 9 / 5 + 32;
        }
        weather.temperature = localTemperature;
        weather.external = false;
    }
    char temperature[6];
    snprintf(temperature, sizeof(temperature), "%d%c", weather.temperature,
             weather.isMetric ? 'C' : 'F');
    const int16_t code = weather.weatherConditionCode;
    const char *condition = "LOCAL";
    if (weather.external) {
        if (code >= 200 && code < 300) condition = "STORM";
        else if (code >= 300 && code < 600) condition = "RAIN";
        else if (code >= 600 && code < 700) condition = "SNOW";
        else if (code >= 700 && code < 800) condition = "MIST";
        else if (code == 800) condition = "CLEAR";
        else if (code > 800) condition = "CLOUD";
    }
    drawTinyText("WEATHER", 141, 109);
    drawTinyText(temperature, 141, 121, 2);
    drawTinyText(condition, 141, 143);
}

void WatchyNeonRift::drawStatusBar() {
    const float voltage = getBatteryVoltage();
    int16_t percent = static_cast<int16_t>((voltage - 3.30f) * 111.0f);
    percent = constrain(percent, 0, 100);

    display.drawLine(7, 157, 193, 157, GxEPD_WHITE);
    drawTinyText("BATTERY", 10, 163);
    char battery[5];
    snprintf(battery, sizeof(battery), "%d%%", percent);
    drawTinyText(battery, 158, 174, 2);

    display.drawRect(10, 174, 140, 10, GxEPD_WHITE);
    const int16_t fillWidth = 136 * percent / 100;
    display.fillRect(12, 176, fillWidth, 6, GxEPD_WHITE);
    const int16_t segments[] = {39, 67, 95, 123};
    for (const int16_t x : segments) {
        display.drawLine(x, 175, x, 182, GxEPD_BLACK);
    }

    const char *wifiStatus = WIFI_CONFIGURED ? "WIFI ON" : "WIFI OFF";
    const char *bleStatus = BLE_CONFIGURED ? "BLE ON" : "BLE OFF";
#ifdef ARDUINO_ESP32S3_DEV
    const char *usbStatus = USB_PLUGGED_IN ? "USB ON" : "USB OFF";
#else
    const char *usbStatus = "USB N/A";
#endif
    drawTinyText(wifiStatus, 34 - static_cast<int16_t>(strlen(wifiStatus) * 2), 190);
    drawTinyText(bleStatus, 100 - static_cast<int16_t>(strlen(bleStatus) * 2), 190);
    drawTinyText(usbStatus, 166 - static_cast<int16_t>(strlen(usbStatus) * 2), 190);
}

void WatchyNeonRift::drawDigit(uint8_t value, int16_t x, int16_t y) {
    value %= 10;
    for (uint8_t row = 0; row < 7; ++row) {
        const uint8_t bits = pgm_read_byte(&LARGE_DIGITS[value][row]);
        for (uint8_t column = 0; column < 5; ++column) {
            if (bits & (1 << (4 - column))) {
                display.fillRect(x + column * 6, y + row * 6, 5, 5, GxEPD_WHITE);
            }
        }
    }
}

void WatchyNeonRift::drawTinyText(const char *value, int16_t x, int16_t y, uint8_t scale) {
    while (*value) {
        for (uint8_t row = 0; row < 5; ++row) {
            const uint8_t bits = glyphRow(*value, row);
            for (uint8_t column = 0; column < 3; ++column) {
                if (bits & (1 << (2 - column))) {
                    display.fillRect(x + column * scale, y + row * scale, scale, scale, GxEPD_WHITE);
                }
            }
        }
        x += 4 * scale;
        ++value;
    }
}

uint8_t WatchyNeonRift::glyphRow(char value, uint8_t row) {
    if (value >= '0' && value <= '9') {
        return pgm_read_byte(&TINY_FONT[value - '0'][row]);
    }
    if (value >= 'a' && value <= 'z') {
        value -= 'a' - 'A';
    }
    if (value >= 'A' && value <= 'Z') {
        return pgm_read_byte(&TINY_FONT[10 + value - 'A'][row]);
    }

    static const uint8_t slash[] = {1, 1, 2, 4, 4};
    static const uint8_t colon[] = {0, 2, 0, 2, 0};
    static const uint8_t percent[] = {5, 1, 2, 4, 5};
    static const uint8_t dash[] = {0, 0, 7, 0, 0};
    static const uint8_t period[] = {0, 0, 0, 0, 2};
    switch (value) {
        case '/': return slash[row];
        case ':': return colon[row];
        case '%': return percent[row];
        case '-': return dash[row];
        case '.': return period[row];
        default: return 0;
    }
}
