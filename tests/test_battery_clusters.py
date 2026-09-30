import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")

class BatteryClusterTests(unittest.TestCase):
    def test_uses_dc_voltage_not_rms_or_analog(self):
        self.assertIn('addDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE)', MAIN)
        self.assertIn('setDCMultiplierDivisor(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, 1, 1000)', MAIN)
        self.assertIn('setDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE', MAIN)
        self.assertIn('reportDC(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE)', MAIN)
        self.assertNotIn('RMSVOLTAGE', MAIN)
        self.assertNotIn('ZigbeeAnalog', MAIN)

    def test_power_configuration_supports_unknown_values(self):
        self.assertIn('BATTERY_ZCL_UNKNOWN = 0xFF', MAIN)
        self.assertIn('addBatteryPowerConfiguration', MAIN)
        self.assertIn('setBatteryTelemetryRaw', MAIN)
        self.assertIn('ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID', MAIN)
        self.assertIn('ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID', MAIN)

    def test_report_cycle_requires_both_standard_reports_to_succeed(self):
        self.assertIn('if (!(pctOk && voltOk)) return;', MAIN)
        self.assertNotIn('if (!(pctOk || voltOk)) return;', MAIN)

if __name__ == "__main__":
    unittest.main()
