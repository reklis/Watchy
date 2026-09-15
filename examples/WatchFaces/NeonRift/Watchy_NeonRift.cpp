#include "Watchy_NeonRift.h"
#include <Preferences.h>
#include <cmath>

namespace {
RTC_DATA_ATTR uint32_t cachedLocationKey = 0;
RTC_DATA_ATTR bool locationCacheChecked = false;
RTC_DATA_ATTR float cachedLatitude = 0.0f;
RTC_DATA_ATTR float cachedLongitude = 0.0f;
RTC_DATA_ATTR bool cachedLocationValid = false;
RTC_DATA_ATTR uint32_t attemptedLocationKey = 0;
RTC_DATA_ATTR int8_t cachedTemperature = 0;
RTC_DATA_ATTR uint8_t cachedWeatherCode = 0;
RTC_DATA_ATTR bool cachedWeatherValid = false;
RTC_DATA_ATTR int32_t cachedUtcOffset = 0;
RTC_DATA_ATTR bool cachedUtcOffsetValid = false;
RTC_DATA_ATTR int64_t lastWeatherAttempt = -1;
RTC_DATA_ATTR int64_t lastNtpSync = -1;
RTC_DATA_ATTR bool timezoneSyncPending = false;
}

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

WatchyNeonRift::WatchyNeonRift(const watchySettings &settings,
                               const char *postalCode,
                               const char *countryCode,
                               uint16_t ntpSyncInterval)
    : Watchy(settings), postalCode(postalCode), countryCode(countryCode),
      ntpSyncInterval(ntpSyncInterval) {}

void WatchyNeonRift::drawWatchFace() {
    refreshWeather();
    display.fillScreen(GxEPD_BLACK);
    drawFrame();
    drawLunarCycle();
    drawClock();
    drawDataPanel();
    drawStatusBar();
}

void WatchyNeonRift::refreshWeather() {
    const uint32_t locationKey = getLocationKey();
    const bool locationReady = loadCachedLocation(locationKey);
    if (cachedUtcOffsetValid) setTimezoneOffset(cachedUtcOffset);
    const int64_t minuteStamp = getMinuteStamp();
    const uint16_t updateInterval = settings.weatherUpdateInterval > 5
        ? settings.weatherUpdateInterval : 180;
    const bool clockMovedBack = lastWeatherAttempt > minuteStamp;
    const bool postalCodeChanged = attemptedLocationKey != locationKey;
    const bool updateDue = postalCodeChanged || lastWeatherAttempt < 0 ||
        clockMovedBack || minuteStamp - lastWeatherAttempt >= updateInterval;

    if (!updateDue) return;
    attemptedLocationKey = locationKey;
    lastWeatherAttempt = minuteStamp; // Also rate-limits retries after a failure.
    cachedWeatherValid = false;
    if (!connectWiFi(10000)) return;

    bool haveLocation = locationReady;
    if (!haveLocation) {
        haveLocation = geocodePostalCode(locationKey);
    }

    int32_t utcOffset = cachedUtcOffsetValid ? cachedUtcOffset : settings.gmtOffset;
    bool timezoneChanged = false;
    if (haveLocation && fetchOpenMeteoWeather(utcOffset)) {
        timezoneChanged = !cachedUtcOffsetValid || cachedUtcOffset != utcOffset;
        if (timezoneChanged) timezoneSyncPending = true;
        cachedUtcOffset = utcOffset;
        cachedUtcOffsetValid = true;
        setTimezoneOffset(utcOffset);
        if (timezoneChanged) {
            Preferences preferences;
            if (preferences.begin("neonrift", false)) {
                preferences.putLong("utcOffset", utcOffset);
                preferences.putBool("tzValid", true);
                preferences.end();
            }
        }
    }

    // Weather can refresh without an NTP exchange. Sync daily, immediately
    // after a timezone/DST change, or after a clock reset.
    const uint16_t syncInterval = ntpSyncInterval > 0 ? ntpSyncInterval : 1440;
    const bool ntpDue = timezoneSyncPending || lastNtpSync < 0 ||
        lastNtpSync > minuteStamp || minuteStamp - lastNtpSync >= syncInterval;
    if (ntpDue && syncNTP(utcOffset)) {
        RTC.read(currentTime);
        lastNtpSync = getMinuteStamp();
        lastWeatherAttempt = lastNtpSync;
        timezoneSyncPending = false;
    }

    WiFi.mode(WIFI_OFF);
    btStop();
}

bool WatchyNeonRift::loadCachedLocation(uint32_t locationKey) {
    if (locationCacheChecked && cachedLocationKey == locationKey) return cachedLocationValid;

    locationCacheChecked = true;
    cachedLocationKey = locationKey;
    cachedLocationValid = false;
    cachedWeatherValid = false;
    cachedUtcOffsetValid = false;
    Preferences preferences;
    if (!preferences.begin("neonrift", true)) return false;
    const bool matches = preferences.getBool("locValid", false) &&
        preferences.getUInt("locKey", 0) == locationKey;
    if (matches) {
        const float latitude = preferences.getFloat("latitude", NAN);
        const float longitude = preferences.getFloat("longitude", NAN);
        if (std::isfinite(latitude) && std::isfinite(longitude) &&
            latitude >= -90.0f && latitude <= 90.0f &&
            longitude >= -180.0f && longitude <= 180.0f) {
            cachedLatitude = latitude;
            cachedLongitude = longitude;
            cachedLocationKey = locationKey;
            cachedLocationValid = true;
            cachedUtcOffset = preferences.getLong("utcOffset", settings.gmtOffset);
            cachedUtcOffsetValid = preferences.getBool("tzValid", false) &&
                cachedUtcOffset >= -50400 && cachedUtcOffset <= 50400;
        }
    }
    preferences.end();
    return cachedLocationValid;
}

bool WatchyNeonRift::geocodePostalCode(uint32_t locationKey) {
    if (postalCode == nullptr || postalCode[0] == '\0' ||
        countryCode == nullptr || countryCode[0] == '\0') return false;

    String url = "https://api.zippopotam.us/";
    url += urlEncode(countryCode);
    url += "/";
    url += urlEncode(postalCode);

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(3000);
    http.begin(url.c_str());
    const int responseCode = http.GET();
    if (responseCode != 200) {
        http.end();
        return false;
    }
    const String payload = http.getString();
    http.end();

    JSONVar response = JSON.parse(payload);
    if (JSON.typeof(response) == "undefined" ||
        JSON.typeof(response["places"]) != "array" ||
        response["places"].length() == 0) return false;
    JSONVar place = response["places"][0];
    if (JSON.typeof(place["latitude"]) != "string" ||
        JSON.typeof(place["longitude"]) != "string") return false;

    const char *latitude = static_cast<const char *>(place["latitude"]);
    const char *longitude = static_cast<const char *>(place["longitude"]);
    if (latitude == nullptr || longitude == nullptr) return false;
    char *latitudeEnd;
    char *longitudeEnd;
    const double parsedLatitude = strtod(latitude, &latitudeEnd);
    const double parsedLongitude = strtod(longitude, &longitudeEnd);
    if (latitudeEnd == latitude || *latitudeEnd != '\0' ||
        longitudeEnd == longitude || *longitudeEnd != '\0' ||
        !std::isfinite(parsedLatitude) || !std::isfinite(parsedLongitude) ||
        parsedLatitude < -90.0 || parsedLatitude > 90.0 ||
        parsedLongitude < -180.0 || parsedLongitude > 180.0) return false;

    cachedLatitude = parsedLatitude;
    cachedLongitude = parsedLongitude;
    cachedLocationKey = locationKey;
    cachedLocationValid = true;

    // NVS survives power loss and firmware uploads. Mark the cache valid only
    // after every value is written, so an interrupted write cannot be reused.
    Preferences preferences;
    if (preferences.begin("neonrift", false)) {
        preferences.putBool("locValid", false);
        preferences.putBool("tzValid", false);
        const bool stored = preferences.putFloat("latitude", cachedLatitude) == sizeof(float) &&
            preferences.putFloat("longitude", cachedLongitude) == sizeof(float) &&
            preferences.putUInt("locKey", locationKey) == sizeof(uint32_t);
        if (stored) preferences.putBool("locValid", true);
        preferences.end();
    }
    return true;
}

bool WatchyNeonRift::fetchOpenMeteoWeather(int32_t &utcOffset) {
    String url = "https://api.open-meteo.com/v1/forecast?latitude=";
    url += String(cachedLatitude, 6);
    url += "&longitude=";
    url += String(cachedLongitude, 6);
    url += "&current=temperature_2m,weather_code&temperature_unit=";
    url += settings.weatherUnit == "imperial" ? "fahrenheit" : "celsius";
    url += "&timezone=auto&forecast_days=1";

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(3000);
    http.begin(url.c_str());
    const int responseCode = http.GET();
    if (responseCode != 200) {
        http.end();
        return false;
    }
    const String payload = http.getString();
    http.end();

    JSONVar response = JSON.parse(payload);
    if (JSON.typeof(response) == "undefined" ||
        JSON.typeof(response["current"]) != "object" ||
        JSON.typeof(response["current"]["temperature_2m"]) != "number" ||
        JSON.typeof(response["current"]["weather_code"]) != "number") return false;

    const double responseTemperature = static_cast<double>(response["current"]["temperature_2m"]);
    const int responseWeatherCode = static_cast<int>(response["current"]["weather_code"]);
    if (!std::isfinite(responseTemperature) || responseWeatherCode < 0 || responseWeatherCode > 99) {
        return false;
    }
    if (JSON.typeof(response["utc_offset_seconds"]) == "number") {
        const int32_t responseOffset = static_cast<int32_t>(
            static_cast<int>(response["utc_offset_seconds"]));
        if (responseOffset >= -50400 && responseOffset <= 50400) utcOffset = responseOffset;
    }
    const int16_t temperature = round(responseTemperature);
    cachedTemperature = constrain(temperature, -127, 127);
    cachedWeatherCode = static_cast<uint8_t>(responseWeatherCode);
    cachedWeatherValid = true;
    return true;
}

uint32_t WatchyNeonRift::getLocationKey() const {
    uint32_t hash = 2166136261UL;
    const char *parts[] = {countryCode, "|", postalCode};
    for (const char *part : parts) {
        if (part == nullptr) continue;
        while (*part) {
            hash ^= static_cast<uint8_t>(*part++);
            hash *= 16777619UL;
        }
    }
    return hash;
}

int64_t WatchyNeonRift::getMinuteStamp() const {
    const int32_t month = currentTime.Month;
    const int32_t a = (14 - month) / 12;
    const int32_t year = tmYearToCalendar(currentTime.Year) + 4800 - a;
    const int32_t adjustedMonth = month + 12 * a - 3;
    const int32_t day = currentTime.Day + (153 * adjustedMonth + 2) / 5 +
        365 * year + year / 4 - year / 100 + year / 400 - 32045;
    return static_cast<int64_t>(day) * 1440 + currentTime.Hour * 60 + currentTime.Minute;
}

String WatchyNeonRift::urlEncode(const char *value) const {
    String encoded;
    while (value != nullptr && *value) {
        const uint8_t character = static_cast<uint8_t>(*value++);
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' ||
            character == '_' || character == '.' || character == '~') {
            encoded += static_cast<char>(character);
        } else {
            char escaped[4];
            snprintf(escaped, sizeof(escaped), "%%%02X", character);
            encoded += escaped;
        }
    }
    return encoded;
}

const char *WatchyNeonRift::getWeatherLabel(uint8_t code) const {
    if (code == 0) return "CLEAR";
    if (code <= 3) return "CLOUD";
    if (code == 45 || code == 48) return "MIST";
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "RAIN";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "SNOW";
    if (code >= 95) return "STORM";
    return "MIXED";
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

    int16_t temperatureValue = cachedTemperature;
    const bool isMetric = settings.weatherUnit != "imperial";
    const char *condition = getWeatherLabel(cachedWeatherCode);
    if (!cachedWeatherValid) {
        temperatureValue = sensor.readTemperature();
        if (!isMetric) temperatureValue = temperatureValue * 9 / 5 + 32;
        condition = "LOCAL";
    }
    char temperature[6];
    snprintf(temperature, sizeof(temperature), "%d%c", temperatureValue,
             isMetric ? 'C' : 'F');
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
