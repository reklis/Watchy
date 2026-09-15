# NeonRift

A native-resolution monochrome Watchy face with a restrained cyberpunk
instrument-panel style. Readability and watch data take priority over decoration.
It shows:

- 24-hour time
- day, date, and year
- lunar phase and illumination percentage
- step count
- local temperature, or live weather when an OpenWeatherMap key is configured
- battery percentage and segmented gauge
- Wi-Fi, Bluetooth, and USB power status (USB power is available on Watchy v3)

The artwork is drawn with display primitives and two tiny bitmap fonts, so the
face has no external asset or font dependency.

![NeonRift preview](NeonRift-preview.png)

## Install

1. Open `NeonRift.ino` in Arduino IDE.
2. Set `GMT_OFFSET_SEC` in `settings.h` for your timezone.
3. Optionally set `CITY_ID` and `OPENWEATHERMAP_APIKEY` for live weather. With
   no API key, the face shows the onboard temperature as `LOCAL`.
4. Select your Watchy board and upload.

## Preview

`NeonRift-preview.png` is an 800x800 nearest-neighbor preview of the native
200x200 e-paper output. Regenerate both the high-resolution preview and the
catalog screenshot with:

```sh
python3 render_preview.py
```

The preview uses a representative time, date, step count, network state, and
battery charge. The hardware face replaces those values with live readings.
