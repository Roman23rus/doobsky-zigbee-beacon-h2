import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")
CFG = (ROOT / "include" / "config.h").read_text(encoding="utf-8-sig")
BEACON_PATH = ROOT / "src" / "beacon_engine.cpp"
ZIGBEE_PATH = ROOT / "src" / "zigbee_light.cpp"


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


class PerformanceRegressionTests(unittest.TestCase):
    def test_zigbee_callback_is_non_blocking(self):
        source = read(ZIGBEE_PATH) or MAIN
        body = function_text(source, "ZigbeeLight::handleLightChange") or function_text(source, "onZigbeeLightChange")
        self.assertNotIn("Serial.", body)
        self.assertNotIn("delay(", body)
        self.assertNotIn("analogRead", body)

    def test_beacon_hot_path_avoids_cycle_modulo(self):
        source = read(BEACON_PATH) or MAIN
        body = function_text(source, "BeaconEngine::serviceAt") or function_text(source, "updateBeaconOutput")
        self.assertNotIn("% CYCLE_US", body)
        self.assertIn("cycleEpochUs", body)

    def test_runtime_battery_sampling_is_incremental(self):
        source = (ROOT / "src" / "battery_telemetry.cpp")
        text = read(source) or MAIN
        self.assertIn("sampler_", text)
        runtime = function_text(text, "BatteryTelemetry::service") or function_text(text, "updateBatteryStatus")
        self.assertNotIn("delayMicroseconds", runtime)

    def test_beacon_task_has_lower_priority_than_zigbee(self):
        source = read(BEACON_PATH)
        self.assertIn("BEACON_TASK_PRIORITY", CFG)
        self.assertRegex(source, r"xTaskCreate[^;]*BEACON_TASK_PRIORITY")

    def test_off_and_dark_intervals_block_instead_of_polling(self):
        source = read(BEACON_PATH)
        self.assertRegex(source, r"xTaskNotifyWait|ulTaskNotifyTake")
        loop = function_text(MAIN, "loop")
        self.assertNotIn("delay(1)", loop)
        self.assertNotIn("updateBeaconOutput", loop)

    def test_identify_callback_is_non_blocking(self):
        source = read(ZIGBEE_PATH) or MAIN
        body = function_text(source, "ZigbeeLight::handleIdentify") or function_text(source, "onIdentify")
        self.assertNotIn("Serial.", body)
        self.assertNotIn("delay(", body)


if __name__ == "__main__":
    unittest.main()
