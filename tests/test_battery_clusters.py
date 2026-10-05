import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BATTERY_PATH = ROOT / "src" / "battery_telemetry.cpp"
BATTERY_H_PATH = ROOT / "include" / "battery_telemetry.h"


def battery_source() -> str:
    parts = []
    for path in (BATTERY_H_PATH, BATTERY_PATH):
        if path.exists():
            parts.append(path.read_text(encoding="utf-8-sig"))
    return "\n".join(parts)


class BatteryClusterTests(unittest.TestCase):
    def test_battery_module_exists(self):
        self.assertTrue(BATTERY_PATH.exists())
        self.assertTrue(BATTERY_H_PATH.exists())

    def test_uses_dc_voltage_not_rms_or_analog(self):
        source = battery_source()
        self.assertIn('addDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE)', source)
        compact = ''.join(source.split())
        self.assertIn('setDCMultiplierDivisor(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE,1,1000)', compact)
        self.assertIn('setDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE', compact)
        self.assertIn('reportDC(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE)', compact)
        self.assertNotIn('RMSVOLTAGE', source)
        self.assertNotIn('ZigbeeAnalog', source)

    def test_power_configuration_supports_unknown_values(self):
        source = battery_source()
        self.assertIn('BATTERY_ZCL_UNKNOWN = 0xFF', source)
        self.assertIn('addBatteryPowerConfiguration', source)
        self.assertIn('setBatteryTelemetryRaw', source)
        self.assertIn('ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID', source)
        self.assertIn('ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID', source)

    def test_report_cycle_requires_all_standard_reports_to_succeed(self):
        source = battery_source()
        self.assertIn('reportBatteryPercentage()', source)
        self.assertIn('reportBatteryVoltage()', source)
        self.assertIn('reportDC(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE)', source)
        self.assertIn('if (!(pctOk && batteryVoltOk && dcVoltOk)) return;', source)
        self.assertNotIn('setDCReporting(', source)

    def test_battery_voltage_has_explicit_power_config_report(self):
        source = battery_source()
        self.assertIn('ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID', source)
        self.assertIn('bool BatteryTelemetryEndpoint::reportBatteryVoltage()', source)
        self.assertIn('reportClusterAttribute(&report)', source)


if __name__ == "__main__":
    unittest.main()
