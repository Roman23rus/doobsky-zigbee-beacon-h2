import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")

class LightProfileTests(unittest.TestCase):
    def test_light_is_separate_dimmable_endpoint(self):
        self.assertIn('ZigbeeDimmableLight zbLight(LIGHT_ENDPOINT)', MAIN)
        self.assertIn('setManufacturerAndModel(MANUFACTURER, MODEL)', MAIN)
        self.assertIn('Zigbee.addEndpoint(&zbBatterySensor)', MAIN)
        self.assertIn('Zigbee.addEndpoint(&zbLight)', MAIN)

    def test_battery_cluster_not_attached_to_light(self):
        self.assertNotIn('zbLight.setPowerSource', MAIN)
        self.assertNotIn('zbLight.setBatteryPercentage', MAIN)
        self.assertNotIn('zbLight.setBatteryVoltage', MAIN)

if __name__ == "__main__":
    unittest.main()

