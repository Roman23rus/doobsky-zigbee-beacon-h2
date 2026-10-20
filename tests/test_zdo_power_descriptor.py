import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")
LIGHT_H = (ROOT / "include" / "zigbee_light.h").read_text(encoding="utf-8-sig")
LIGHT_CPP = (ROOT / "src" / "zigbee_light.cpp").read_text(encoding="utf-8-sig")

class ZdoPowerDescriptorTests(unittest.TestCase):
    def test_node_power_descriptor_declares_rechargeable_battery(self):
        self.assertIn("esp_zb_set_node_descriptor_power_source(false)", MAIN)
        self.assertIn("esp_zb_set_node_power_descriptor(power)", MAIN)
        self.assertIn("ESP_ZB_AF_NODE_POWER_SOURCE_RECHARGEABLE_BATTERY", MAIN)
        self.assertIn("ESP_ZB_AF_NODE_POWER_MODE_SYNC_ON_WHEN_IDLE", MAIN)

    def test_zigbee_descriptor_calls_are_guarded_from_application_loop(self):
        self.assertIn("esp_zb_lock_acquire(pdMS_TO_TICKS(10))", MAIN)
        self.assertIn("esp_zb_lock_release()", MAIN)
        self.assertIn("setInitialBatteryPowerSource", LIGHT_CPP)
        self.assertIn("syncZigbeePowerDescriptors();", MAIN)
        self.assertIn("if (!esp_zb_lock_acquire(pdMS_TO_TICKS(10))) return;", MAIN)

    def test_light_basic_power_source_matches_physical_node(self):
        self.assertNotIn("setBasicPowerSourceBattery", LIGHT_H)
        self.assertIn("setInitialBatteryPowerSource", LIGHT_H)
        self.assertIn("ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID", LIGHT_CPP)
        self.assertIn("ZB_POWER_SOURCE_BATTERY", LIGHT_CPP)

if __name__ == "__main__":
    unittest.main()
