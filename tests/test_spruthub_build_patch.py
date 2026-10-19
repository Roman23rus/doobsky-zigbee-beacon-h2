import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD_SCRIPT = ROOT / "scripts/build_spruthub.py"


class SprutBuildScriptTests(unittest.TestCase):
    def test_scoped_patch_restored_on_success_and_failure(self):
        self.assertTrue(BUILD_SCRIPT.exists())
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            core = base / "packages/framework-arduinoespressif32/libraries/Zigbee/src/ZigbeeCore.cpp"
            core.parent.mkdir(parents=True)
            before = b'#include "ZigbeeCore.h"\nvoid init() {\n  esp_zb_init(zb_cfg);\n}\n'
            core.write_bytes(before)
            if os.name == "nt":
                fake = base / "mock-pio.cmd"
                fake.write_text('@echo off\nfindstr /C:"DOOBSKY_TCLK_PRESTART" "%PLATFORMIO_CORE_DIR%\\packages\\framework-arduinoespressif32\\libraries\\Zigbee\\src\\ZigbeeCore.cpp" >nul\nexit /B %ERRORLEVEL%\n',encoding='ascii')
            else:
                fake = base / "mock-pio"
                fake.write_text('#!/bin/sh\ngrep -q DOOBSKY_TCLK_PRESTART "$PLATFORMIO_CORE_DIR/packages/framework-arduinoespressif32/libraries/Zigbee/src/ZigbeeCore.cpp"\n',encoding='ascii')
                fake.chmod(0o755)
            env = dict(os.environ, PLATFORMIO_CORE_DIR=str(base), PIO_EXE=str(fake), DOOBSKY_BUILD_TEST_NO_FIRMWARE="1")
            call = subprocess.run([sys.executable, str(BUILD_SCRIPT)], cwd=ROOT, env=env, capture_output=True, text=True)
            self.assertEqual(call.returncode, 0, call.stdout + call.stderr)
            self.assertEqual(core.read_bytes(), before, "SDK was not restored byte-for-byte")
            if os.name == "nt":
                fake.write_text("@echo off\nexit /B 3\n",encoding="ascii")
            else:
                fake.write_text("#!/bin/sh\nexit 3\n",encoding="ascii")
            call = subprocess.run([sys.executable, str(BUILD_SCRIPT)], cwd=ROOT, env=env, capture_output=True, text=True)
            self.assertEqual(call.returncode, 3, call.stdout + call.stderr)
            self.assertEqual(core.read_bytes(), before, "SDK was not restored after failure")


if __name__ == "__main__":
    unittest.main()