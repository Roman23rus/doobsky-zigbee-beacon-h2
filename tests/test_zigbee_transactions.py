import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class ZigbeeTransactionsTests(unittest.TestCase):
    def test_portable_transaction_policy_compiles_and_executes(self):
        cxx = shutil.which("g++") or shutil.which("clang++")
        if not cxx:
            self.skipTest("Host C++ compiler unavailable (Linux CI runs fixture)")
        with tempfile.TemporaryDirectory() as tmp:
            output = pathlib.Path(tmp) / "transaction_test"
            args = [cxx,"-std=c++17","-Wall","-Wextra","-pedantic","-I",str(ROOT/"include"),
                    str(ROOT/"tests/zigbee_transactions_fixture.cpp"),"-o",str(output)]
            build = subprocess.run(args,capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            proc = subprocess.run([str(output)],capture_output=True,text=True)
            self.assertEqual(proc.returncode,0,proc.stdout+proc.stderr)

    def test_sdk_setter_not_used_in_local_light(self):
        light=(ROOT/"src/zigbee_light.cpp").read_text(encoding="utf8")
        self.assertIn("commitLocalLight(",light)
        self.assertIn("pdMS_TO_TICKS(100)",light)
        self.assertNotIn("endpoint_.setLight(",light)
        self.assertIn("esp_zb_zcl_get_attribute(",light)
        self.assertIn("LightCommitResult::Committed",light)
        self.assertIn("void SafeDimmableLight::zbAttributeSet",light)

    def test_reporting_tracks_each_attribute_independently(self):
        src=(ROOT/"src/battery_telemetry.cpp").read_text(encoding="utf8")
        self.assertIn("reportQueue_.finishAttempt(pctQueued, dcQueued)",src)
        self.assertIn("reportQueue_.percentagePending",src)
        self.assertIn("reportQueue_.voltagePending",src)
        self.assertNotIn("endpoint_.reportDC(",src)
        self.assertIn("esp_zb_zcl_report_attr_cmd_t req{}",src)

    def test_callbacks_do_not_write_serial_directly(self):
        src=(ROOT/"src/main.cpp").read_text(encoding="utf8")
        callback=src.split("void coordinatorProbeCallback(",1)[1].split(chr(10)+"}",1)[0]
        self.assertNotIn("Serial.",callback)
        self.assertIn("zdoReply.sequence",callback)
        light=(ROOT/"src/zigbee_light.cpp").read_text(encoding="utf8")
        handler=light.split("void ZigbeeLight::handleLightChange(",1)[1].split(chr(10)+"}",1)[0]
        self.assertNotIn("Serial.",handler)
        self.assertIn("traceDropped_",handler)

if __name__ == "__main__":
    unittest.main()