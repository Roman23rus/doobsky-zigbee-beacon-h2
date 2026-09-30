import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")
ZIGBEE_PATH = ROOT / "src" / "zigbee_light.cpp"
BEACON_PATH = ROOT / "src" / "beacon_engine.cpp"


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8-sig") if path.exists() else ""


class LightProfileTests(unittest.TestCase):
    def test_light_is_separate_dimmable_endpoint(self):
        source = MAIN + read(ZIGBEE_PATH)
        self.assertIn('ZigbeeDimmableLight', source)
        self.assertIn('LIGHT_ENDPOINT', source)
        self.assertIn('setManufacturerAndModel(MANUFACTURER, MODEL)', source)
        self.assertIn('Zigbee.addEndpoint', source)

    def test_zigbee_light_module_owns_endpoint(self):
        source = read(ZIGBEE_PATH)
        self.assertTrue(ZIGBEE_PATH.exists())
        self.assertIn('ZigbeeDimmableLight', source)
        self.assertIn('setLight(', source)

    def test_battery_cluster_not_attached_to_light(self):
        source = MAIN + read(ZIGBEE_PATH)
        self.assertNotIn('zbLight.setPowerSource', source)
        self.assertNotIn('zbLight.setBatteryPercentage', source)
        self.assertNotIn('zbLight.setBatteryVoltage', source)

    def test_beacon_engine_never_updates_zigbee_light(self):
        source = read(BEACON_PATH)
        self.assertNotIn('setLight(', source)
        self.assertNotIn('Zigbee', source)


if __name__ == "__main__":
    unittest.main()
