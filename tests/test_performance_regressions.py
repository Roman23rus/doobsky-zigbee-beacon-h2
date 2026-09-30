import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")
CFG = (ROOT / "include" / "config.h").read_text(encoding="utf-8-sig")


def function_text(name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", MAIN)
    if not match:
        return ""
    start = match.start()
    pos = match.end() - 1
    depth = 0
    for i in range(pos, len(MAIN)):
        if MAIN[i] == "{":
            depth += 1
        elif MAIN[i] == "}":
            depth -= 1
            if depth == 0:
                return MAIN[start:i + 1]
    return MAIN[start:]


class PerformanceRegressionTests(unittest.TestCase):
    def test_light_callback_is_non_blocking(self):
        body = function_text("onZigbeeLightChange")
        self.assertNotIn("Serial.", body)
        self.assertNotIn("delay(", body)

    def test_identify_callback_is_non_blocking(self):
        body = function_text("onIdentify")
        self.assertNotIn("Serial.", body)
        self.assertNotIn("delay(", body)

    def test_state_apply_has_lock_free_idle_fast_path(self):
        body = function_text("applyRequestedState")
        self.assertRegex(body, r"if\s*\(!requestPending\)\s*return;")
        self.assertLess(body.find("if (!requestPending) return;"), body.find("portENTER_CRITICAL"))

    def test_status_and_button_are_throttled(self):
        self.assertIn("STATUS_UPDATE_INTERVAL_MS", CFG)
        self.assertIn("BUTTON_POLL_INTERVAL_MS", CFG)
        self.assertIn("lastStatusLedLevel", MAIN)

    def test_beacon_hot_path_avoids_cycle_modulo(self):
        body = function_text("updateBeaconOutput")
        self.assertNotIn("% CYCLE_US", body)
        self.assertIn("nextBeaconUpdateUs", body)

    def test_runtime_battery_sampling_is_incremental(self):
        body = function_text("updateBatteryStatus")
        self.assertIn("batterySampler", body)
        self.assertNotIn("delayMicroseconds", body)


if __name__ == "__main__":
    unittest.main()

