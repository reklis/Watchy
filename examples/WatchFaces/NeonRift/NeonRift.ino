#include "Watchy_NeonRift.h"
#include "settings.h"

WatchyNeonRift watchy(settings, POSTAL_CODE, COUNTRY_CODE, NTP_SYNC_INTERVAL);

void setup() {
    watchy.init();
}

void loop() {}
