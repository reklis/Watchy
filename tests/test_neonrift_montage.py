"""Check the shareable montage's screen coverage, pixels, and output size."""
import importlib.util
import pathlib
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
PREVIEW_DIR = ROOT / "examples/WatchFaces/NeonRift"


class MontageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("neonrift_montage", PREVIEW_DIR / "render_montage.py")
        cls.montage = importlib.util.module_from_spec(spec)
        with mock.patch.object(sys, "path", [str(PREVIEW_DIR), *sys.path]):
            spec.loader.exec_module(cls.montage)

    def test_all_seven_screens_are_preserved_pixel_for_pixel(self):
        montage = self.montage
        self.assertEqual(len(montage.SCREENS), 7)
        original = montage.face.pixels
        original_pixels = [row[:] for row in original]
        canvas = montage.render_montage()
        self.assertIs(montage.face.pixels, original)
        self.assertEqual(original, original_pixels)
        self.assertEqual((len(canvas[0]), len(canvas)), (960, 540))
        for index, (label, renderer) in enumerate(montage.SCREENS):
            with self.subTest(screen=label):
                x, y = montage.COL_X[index % 4], montage.ROW_Y[index // 4]
                expected = renderer()
                actual = [row[x:x + 200] for row in canvas[y:y + 200]]
                self.assertEqual(actual, expected)

    def test_png_is_full_hd(self):
        montage = self.montage
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "montage.png"
            montage.save_montage(path, montage.render_montage())
            png = path.read_bytes()
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack(">II", png[16:24]), (1920, 1080))


if __name__ == "__main__":
    unittest.main()
