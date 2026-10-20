#!/usr/bin/env python3
"""Safe, repeatable Sprut.Hub firmware build. Does not upload to any device.

Fail-closed PlatformIO environment requires this wrapper. The Arduino Zigbee
stack is patched only within a short, locked build scope. Original bytes
are restored even on build failure; the output manifest records provenance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
PIO_HOME = Path(os.environ.get("PLATFORMIO_CORE_DIR")
                or os.environ.get("PLATFORMIO_HOME_DIR")
                or Path.home() / ".platformio")
CORE = (PIO_HOME / "packages/framework-arduinoespressif32/libraries/"
        "Zigbee/src/ZigbeeCore.cpp")
ENV_COMPAT = "esp32-h2-sprut-compat"
ENV_STRICT = "esp32-h2-zigbee3-strict"
INCLUDE = '#include "ZigbeeCore.h"'
INIT = "  esp_zb_init(zb_cfg);"
PATCH_MARKER = "DOOBSKY_TCLK_PRESTART"
PATCH = """  esp_zb_init(zb_cfg);
#if defined(SPRUT_TC_COMPAT_MODE) && !defined(ZIGBEE3_STRICT_SECURITY)
  // DOOBSKY_TCLK_PRESTART: compatibility only; no TCLK exchange requested.
  // Zigbee NWK AES-128 remains enabled.
  esp_zb_secur_link_key_exchange_required_set(false);
  log_w("[ZBSEC] Sprut compatibility mode (TCLK exchange not required)");
#elif defined(ZIGBEE3_STRICT_SECURITY) && !defined(SPRUT_TC_COMPAT_MODE)
  // DOOBSKY_TCLK_PRESTART: strict Zigbee3 join requires TCLK update.
  esp_zb_secur_link_key_exchange_required_set(true);
  log_i("[ZBSEC] Zigbee3 strict mode (TCLK exchange required)");
#else
#error "Exactly one Zigbee security profile must be selected"
#endif"""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run(cmd: list[str], **kwargs) -> int:
    print("+", " ".join(cmd), flush=True)
    return subprocess.call(cmd, cwd=ROOT, **kwargs)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=("strict", "sprut"), default="strict")
    args = parser.parse_args()
    env_name = ENV_STRICT if args.profile == "strict" else ENV_COMPAT
    ini = (ROOT / "platformio.ini").read_text(encoding="utf-8-sig")
    macro = "-D ZIGBEE3_STRICT_SECURITY" if args.profile == "strict" else "-D SPRUT_TC_COMPAT_MODE"
    if macro not in ini:
        raise RuntimeError("Required security profile build define is missing")
    core_home = PIO_HOME / "packages"
    exe = os.environ.get("PIO_EXE")
    if not exe:
        candidate = PIO_HOME / "penv" / ("Scripts/pio.exe" if os.name == "nt" else "bin/pio")
        exe = str(candidate) if candidate.is_file() else "pio"
    if not CORE.exists():
        print("Installing PlatformIO environment packages...", flush=True)
        result = run([exe, "pkg", "install", "-e", env_name])
        if result:
            return result
    if not CORE.exists():
        raise RuntimeError(f"Framework ZigbeeCore.cpp still missing: {CORE}")
    original = CORE.read_bytes()
    if original.startswith(b"\xef\xbb\xbf"):
        raise RuntimeError("Unexpected UTF-8 BOM in framework; avoid an unsafe text rewrite")
    source = original.decode("utf-8")
    if source.count(INCLUDE) != 1 or source.count(INIT) != 1 or PATCH_MARKER in source:
        raise RuntimeError("SDK markers not unique or SDK is already patched")
    patched = source.replace(INCLUDE, INCLUDE + '\n#include "esp_zigbee_secur.h"', 1)
    patched = patched.replace(INIT, PATCH, 1)
    if b"\r\n" in original:
        patched = patched.replace("\n", "\r\n")
    patch_bytes = patched.encode("utf-8")
    lock = core_home / ".doobsky_sprut_build_lock"
    # Fail rather than run two builds against the same temporarily modified SDK.
    try:
        lock.mkdir(exist_ok=False)
    except FileExistsError:
        raise RuntimeError(f"Another Sprut build is using this SDK: {lock}")
    code = 1
    try:
        if CORE.read_bytes() != original:
            raise RuntimeError("Framework file changed before build start")
        try:
            CORE.write_bytes(patch_bytes)
            build_env = dict(os.environ)
            build_env["DOOBSKY_PROFILE_PATCH_APPLIED"] = "1"
            build_env["DOOBSKY_ZIGBEE_PROFILE"] = args.profile
            code = run([exe, "run", "-e", env_name], env=build_env)
        finally:
            CORE.write_bytes(original)
            if CORE.read_bytes() != original:
                raise RuntimeError("FATAL: SDK was not restored byte-for-byte")
            print("Original Arduino ZigbeeCore restored (SHA256 " + sha256(original) + ")", flush=True)
    finally:
        lock.rmdir()
    if code != 0:
        return code
    if os.environ.get("DOOBSKY_BUILD_TEST_NO_FIRMWARE") == "1":
        return 0  # Mocked pio command in isolated unit test only.
    fw = ROOT / ".pio/build" / env_name / "firmware.bin"
    if not fw.is_file():
        raise RuntimeError("Build reported success but firmware.bin is absent")
    def get_version(package: str) -> str:
        meta = core_home / package / "package.json"
        if not meta.exists():
            return "unknown"
        try:
            return str(json.loads(meta.read_text(encoding="utf-8-sig")).get("version","unknown"))
        except (ValueError, OSError):
            return "unknown"
    config = (ROOT / "include/config.h").read_text(encoding="utf-8")
    m = re.search(r'FW_VERSION\[\]\s*=\s*"([^"]+)"', config)
    try:
        git_sha = subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()
    except Exception:
        git_sha = "unknown"
    result = {
        "profile": args.profile,
        "tclk_exchange_required": args.profile == "strict",
        "firmware_version": m.group(1) if m else "unknown",
        "git_revision": git_sha,
        "pio_environment": env_name,
        "platformio_core": subprocess.run([exe,"--version"],capture_output=True,text=True).stdout.strip(),
        "arduino_framework": get_version("framework-arduinoespressif32"),
        "platform": "pioarduino 55.03.312-1",
        "esp_zigbee_sdk": "bundled with Arduino, target 1.6.8",
        "sdk_core_original_sha256": sha256(original),
        "sdk_core_patched_sha256": sha256(patch_bytes),
        "firmware_sha256": sha256(fw.read_bytes()),
        "firmware_size_bytes": fw.stat().st_size,
        "build_utc": datetime.now(timezone.utc).isoformat(),
        "flash_or_nvs_modified": False,
    }
    dest=ROOT/"build"/f"firmware-manifest-{args.profile}.json"
    dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(json.dumps(result,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    print("Built", fw, "\nSHA256:",result["firmware_sha256"],"\nManifest:",dest,flush=True)
    return 0


if __name__=="__main__":
    try:
        sys.exit(main())
    except (RuntimeError,OSError,UnicodeError) as error:
        print("BUILD ERROR:",error,file=sys.stderr)
        sys.exit(1)
