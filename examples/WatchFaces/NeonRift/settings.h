#ifndef SETTINGS_H
#define SETTINGS_H

#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 0 // Change to your UTC offset in seconds.
#define CITY_ID "5128581" // New York City; use your OpenWeatherMap city ID.
#define OPENWEATHERMAP_APIKEY "" // Empty uses Watchy's onboard temperature sensor.
#define OPENWEATHERMAP_URL "http://api.openweathermap.org/data/2.5/weather?id={cityID}&lang={lang}&units={units}&appid={apiKey}"

watchySettings settings{
    .cityID = CITY_ID,
    .lat = "",
    .lon = "",
    .weatherAPIKey = OPENWEATHERMAP_APIKEY,
    .weatherURL = OPENWEATHERMAP_URL,
    .weatherUnit = "metric",
    .weatherLang = "en",
    .weatherUpdateInterval = 30,
    .ntpServer = NTP_SERVER,
    .gmtOffset = GMT_OFFSET_SEC,
    .vibrateOClock = true,
};

#endif
