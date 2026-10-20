import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]

class LightStateBehaviorTests(unittest.TestCase):
    def test_portable_light_transition_cpp_runs(self):
        compiler=shutil.which("g++") or shutil.which("clang++")
        if not compiler:
            self.skipTest("No host C++ compiler installed (GitHub CI Ubuntu runs this test)")
        with tempfile.TemporaryDirectory() as td:
            binary=pathlib.Path(td)/("light_policy.exe" if __import__("os").name=="nt" else "light_policy")
            res=subprocess.run([compiler,"-std=c++17","-O2","-I",str(ROOT/"include"),str(ROOT/"tests/light_state_transition_fixture.cpp"),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(res.returncode,0,res.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_no_unconditional_light_setter_from_service(self):
        source=(ROOT/"src/zigbee_light.cpp").read_text(encoding="utf-8-sig")
        self.assertNotIn("setLightLevel(",source)
        self.assertNotIn("levelCorrection",source)
        self.assertIn("applyLightAttribute",source)

if __name__=="__main__":unittest.main()
