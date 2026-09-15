# NeonRift

A native-resolution monochrome Watchy face with a restrained cyberpunk
instrument-panel style. Readability and watch data take priority over decoration.
It shows:

- 24-hour time
- day, date, and year
- lunar phase and illumination percentage
- step count
- local temperature fallback and key-free live weather for a configured postal code
- battery percentage and segmented gauge
- Wi-Fi, Bluetooth, and USB power status (USB power is available on Watchy v3)

The artwork is drawn with display primitives and two tiny bitmap fonts, so the
face has no external asset or font dependency.

![NeonRift preview](NeonRift-preview.png)

## Install

1. Open `NeonRift.ino` in Arduino IDE.
2. Set `POSTAL_CODE` and the two-letter `COUNTRY_CODE` in `settings.h`.
3. Choose `metric` or `imperial` with `WEATHER_UNIT`.
4. Select your Watchy board and upload.

## Compile with Devbox

The repository includes a reproducible Arduino CLI environment using the same
ESP32 core version as CI. Install the board core and libraries once, then build
the revision matching your hardware:

```sh
devbox run setup
devbox run build-v10 # Watchy PCB v1.0
devbox run build-v20 # Watchy PCB v2.0
devbox run build-v30 # Watchy PCB v3.0 / current SQFMI-WATCHY-10
```

Firmware binaries are written under `.devbox/build/<revision>/`.

NeonRift resolves the postal code through [Zippopotam.us](https://www.zippopotam.us/),
then gets current weather and the local UTC offset from
[Open-Meteo](https://open-meteo.com/). Neither service requires an API key.
The resolved coordinates are stored in ESP32 NVS and reused through power
cycles; a successful lookup runs again only when the postal or country setting
changes. Failed requests are retried no more than once per weather interval.
Weather refreshes every three hours by default. NTP synchronizes daily, or
immediately when the resolved timezone offset changes for travel or daylight
saving time. Both intervals are configurable in `settings.h`.
If networking fails, the display falls back to Watchy's onboard temperature
and the configured or most recently resolved UTC offset.

## Preview

`NeonRift-preview.png` is an 800x800 nearest-neighbor preview of the native
200x200 e-paper output. Regenerate both the high-resolution preview and the
catalog screenshot with:

```sh
python3 render_preview.py
```

The preview uses a representative time, date, step count, network state, and
battery charge. The hardware face replaces those values with live readings.
