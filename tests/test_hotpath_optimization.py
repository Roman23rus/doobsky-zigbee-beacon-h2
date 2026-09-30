import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")
INI = (ROOT / "platformio.ini").read_text(encoding="utf-8-sig")


def function_text(name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", MAIN)
    if not match:
        return ""
    pos = match.end() - 1
    depth = 0
    for i in range(pos, len(MAIN)):
        depth += MAIN[i] == "{"
        depth -= MAIN[i] == "}"
        if depth == 0:
            return MAIN[match.start():i + 1]
    return ""


class HotPathOptimizationTests(unittest.TestCase):
    def test_flash_envelope_uses_lut_not_runtime_trig(self):
        body = function_text("updateBeaconOutput")
        self.assertIn("FLASH_ENVELOPE_LUT", body)
        self.assertNotIn("sinf(", MAIN)
        self.assertNotIn("lroundf(envelope", MAIN)

    def test_time_critical_control_paths_do_not_print(self):
        self.assertNotIn("Serial.", function_text("applyRequestedState"))
        self.assertNotIn("Serial.", function_text("localToggle"))
        self.assertNotIn("Serial.", function_text("serviceRuntimeBatterySample"))

    def test_blocking_work_is_kept_out_of_flash_window(self):
        guard = function_text("backgroundWorkAllowed")
        nvs = function_text("savePersistentStateIfNeeded")
        batt = function_text("updateBatteryStatus")
        sampler = function_text("serviceRuntimeBatterySample")
        self.assertIn("lastPwmDuty != 0", guard)
        self.assertIn("BACKGROUND_WORK_GUARD_US", guard)
        self.assertIn("backgroundWorkAllowed()", nvs)
        self.assertIn("backgroundWorkAllowed()", batt)
        self.assertIn("backgroundWorkAllowed()", sampler)
        self.assertLess(batt.find("backgroundWorkAllowed()"), batt.find("reportBatteryPercentage"))

    def test_release_build_really_uses_o2(self):
        self.assertIn("build_unflags", INI)
        self.assertIn("-Os", INI)
        self.assertIn("-O2", INI)

    def test_no_obsolete_startup_waits(self):
        setup = function_text("setup")
        self.assertNotIn("delay(150)", setup)
        self.assertNotIn("delay(10)", setup)


if __name__ == "__main__":
    unittest.main()
