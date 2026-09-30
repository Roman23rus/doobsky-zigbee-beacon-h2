import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")
INI = (ROOT / "platformio.ini").read_text(encoding="utf-8-sig")
BEACON_PATH = ROOT / "src" / "beacon_engine.cpp"
BATTERY_PATH = ROOT / "src" / "battery_telemetry.cpp"


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


class HotPathOptimizationTests(unittest.TestCase):
    def test_flash_envelope_uses_lut_not_runtime_trig(self):
        source = read(BEACON_PATH) or MAIN
        body = function_text(source, "BeaconEngine::serviceAt") or function_text(source, "updateBeaconOutput")
        self.assertIn("FLASH_ENVELOPE_LUT", body)
        self.assertNotIn("sinf(", source)
        self.assertNotIn("lroundf(envelope", source)

    def test_optical_hot_path_has_no_blocking_or_background_work(self):
        source = read(BEACON_PATH)
        if not source:
            self.skipTest("BeaconEngine not extracted yet")
        body = function_text(source, "BeaconEngine::serviceAt")
        for token in ("Serial.", "analogRead", "Preferences", "setClusterAttribute", "report", "delay("):
            self.assertNotIn(token, body)

    def test_runtime_battery_service_is_non_blocking(self):
        source = read(BATTERY_PATH) or MAIN
        body = function_text(source, "BatteryTelemetry::service") or function_text(source, "updateBatteryStatus")
        self.assertNotIn("delayMicroseconds", body)
        self.assertNotIn("delay(", body)

    def test_release_build_really_uses_o2(self):
        self.assertIn("build_unflags", INI)
        self.assertIn("-Os", INI)
        self.assertIn("-O2", INI)

    def test_no_blocking_battery_read_before_zigbee_start(self):
        setup = function_text(MAIN, "setup")
        begin_pos = setup.find("Zigbee.begin")
        sample_pos = setup.find("sampleBattery()")
        if sample_pos >= 0:
            self.assertTrue(begin_pos >= 0 and sample_pos > begin_pos)
        self.assertNotIn("readBatteryMv()", setup[:begin_pos if begin_pos >= 0 else None])


if __name__ == "__main__":
    unittest.main()
