import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")
CFG = (ROOT / "include" / "config.h").read_text(encoding="utf-8")

class Zigbee3ProfileTests(unittest.TestCase):
    def test_version_and_endpoint_order(self):
        self.assertIn('FW_VERSION[] = "0.7.3-alpha.1"', CFG)
        self.assertIn('BATTERY_SENSOR_ENDPOINT = 1', CFG)
        self.assertIn('LIGHT_ENDPOINT = 2', CFG)

    def test_no_vendor_or_device_type_spoofing(self):
        for token in ('LUMI', 'YNDX-', 'lumi.', 'ZigbeeContactSwitch', 'ZigbeeTempSensor'):
            self.assertNotIn(token, MAIN)

    def test_battery_is_standard_meter_interface(self):
        self.assertIn('class BatteryTelemetryEndpoint : public ZigbeeElectricalMeasurement', MAIN)
        self.assertIn('ESP_ZB_HA_METER_INTERFACE_DEVICE_ID', MAIN)
        self.assertIn('ESP_ZB_AF_HA_PROFILE_ID', MAIN)

if __name__ == "__main__":
    unittest.main()


