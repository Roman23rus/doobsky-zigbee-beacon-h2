import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BATTERY_PATH = ROOT / "src" / "battery_telemetry.cpp"


def source() -> str:
    return BATTERY_PATH.read_text(encoding="utf-8-sig") if BATTERY_PATH.exists() else ""


class BatteryUnknownTests(unittest.TestCase):
    def test_invalid_measurements_use_zcl_unknown_sentinels(self):
        text = source()
        self.assertIn('BATTERY_ZCL_UNKNOWN = 0xFF', text)
        self.assertIn('DC_VOLTAGE_UNKNOWN = INT16_MIN', text)
        self.assertIn('batteryValid_ ? static_cast<int16_t>(batteryMv_) : DC_VOLTAGE_UNKNOWN', text)

    def test_power_config_percentage_is_half_percent_units(self):
        self.assertIn('static_cast<uint8_t>(batteryPercent_ * 2U)', source())

    def test_startup_sampling_is_incremental(self):
        text = source()
        self.assertIn('sampler_.active', text)
        self.assertNotIn('delayMicroseconds', text)
        self.assertNotIn('for (uint8_t i = 0; i < BATTERY_ADC_SAMPLES', text)


if __name__ == "__main__":
    unittest.main()
