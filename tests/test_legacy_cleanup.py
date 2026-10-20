import pathlib
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
MAIN=(ROOT/"src/main.cpp").read_text(encoding="utf-8-sig")
LIGHT=(ROOT/"src/zigbee_light.cpp").read_text(encoding="utf-8-sig")
CONFIG=(ROOT/"include/config.h").read_text(encoding="utf-8-sig")
INI=(ROOT/"platformio.ini").read_text(encoding="utf-8-sig")

class LegacyCleanupTests(unittest.TestCase):
    def test_single_logical_zero_level_handler(self):
        self.assertIn("applyLightAttribute",LIGHT)
        self.assertNotIn("Some coordinators carry CurrentLevel=0",MAIN)
        self.assertNotIn("if (!newOn && newLevel == 0)",MAIN)

    def test_basic_power_source_not_written_twice(self):
        self.assertIn("setInitialBatteryPowerSource",LIGHT)
        self.assertNotIn("setBasicPowerSourceBattery",LIGHT+MAIN)
        self.assertIn("esp_zb_set_node_descriptor_power_source(false)",MAIN)

    def test_no_legacy_immediate_nvs_migration(self):
        start=MAIN.index("void loadPersistentState()")
        end=MAIN.index("void savePersistentStateIfNeeded(",start)
        self.assertNotIn("prefs.putUChar",MAIN[start:end])
        self.assertIn("prefsLevelDirty = true",MAIN[start:end])

    def test_obsolete_testing_templates_and_generator_absent(self):
        self.assertFalse((ROOT/"templates/spruthub/Doobsky-DBL-BAT-TEST.json").exists())
        self.assertFalse((ROOT/"templates/spruthub/Doobsky-DBL-BAT-CONTROL-v4-POWER-EXPERIMENT.json").exists())
        self.assertFalse((ROOT/"scripts/generate_install_code.py").exists())
        self.assertTrue((ROOT/"templates/spruthub/Doobsky-DBL-BAT-CONTROL-v3.json").exists())

    def test_profile_tools_have_generic_names(self):
        self.assertTrue((ROOT/"scripts/build_firmware.py").exists())
        self.assertTrue((ROOT/"scripts/require_profile_build.py").exists())
        self.assertNotIn("build_spruthub.py",INI)
        self.assertNotIn("require_sprut_build.py",INI)
        self.assertIn("default_envs = esp32-h2-zigbee3-strict",INI)
        self.assertIn("esp32-h2-sprut-compat",INI)
        self.assertIn('FW_VERSION[] = "0.9.0-alpha.3"',CONFIG)

if __name__=="__main__": unittest.main()
