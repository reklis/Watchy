"""Clock-tools rendering regressions; requires g++, not hardware or image libraries."""
import importlib.util
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
PREVIEW_DIR = ROOT / "examples/WatchFaces/NeonRift"


class ClockToolsPreviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location(
            "clock_tools_preview", PREVIEW_DIR / "render_clock_tools_preview.py"
        )
        cls.preview = importlib.util.module_from_spec(spec)
        with mock.patch.object(sys, "path", [str(PREVIEW_DIR), *sys.path]):
            spec.loader.exec_module(cls.preview)
        directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(directory.cleanup)
        cls.renderer = pathlib.Path(directory.name) / "clock-tools"
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "src"), str(ROOT / "tests/clock_tools_canvas.cpp"),
                        "-o", str(cls.renderer)],
                       check=True, capture_output=True, text=True)

    def assert_firmware_matches(self, args, canvas):
        actual = subprocess.run([str(self.renderer), *map(str, args)],
                                check=True, capture_output=True).stdout
        expected = bytes(pixel for row in canvas for pixel in row)
        self.assertEqual(len(actual), 40000)
        if actual != expected:
            first = next(index for index, pair in enumerate(zip(actual, expected))
                         if pair[0] != pair[1])
            count = sum(a != b for a, b in zip(actual, expected))
            self.fail(f"{args}: {count} differing pixels; first at ({first % 200},{first // 200})")

    def test_firmware_menu_matches_preview_for_each_state(self):
        for selected in range(3):
            for enabled in (False, True):
                for running in (False, True):
                    with self.subTest(selected=selected, enabled=enabled, running=running):
                        self.assert_firmware_matches(
                            ["menu", selected, int(enabled), int(running), 75, int(running)],
                            self.preview.render_menu(selected, alarm_enabled=enabled,
                                                     timer_running=running, stopwatch_minutes=75,
                                                     stopwatch_running=running))

    def test_firmware_alarm_matches_preview_for_all_fields(self):
        for field in range(3):
            for enabled in (False, True):
                for hour, minute in ((0, 0), (7, 30), (23, 59)):
                    with self.subTest(field=field, enabled=enabled, hour=hour, minute=minute):
                        self.assert_firmware_matches(
                            ["alarm", field, int(enabled), hour * 60 + minute],
                            self.preview.render_alarm(hour, minute, enabled, field))

    def test_firmware_timer_stopwatch_and_alert_screens(self):
        for field in range(2):
            for minutes in (0, 25, 1439):
                self.assert_firmware_matches(
                    ["timer-editor", field, minutes],
                    self.preview.render_timer_editor(minutes // 60, minutes % 60, field))
        for minutes in (0, 1, 25, 1439):
            self.assert_firmware_matches(["timer-running", minutes],
                                         self.preview.render_timer_running(minutes))
        for running in (False, True):
            for minutes in (0, 75, 6000, 71582788, 4294967295):
                self.assert_firmware_matches(["stopwatch", minutes, int(running)],
                                             self.preview.render_stopwatch(minutes, running))
                self.assert_firmware_matches(["menu", 2, 1, 0, minutes, int(running)],
                                             self.preview.render_menu(2, stopwatch_minutes=minutes,
                                                                      stopwatch_running=running))
        for alarm, timer in ((True, False), (False, True), (True, True)):
            self.assert_firmware_matches(["alert", int(alarm), int(timer)],
                                         self.preview.render_alert(alarm, timer))

    def assert_native_canvas(self, canvas):
        self.assertEqual(len(canvas), 200)
        self.assertTrue(all(len(row) == 200 for row in canvas))
        self.assertEqual({pixel for row in canvas for pixel in row}, {0, 1})

    def test_selected_card_inverts_and_others_stay_dark(self):
        for selected in range(3):
            with self.subTest(selected=selected):
                canvas = self.preview.render_menu(selected)
                self.assert_native_canvas(canvas)
                for card in range(3):
                    self.assertEqual(canvas[50 + card * 42][15], int(card == selected))

    def test_only_selected_hour_is_underlined(self):
        canvas = self.preview.render_alarm()
        self.assert_native_canvas(canvas)
        start = (200 - self.preview.time_width("07:30", 5)) // 2
        self.assertTrue(all(canvas[113][x] == 1 for x in range(start, start + 55)))
        self.assertTrue(all(canvas[113][x] == 0 for x in range(start + 75, start + 130)))

    def test_export_dimensions_and_renderer_state_restoration(self):
        menu, alarm = self.preview.render_menu(), self.preview.render_alarm()
        pair = [left + [0] * 8 + right for left, right in zip(menu, alarm)]
        original = self.preview.face.pixels
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "pair.png"
            self.preview.save_preview(path, pair)
            data = path.read_bytes()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack(">II", data[16:24]), (1632, 800))
        self.assertIs(self.preview.face.pixels, original)
        self.assertEqual((self.preview.face.W, self.preview.face.H), (200, 200))


if __name__ == "__main__":
    unittest.main()
