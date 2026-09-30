import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")
ZIGBEE_PATH = ROOT / "src" / "zigbee_light.cpp"
ZIGBEE_H_PATH = ROOT / "include" / "zigbee_light.h"
BEACON_PATH = ROOT / "src" / "beacon_engine.cpp"


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8-sig") if path.exists() else ""


def function_text(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", source)
    if not match:
        return ""
    pos = match.end() - 1
    depth = 0
    for i in range(pos, len(source)):
        depth += source[i] == "{"
        depth -= source[i] == "}"
        if depth == 0:
            return source[match.start():i + 1]
    return ""


class LightProfileTests(unittest.TestCase):
    def test_light_is_separate_dimmable_endpoint(self):
        source = MAIN + read(ZIGBEE_H_PATH) + read(ZIGBEE_PATH)
        self.assertIn('ZigbeeDimmableLight', source)
        self.assertIn('LIGHT_ENDPOINT', source)
        self.assertIn('setManufacturerAndModel(MANUFACTURER, MODEL)', source)
        self.assertIn('Zigbee.addEndpoint', source)

    def test_zigbee_light_module_owns_endpoint(self):
        source = read(ZIGBEE_H_PATH) + read(ZIGBEE_PATH)
        self.assertTrue(ZIGBEE_PATH.exists())
        self.assertTrue(ZIGBEE_H_PATH.exists())
        self.assertIn('ZigbeeDimmableLight', source)
        self.assertIn('setLight(', source)

    def test_setlight_is_confined_to_logical_light_module(self):
        zigbee = read(ZIGBEE_PATH)
        beacon = read(BEACON_PATH)
        self.assertNotIn('setLight(', MAIN)
        self.assertNotIn('setLight(', beacon)
        self.assertIn('setLight(', zigbee)

    def test_local_toggle_updates_shadow_once(self):
        source = read(ZIGBEE_PATH)
        body = function_text(source, 'ZigbeeLight::setLocalState')
        self.assertEqual(body.count('.setLight('), 1)

    def test_logical_level_never_depends_on_instantaneous_pwm(self):
        source = read(ZIGBEE_H_PATH) + read(ZIGBEE_PATH)
        for token in ('ledcWrite', 'FLASH_ENVELOPE_LUT', 'PWM_MAX', 'writePwm'):
            self.assertNotIn(token, source)
        self.assertIn('level_', source)

    def test_battery_cluster_not_attached_to_light(self):
        source = MAIN + read(ZIGBEE_H_PATH) + read(ZIGBEE_PATH)
        self.assertNotIn('setPowerSource', source)
        self.assertNotIn('setBatteryPercentage', source)
        self.assertNotIn('setBatteryVoltage', source)

    def test_beacon_engine_never_updates_zigbee_light(self):
        source = read(BEACON_PATH)
        self.assertNotIn('setLight(', source)
        self.assertNotIn('Zigbee', source)


if __name__ == "__main__":
    unittest.main()
