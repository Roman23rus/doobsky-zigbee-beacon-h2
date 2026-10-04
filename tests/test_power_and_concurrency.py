import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")
BEACON = (ROOT / "src" / "beacon_engine.cpp").read_text(encoding="utf-8-sig")
BATTERY = (ROOT / "src" / "battery_telemetry.cpp").read_text(encoding="utf-8-sig")
ZIGBEE_H = (ROOT / "include" / "zigbee_light.h").read_text(encoding="utf-8-sig")
CFG = (ROOT / "include" / "config.h").read_text(encoding="utf-8-sig")


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


def integer_constant(name: str) -> int:
    match = re.search(rf"\b{name}\s*=\s*([0-9'_,]+)", CFG)
    if not match:
        raise AssertionError(f"missing constant {name}")
    return int(match.group(1).replace("'", "").replace("_", "").replace(",", ""))


class PowerAndConcurrencyTests(unittest.TestCase):
    def test_pending_request_is_consumed_under_mux(self):
        body = function_text(MAIN, "applyRequestedState")
        enter = body.find("portENTER_CRITICAL(&stateMux)")
        pending = body.find("if (requestPending)")
        exit_ = body.find("portEXIT_CRITICAL(&stateMux)")
        self.assertGreaterEqual(enter, 0)
        self.assertGreater(pending, enter)
        self.assertGreater(exit_, pending)

    def test_identify_state_is_synchronized(self):
        writer = function_text(MAIN, "onIdentify")
        reader = function_text(MAIN, "updateIndicators")
        self.assertIn("portENTER_CRITICAL(&stateMux)", writer)
        self.assertIn("portEXIT_CRITICAL(&stateMux)", writer)
        self.assertIn("portENTER_CRITICAL(&stateMux)", reader)
        self.assertIn("portEXIT_CRITICAL(&stateMux)", reader)

    def test_zigbee_wrapper_has_no_dead_identify_shadow(self):
        self.assertNotIn("identifyActive_", ZIGBEE_H)

    def test_nvs_tracks_on_and_level_dirty_state_separately(self):
        body = function_text(MAIN, "savePersistentStateIfNeeded")
        self.assertIn("prefsOnDirty", body)
        self.assertIn("prefsLevelDirty", body)
        self.assertIn('prefs.putBool("on"', body)
        self.assertIn('prefs.putUChar("level"', body)
        self.assertIn("prefsDirtySinceMs = nowMs", body)

    def test_power_profile_reduces_cpu_without_sleepy_zigbee(self):
        self.assertEqual(integer_constant("CPU_FREQUENCY_MHZ"), 64)
        self.assertIn("ZIGBEE_RX_ON_WHEN_IDLE = true", CFG)
        setup = function_text(MAIN, "setup")
        self.assertIn("configureCpuFrequency()", setup)
        cpu = function_text(MAIN, "configureCpuFrequency")
        self.assertIn("setCpuFrequencyMhz(CPU_FREQUENCY_MHZ)", cpu)

    def test_housekeeping_and_rgb_updates_are_throttled(self):
        self.assertEqual(integer_constant("HOUSEKEEPING_INTERVAL_MS"), 20)
        self.assertEqual(integer_constant("STATUS_UPDATE_INTERVAL_MS"), 20)
        self.assertEqual(integer_constant("BUTTON_POLL_INTERVAL_MS"), 20)
        self.assertEqual(integer_constant("RGB_UPDATE_US"), 10_000)

    def test_beacon_hot_path_avoids_unneeded_locks(self):
        body = function_text(BEACON, "BeaconEngine::serviceAt")
        self.assertNotIn("portENTER_CRITICAL", body)
        arm = function_text(BEACON, "BeaconEngine::armDeadline")
        self.assertNotIn("esp_timer_stop", arm)

    def test_battery_scaling_is_fixed_point_and_zero_safe(self):
        body = function_text(BATTERY, "BatteryTelemetry::batteryMvFromAdcSum")
        self.assertIn("if (samples == 0)", body)
        self.assertIn("BATTERY_CALIBRATION_PERMILLE", body)
        self.assertNotIn("float", body)
        self.assertNotIn("BATTERY_DIVIDER_RATIO", CFG)
        self.assertNotIn("BATTERY_CALIBRATION =", CFG)


if __name__ == "__main__":
    unittest.main()
