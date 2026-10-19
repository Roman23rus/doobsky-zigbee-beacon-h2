#!/usr/bin/env python3
"""Build with Sprut.Hub's Zigbee Trust Center compatibility workaround.

Temporarily edits installed Arduino-ESP32 ZigbeeCore.cpp (then always restores it)
so esp_zb_secur_link_key_exchange_required_set(false) runs after esp_zb_init()
but before Zigbee stack start. Does not disable AES-128 network encryption.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PIO_HOME = Path(os.environ.get("PLATFORMIO_CORE_DIR") or os.environ.get("PLATFORMIO_HOME_DIR") or Path.home() / ".platformio")
CORE = PIO_HOME / "packages/framework-arduinoespressif32/libraries/Zigbee/src/ZigbeeCore.cpp"
INCLUDE = '#include "ZigbeeCore.h"'
INIT = "  esp_zb_init(zb_cfg);"
PATCH = """  esp_zb_init(zb_cfg);
#if defined(SPRUT_TC_COMPAT_MODE)
  // DOOBSKY_TCLK_PRESTART: optional R21 TC exchange not required; NWK AES unchanged.
  esp_zb_secur_link_key_exchange_required_set(false);
  log_w("[ZBSEC] optional Trust Center link-key exchange disabled BEFORE stack start");
#endif"""


def main():
    if "-D SPRUT_TC_COMPAT_MODE" not in (ROOT / "platformio.ini").read_text(encoding="utf-8-sig"):
        raise RuntimeError("Define SPRUT_TC_COMPAT_MODE in platformio.ini")
    original = CORE.read_bytes()
    text = original.decode("utf-8-sig")
    if text.count(INCLUDE) != 1 or text.count(INIT) != 1 or "DOOBSKY_TCLK_PRESTART" in text:
        raise RuntimeError("Unknown or already modified Zigbee SDK; refusing to patch")
    patched = text.replace(INCLUDE, INCLUDE + "\n#include \"esp_zigbee_secur.h\"", 1).replace(INIT, PATCH, 1)
    if b"\r\n" in original:
        patched = patched.replace("\n", "\r\n")
    pio = os.environ.get("PIO_EXE") or str(PIO_HOME / "penv" / ("Scripts/pio.exe" if os.name == "nt" else "bin/pio"))
    if not Path(pio).is_file():
        pio = "pio"
    print("Patching SDK only for this build:", CORE, flush=True)
    try:
        CORE.write_bytes(patched.encode("utf-8"))
        result = subprocess.call([pio, "run", "-e", "esp32-h2-supermini-end-device"], cwd=ROOT)
    finally:
        CORE.write_bytes(original)
        if CORE.read_bytes() != original:
            raise RuntimeError("Could not restore framework ZigbeeCore.cpp")
        print("Original SDK restored byte-for-byte.", flush=True)
    if result == 0:
        output = ROOT / ".pio/build/esp32-h2-supermini-end-device/firmware.bin"
        if output.exists():
            print("Firmware", output, "SHA256", hashlib.sha256(output.read_bytes()).hexdigest())
    return result


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print("Build failed:", exc, file=sys.stderr)
        sys.exit(1)