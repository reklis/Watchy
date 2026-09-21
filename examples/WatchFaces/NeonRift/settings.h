#ifndef SETTINGS_H
#define SETTINGS_H

#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 0 // Used until the first successful weather update.
#define POSTAL_CODE "10001"
#define COUNTRY_CODE "US" // Two-letter ISO country code.
#define WEATHER_UNIT "metric" // "metric" for Celsius, "imperial" for Fahrenheit.
#define ENABLE_AUTO_WEATHER 0 // Keep weather requests off for maximum stability.
#define ENABLE_AUTO_NTP 1 // Sync time without fetching weather.
#define WEATHER_UPDATE_INTERVAL 180 // Used only when ENABLE_AUTO_WEATHER is enabled.
#define NTP_SYNC_INTERVAL 1440 // Sync once per day; failed attempts retry hourly.

watchySettings settings{
    .cityID = "",
    .lat = "",
    .lon = "",
    .weatherAPIKey = "",
    .weatherURL = "",
    .weatherUnit = WEATHER_UNIT,
    .weatherLang = "en",
    .weatherUpdateInterval = WEATHER_UPDATE_INTERVAL,
    .ntpServer = NTP_SERVER,
    .gmtOffset = GMT_OFFSET_SEC,
    .vibrateOClock = true,
};

#endif
