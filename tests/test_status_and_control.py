import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8-sig")
BEACON = (ROOT / "src" / "beacon_engine.cpp").read_text(encoding="utf-8-sig")
ZIGBEE = (ROOT / "src" / "zigbee_light.cpp").read_text(encoding="utf-8-sig")
ZIGBEE_H = (ROOT / "include" / "zigbee_light.h").read_text(encoding="utf-8-sig")


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


class StatusAndControlTests(unittest.TestCase):
    def test_connected_blue_led_is_off(self):
        body = function_text(MAIN, "updateIndicators")
        connected = body.find("Zigbee.connected()")
        self.assertGreaterEqual(connected, 0)
        branch = body[connected:].split("else", 1)[0]
        self.assertIn("writeStatusLed(false)", branch)

    def test_identify_overrides_connected_off_state(self):
        body = function_text(MAIN, "updateIndicators")
        identify = body.find("if (identify)")
        connected = body.find("Zigbee.connected()")
        self.assertGreaterEqual(identify, 0)
        self.assertGreater(connected, identify)
        self.assertIn("writeStatusLed", body[identify:connected])
        self.assertIn("writeStatusLed(false)", body[connected:])

    def test_zigbee_callback_wakes_beacon_immediately(self):
        body = function_text(MAIN, "onZigbeeLightChange")
        self.assertIn("beacon.request(state, level)", body)
        self.assertIn("enqueueRequestedState(state, level)", body)
        self.assertNotIn("delay(", body)
        self.assertNotIn("Serial.", body)

    def test_main_state_apply_does_not_retrigger_beacon(self):
        body = function_text(MAIN, "applyRequestedState")
        self.assertNotIn("beacon.request", body)
        self.assertIn("prefsDirty", body)

    def test_local_toggle_is_one_logical_update(self):
        body = function_text(MAIN, "localToggle")
        self.assertEqual(body.count("zigbeeLight.setLocalState"), 1)

    def test_zdo_ping_is_rate_limited_before_stack_enqueue(self):
        body = function_text(MAIN, "requestCoordinatorProbe")
        self.assertIn("nowMs - lastRequestMs < 15000U", body)
        self.assertIn("lastRequestMs = nowMs", body)
        self.assertIn("[ZDO-PING] ignored: rate limit 15s", body)
        self.assertLess(body.index("lastRequestMs != 0"), body.index("esp_zb_zdo_node_desc_req("))

    def test_persistence_is_delayed_and_outside_flash_window(self):
        body = function_text(MAIN, "savePersistentStateIfNeeded")
        self.assertIn("PREFS_WRITE_DELAY_MS", body)
        self.assertIn("backgroundWorkAllowed()", body)
        self.assertIn('prefs.putBool("on"', body)
        self.assertIn('prefs.putUChar("level"', body)

    def test_level_zero_keeps_remembered_brightness_without_rewriting_onoff(self):
        handler = function_text(ZIGBEE, "ZigbeeLight::handleLightChange")
        transition = (ROOT / "include" / "light_state_transition.h").read_text(encoding="utf-8-sig")
        self.assertIn("normalizedLevel == 0", transition)
        self.assertIn("rememberedLevel", handler)
        self.assertIn("applyLightAttribute", handler)
        self.assertNotIn("setLightLevel(", ZIGBEE)
        self.assertNotIn("levelCorrection", ZIGBEE_H + ZIGBEE)
        self.assertIn("zigbeeLight.service(nowMs)", MAIN)

    def test_callback_does_not_block_or_log(self):
        handler = function_text(ZIGBEE, "ZigbeeLight::handleLightChange")
        self.assertNotIn("Serial.", handler)
        self.assertNotIn("delay(", handler)
        self.assertIn("++traceSequence_", handler)
        self.assertIn("traces_[traceHead_]", handler)
        self.assertIn("callback(state, next.level)", handler)
        self.assertIn("readTrace(lightTrace, seenLightTrace)", MAIN)

    def test_local_setter_is_only_writable_path_in_project(self):
        self.assertEqual(ZIGBEE.count("endpoint_.setLocalChecked("), 2) # begin and local
        self.assertNotIn("endpoint_.setLightLevel(", ZIGBEE)

    def test_latest_beacon_request_is_authoritative(self):
        body = function_text(BEACON, "BeaconEngine::request")
        self.assertIn("requestedOn_ = on", body)
        self.assertIn("requestedLevel_ = level", body)
        self.assertIn("requestDirty_ = true", body)
        self.assertEqual(body.count("xTaskNotifyGive"), 1)

    def test_factory_reset_checks_app_nvs_clear(self):
        body = function_text(MAIN, "performFactoryReset")
        self.assertIn("bool cleared = prefs.clear()", body)
        self.assertIn("if (!cleared)", body)
        self.assertIn("prefs.end()", body)

    def test_housekeeping_does_not_poll_at_one_millisecond(self):
        body = function_text(MAIN, "loop")
        self.assertNotIn("delay(1)", body)
        self.assertNotIn("updateBeaconOutput", body)


if __name__ == "__main__":
    unittest.main()
