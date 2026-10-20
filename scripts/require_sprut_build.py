# Both modes patch the upstream Arduino ZigbeeCore at stack initialization.
# An ordinary PlatformIO build must fail rather than silently use different security.
import os
from pathlib import Path

Import("env")
mode = os.environ.get("DOOBSKY_ZIGBEE_PROFILE")
expected = ("strict" if env["PIOENV"] == "esp32-h2-zigbee3-strict"
            else "sprut" if env["PIOENV"] == "esp32-h2-supermini-end-device"
            else None)
if expected is None or mode != expected or os.environ.get("DOOBSKY_SPRUT_PATCH_APPLIED") != "1":
    raise RuntimeError("Use python scripts/build_spruthub.py --profile strict|sprut; plain pio run is blocked")
home = Path(os.environ.get("PLATFORMIO_CORE_DIR") or
            os.environ.get("PLATFORMIO_HOME_DIR") or Path.home() / ".platformio")
core = home / "packages/framework-arduinoespressif32/libraries/Zigbee/src/ZigbeeCore.cpp"
if not core.exists():
    raise RuntimeError("Arduino ZigbeeCore.cpp not installed")
source = core.read_text(encoding="utf-8")
requirement = "esp_zb_secur_link_key_exchange_required_set(true)" if mode == "strict" else "esp_zb_secur_link_key_exchange_required_set(false)"
if ("DOOBSKY_TCLK_PRESTART" not in source or
        requirement not in source or
        source.index("esp_zb_init(zb_cfg)") > source.index(requirement)):
    raise RuntimeError("Security profile not patched after esp_zb_init; refusing build")
