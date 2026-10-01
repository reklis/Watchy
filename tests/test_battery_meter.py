"""Host regressions for the actual battery-reading and NeonRift rendering code.

Run: python3 -m unittest discover -s tests -v
Requires g++; no Arduino libraries or attached Watchy are needed.
"""
import importlib.util
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function_source(path, signature):
    text = (ROOT / path).read_text()
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


STUBS = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#define BATT_ADC_PIN 9
#define CHRG_STATUS_PIN 10
#define INPUT_PULLUP 1
#define LOW 0
#define ADC_11db 3
#define ADC_VOLTAGE_DIVIDER ((360.0f+100.0f)/360.0f)
#define GxEPD_WHITE 1
#define GxEPD_BLACK 0
#define DS3231 1
constexpr int16_t RIGHT_TEXT_EDGE = 189;
const uint8_t TINY_FONT[36][5] = {};
uint8_t pgm_read_byte(const uint8_t *p) { return *p; }
bool USB_PLUGGED_IN = false, WIFI_CONFIGURED = false, BLE_CONFIGURED = false;
int chargePin = 1, railSamples = 0, rawCalls = 0, mvCalls = 0, attenuation = -1;
uint32_t mockMillivolts = 3050;
void pinMode(int, int) {}
int digitalRead(int) { return chargePin; }
void analogSetPinAttenuation(int, int value) { attenuation = value; }
uint32_t analogReadMilliVolts(int) {
    // A bad first conversion must not contaminate the averaged voltage.
    return ++mvCalls == 1 ? 999 : mockMillivolts;
}
uint16_t analogReadRaw(int) { return rawCalls++ < railSamples ? 4095 : 3800; }
void delayMicroseconds(int) {}
struct { int rtcType = DS3231; } RTC;
class Watchy {
public:
    float getBatteryVoltage(bool *adcSaturated = nullptr);
};
struct Display {
    int fillWidth = -1;
    void drawLine(int, int, int, int, int) {}
    void drawRect(int, int, int, int, int) {}
    void fillRect(int, int, int width, int, int) { fillWidth = width; }
};
class WatchyNeonRift : public Watchy {
public:
    Display display;
    float mockVoltage = 3.895f;
    bool mockSaturated = false;
    std::vector<std::pair<std::string, int>> labels;
    float getBatteryVoltage(bool *saturated) {
        *saturated = mockSaturated;
        return mockVoltage;
    }
    void drawTinyText(const char *text, int, int y, int = 1) {
        labels.push_back({text, y});
    }
    void drawStatusBar();
    uint8_t glyphRow(char value, uint8_t row);
    bool hasText(const char *text, int y) {
        for (const auto &label : labels)
            if (label.first == text && label.second == y) return true;
        return false;
    }
};
'''

CHECKS = r'''
void resetADC(int rails) {
    railSamples = rails;
    rawCalls = mvCalls = 0;
    attenuation = -1;
}
int main() {
    assert(std::strcmp(batteryLabel(3.0f, false), "LOW") == 0);
    assert(std::strcmp(batteryLabel(3.599f, false), "LOW") == 0);
    assert(std::strcmp(batteryLabel(3.60f, false), "NOMINAL") == 0);
    assert(std::strcmp(batteryLabel(3.895f, false), "NOMINAL") == 0);
    assert(std::strcmp(batteryLabel(3.90f, false), "HIGH") == 0);
    assert(std::strcmp(batteryLabel(4.20f, false), "HIGH") == 0);
    assert(std::strcmp(batteryLabel(3.895f, true), "HIGH") == 0);
    for (float invalid : {NAN, INFINITY, 0.0f, -1.0f}) {
        assert(std::strcmp(batteryLabel(invalid, false), "UNKNOWN") == 0);
        assert(std::strcmp(batteryLabel(invalid, true), "UNKNOWN") == 0);
    }

    Watchy reader;
#ifdef ARDUINO_ESP32S3_DEV
    for (int rails : {0, 1, 7, 8, 16}) {
        resetADC(rails);
        bool saturated = true;
        float voltage = reader.getBatteryVoltage(&saturated);
        assert(std::fabs(voltage - 3.05f * ADC_VOLTAGE_DIVIDER) < 0.0001f);
        assert(attenuation == ADC_11db);
        assert(mvCalls == 17 && rawCalls == 16);
        assert(saturated == (rails >= 8));
    }
    resetADC(16);
    reader.getBatteryVoltage(); // Existing API callers need no extra raw samples.
    assert(rawCalls == 0);
#else
    resetADC(0);
    mvCalls = 1;
    bool saturated = true;
    assert(std::fabs(reader.getBatteryVoltage(&saturated) - 6.10f) < 0.0001f);
    assert(!saturated && rawCalls == 0);
    RTC.rtcType = 2;
    assert(std::fabs(reader.getBatteryVoltage() - 6.10f) < 0.0001f);
#endif

    // USB and charging must not change the voltage band or replace its label.
    for (bool usb : {false, true}) {
        for (int status : {0, 1}) {
            USB_PLUGGED_IN = usb;
            chargePin = status;
            WatchyNeonRift face;
            face.drawStatusBar();
            assert(face.hasText("NOMINAL", 174));
            assert(face.display.fillWidth == -1); // No progress bar.
#ifdef ARDUINO_ESP32S3_DEV
            assert(face.hasText("CHG", 174) == (usb && status == LOW));
#else
            assert(!face.hasText("CHG", 174));
#endif
            face.labels.clear();
            face.mockSaturated = true;
            face.drawStatusBar();
            assert(face.hasText("HIGH", 174));
            assert(face.display.fillWidth == -1);
            face.labels.clear();
            face.mockSaturated = false;
            face.mockVoltage = 3.50f;
            face.drawStatusBar();
            assert(face.hasText("LOW", 174));
        }
    }
    WatchyNeonRift full;
    full.mockVoltage = 4.20f;
    full.drawStatusBar();
    assert(full.hasText("HIGH", 174));
    assert(full.display.fillWidth == -1);
    const uint8_t mirroredN[] = {5, 3, 7, 6, 5};
    for (uint8_t row = 0; row < 5; ++row) {
        assert(full.glyphRow('N', row) == mirroredN[row]);
        assert(full.glyphRow('n', row) == mirroredN[row]);
    }
    // Preserve support for the custom font's '+' marker.
    const uint8_t plus[] = {0, 2, 7, 2, 0};
    for (uint8_t row = 0; row < 5; ++row)
        assert(full.glyphRow('+', row) == plus[row]);
}
'''


class BatteryMeterTests(unittest.TestCase):
    def test_preview_glyph_and_data_alignment(self):
        path = ROOT / "examples/WatchFaces/NeonRift/render_preview.py"
        spec = importlib.util.spec_from_file_location("neonrift_preview", path)
        preview = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(preview)
        self.assertEqual(preview.FONT["N"], (5, 3, 7, 6, 5))
        self.assertEqual({preview.glyph_width(ch) for ch in preview.FONT}, {3})
        self.assertTrue(all(bits <= 7 for rows in preview.FONT.values() for bits in rows))
        self.assertEqual(preview.text_width("NOMINAL", 2), 66)
        with mock.patch.object(preview, "text") as text:
            preview.data_panel()
        positions = {call.args[0]: call.args[2] for call in text.call_args_list}
        self.assertEqual(positions["MAR 14 2025"], positions["DAILY"])
        self.assertEqual(positions["DAILY"], positions["CLEAR"])
        self.assertEqual(positions["FRI"], positions["08421"])
        self.assertEqual(positions["FRI"], positions["21C"])
        with mock.patch.object(preview, "text") as text:
            preview.status_bar(3.78, charging=True, usb=True)
        charge = next(call.args for call in text.call_args_list if call.args[0] == "CHG")
        self.assertEqual(charge[1:], (163, 174, 2))
        charge_right = charge[1] + preview.text_width("CHG", 2)
        self.assertEqual(charge_right, preview.RIGHT_TEXT_EDGE)
        for day in (1, 7, 14, 21):
            with mock.patch.object(preview, "text") as text:
                preview.lunar_cycle(2025, 3, day, 23, 47)
            light = next(call.args for call in text.call_args_list if call.args[0].endswith("LIT"))
            self.assertEqual(light[1] + preview.text_width(light[0]), charge_right)

    def test_enlarged_text_tracking_and_panel_bounds(self):
        source = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#define GxEPD_WHITE 1
struct Rect { int x, width; };
struct Display {
    std::vector<Rect> rects;
    void fillRect(int x, int, int width, int, int) { rects.push_back({x, width}); }
};
class FontProbe {
public:
    Display display;
    uint8_t glyphRow(char, uint8_t) { return 7; }
    void drawTinyText(const char *value, int16_t x, int16_t y, uint8_t scale);
};
''' + function_source(
            "examples/WatchFaces/NeonRift/Watchy_NeonRift.cpp", "uint8_t tinyGlyphWidth(",
        ) + function_source(
            "examples/WatchFaces/NeonRift/Watchy_NeonRift.cpp", "int16_t tinyTextWidth(",
        ) + function_source(
            "examples/WatchFaces/NeonRift/Watchy_NeonRift.cpp",
            "void WatchyNeonRift::drawTinyText(",
        ).replace("WatchyNeonRift::", "FontProbe::") + r'''
int main() {
    for (char ch = ' '; ch <= 'z'; ++ch) assert(tinyGlyphWidth(ch) == 3);
    assert(tinyTextWidth("") == 0);
    assert(tinyTextWidth("NOMINAL", 2) == 66);
    FontProbe font;
    font.drawTinyText("HH", 10, 20, 1);
    assert(font.display.rects[15].x == 14); // Small labels unchanged.
    font.display.rects.clear();
    font.drawTinyText("HH", 10, 20, 2);
    assert(font.display.rects[15].x == 20); // Four-pixel gap, formerly two.
    for (const char *level : {"LOW", "NOMINAL", "HIGH", "UNKNOWN"}) {
        font.display.rects.clear();
        font.drawTinyText(level, 10, 174, 2);
        for (const auto &rect : font.display.rects)
            assert(rect.x + rect.width < 163); // Clear of CHG.
    }
    font.display.rects.clear();
    font.drawTinyText("CHG", 163, 174, 2);
    for (const auto &rect : font.display.rects)
        assert(rect.x + rect.width <= 189); // Same right edge as the moon readout.
    font.display.rects.clear();
    font.drawTinyText("99999", 80, 121, 2);
    for (const auto &rect : font.display.rects)
        assert(rect.x + rect.width < 134); // Fits the steps panel.
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = pathlib.Path(directory) / "font.cpp"
            binary = pathlib.Path(directory) / "font"
            cpp.write_text(source)
            subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True)

    def test_reading_and_rendering_on_both_board_families(self):
        face_path = "examples/WatchFaces/NeonRift/Watchy_NeonRift.cpp"
        font = function_source(face_path, "static const uint8_t TINY_FONT")
        font = font.replace(" PROGMEM", "") + ";"
        source = "\n".join([
            STUBS.replace("const uint8_t TINY_FONT[36][5] = {};", font),
            function_source(face_path, "uint8_t tinyGlyphWidth("),
            function_source(face_path, "int16_t tinyTextWidth("),
            function_source("src/Watchy.cpp", "float Watchy::getBatteryVoltage("),
            function_source("examples/WatchFaces/NeonRift/Watchy_NeonRift.cpp",
                            "const char *batteryLabel("),
            function_source("examples/WatchFaces/NeonRift/Watchy_NeonRift.cpp",
                            "void WatchyNeonRift::drawStatusBar()"),
            function_source("examples/WatchFaces/NeonRift/Watchy_NeonRift.cpp",
                            "uint8_t WatchyNeonRift::glyphRow("),
            CHECKS,
        ])
        with tempfile.TemporaryDirectory() as directory:
            cpp = pathlib.Path(directory) / "battery.cpp"
            cpp.write_text(source)
            for board in ("v3", "legacy"):
                with self.subTest(board=board):
                    binary = pathlib.Path(directory) / board
                    flags = ["-DARDUINO_ESP32S3_DEV"] if board == "v3" else []
                    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra",
                                    "-Werror", *flags, str(cpp), "-o", str(binary)],
                                   check=True, capture_output=True, text=True)
                    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
