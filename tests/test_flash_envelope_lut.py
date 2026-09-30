import math
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CFG = (ROOT / "include" / "config.h").read_text(encoding="utf-8-sig")
LUT_PATH = ROOT / "include" / "flash_envelope_lut.h"


class FlashEnvelopeLutTests(unittest.TestCase):
    def test_lut_matches_one_ms_sin_squared_samples(self):
        self.assertTrue(LUT_PATH.exists())
        text = LUT_PATH.read_text(encoding="utf-8-sig")
        values = [int(x) for x in re.findall(r"\b\d+\b", text.split("FLASH_ENVELOPE_LUT", 1)[1])]
        flash_us = 353_571
        pwm_max = 1023
        expected_count = (flash_us + 999) // 1000
        self.assertGreaterEqual(len(values), expected_count)
        values = values[:expected_count]
        expected = [round((math.sin(math.pi * (i * 1000) / flash_us) ** 2) * pwm_max)
                    for i in range(expected_count)]
        self.assertEqual(values, expected)


if __name__ == "__main__":
    unittest.main()
