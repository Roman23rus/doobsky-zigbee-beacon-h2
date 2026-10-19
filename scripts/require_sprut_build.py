# Fail closed: only the scoped wrapper may build the Sprut-compatible image.
import os
from pathlib import Path

pio_home = Path(os.environ.get("PLATFORMIO_CORE_DIR") or os.environ.get("PLATFORMIO_HOME_DIR") or Path.home() / ".platformio")
core = pio_home / "packages/framework-arduinoespressif32/libraries/Zigbee/src/ZigbeeCore.cpp"
if os.environ.get("DOOBSKY_SPRUT_PATCH_APPLIED") != "1" or not core.exists():
    raise RuntimeError("Build Sprut firmware with python scripts/build_spruthub.py (pre-start TCLK patch required)")
content = core.read_text(encoding="utf-8")
if "DOOBSKY_TCLK_PRESTART" not in content or "esp_zb_secur_link_key_exchange_required_set(false)" not in content:
    raise RuntimeError("TCLK patch missing from Arduino ZigbeeCore: refusing unsafe firmware build")
if content.index("esp_zb_init(zb_cfg)") > content.index("esp_zb_secur_link_key_exchange_required_set(false)"):
    raise RuntimeError("TCLK patch ordering invalid: must run after esp_zb_init()")
