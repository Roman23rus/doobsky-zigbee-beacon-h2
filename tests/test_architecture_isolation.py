import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BEACON_CPP_PATH = ROOT / "src" / "beacon_engine.cpp"
BEACON_H_PATH = ROOT / "include" / "beacon_engine.h"
ZIGBEE_CPP_PATH = ROOT / "src" / "zigbee_light.cpp"
MAIN_PATH = ROOT / "src" / "main.cpp"


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


class ArchitectureIsolationTests(unittest.TestCase):
    def test_beacon_engine_has_no_zigbee_dependency(self):
        self.assertTrue(BEACON_CPP_PATH.exists())
        self.assertTrue(BEACON_H_PATH.exists())
        source = read(BEACON_CPP_PATH) + read(BEACON_H_PATH)
        for token in ("Zigbee", "setLight", "setClusterAttribute", "reportBattery", "reportDC"):
            self.assertNotIn(token, source)

    def test_light_shadow_is_not_driven_by_pwm(self):
        source = read(BEACON_CPP_PATH)
        self.assertNotIn("setLight(", source)
        self.assertNotIn("setClusterAttribute", source)
        self.assertNotIn("CurrentLevel", source)
        self.assertNotIn("ON_OFF", source)

    def test_always_on_rx_is_explicit_before_begin(self):
        source = read(ZIGBEE_CPP_PATH) + "\n" + read(MAIN_PATH)
        rx = source.find("Zigbee.setRxOnWhenIdle(true)")
        begin = source.find("Zigbee.begin(")
        self.assertGreaterEqual(rx, 0)
        self.assertGreater(begin, rx)

    def test_connected_status_led_is_off(self):
        main = read(MAIN_PATH)
        body = function_text(main, "updateIndicators")
        connected = body.find("Zigbee.connected()")
        self.assertGreaterEqual(connected, 0)
        connected_path = body[connected:]
        self.assertIn("writeStatusLed(false)", connected_path.split("else", 1)[0])

    def test_beacon_engine_uses_absolute_time_and_event_waits(self):
        source = read(BEACON_CPP_PATH)
        self.assertIn("esp_timer_get_time()", source)
        self.assertNotIn("% CYCLE_US", source)
        self.assertRegex(source, r"xTaskNotifyWait|ulTaskNotifyTake")
        self.assertIn("esp_timer_start_once", source)

    def test_on_transition_starts_immediately_duplicate_on_does_not_restart(self):
        source = read(BEACON_CPP_PATH)
        request = function_text(source, "BeaconEngine::request")
        apply = function_text(source, "BeaconEngine::applyRequestedState")
        self.assertIn("turningOn", request)
        self.assertIn("restartCycleRequested_", request)
        self.assertNotIn("cycleEpochUs_", request)
        self.assertIn("restartCycle", apply)
        self.assertIn("cycleEpochUs_", apply)

    def test_shared_active_state_is_written_under_mux(self):
        source = read(BEACON_CPP_PATH)
        apply = function_text(source, "BeaconEngine::applyRequestedState")
        assignment = apply.find("activeOn_ = newOn")
        self.assertGreaterEqual(assignment, 0)
        enter = apply.rfind("portENTER_CRITICAL", 0, assignment)
        exit_ = apply.rfind("portEXIT_CRITICAL", 0, assignment)
        self.assertGreater(enter, exit_)


if __name__ == "__main__":
    unittest.main()
