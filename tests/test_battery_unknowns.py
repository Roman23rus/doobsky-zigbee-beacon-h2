import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")

class BatteryUnknownTests(unittest.TestCase):
    def test_invalid_measurements_use_zcl_unknown_sentinels(self):
        self.assertIn('BATTERY_ZCL_UNKNOWN = 0xFF', MAIN)
        self.assertIn('DC_VOLTAGE_UNKNOWN = INT16_MIN', MAIN)
        self.assertIn('batteryValid ? static_cast<int16_t>(batteryMv) : DC_VOLTAGE_UNKNOWN', MAIN)

    def test_power_config_percentage_is_half_percent_units(self):
        self.assertIn('static_cast<uint8_t>(batteryPercent * 2U)', MAIN)

if __name__ == "__main__":
    unittest.main()

