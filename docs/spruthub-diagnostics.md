# Sprut.Hub / ESP32-H2 — commissioning compatibility fix (2026-10-19)

## Root-cause assessment

Sprut.Hub logs for device IEEE `48:31:B7:FF:FE:CD:4D:C3` showed a repeating sequence of joins, new Zigbee short addresses, `Node has left from network`, and unsuccessful interviews (including `EMBER_DELIVERY_FAILED`). The short NWK address changes are **a symptom of rejoining**, not a bug in IEEE address generation or a reason to hardcode an address.

On ESP32-H2, the stock Arduino Zigbee stack repeatedly showed `Network steering was not successful (ESP_FAIL)`, `factoryNew=1` and no valid PAN/channel. In a controlled comparison on the same hardware and coordinator, calling the SDK function `esp_zb_secur_link_key_exchange_required_set(false)` **after `esp_zb_init()` but before `esp_zb_start()`** produced a usable network association. The initial approach calling this after `Zigbee.begin()` did not establish stable commissioning.

The function disables the **requirement** for the optional Zigbee R21 Trust Center Link Key exchange; it **does not disable Zigbee network-layer AES-128 encryption**. This is a coordinator compatibility workaround, not a generally recommended default; users should evaluate their Trust Center security policy before deploying to other networks.

The failure to complete the optional TC exchange is the **strongest evidence-supported root-cause hypothesis**. No over-the-air packet capture or decrypted APS Trust Center trace was obtained, so a definitive microscopic failure step has not been proven.

## Verified on real COM3 device

Firmware `0.8.2-alpha.4-sprut-zdo` on ESP32-H2 with IEEE `48:31:B7:FF:FE:CD:4D:C3`:

- Zigbee `connected=1`, `factoryNew=0`, PAN `0x6D38`, channel `11`, stable short address `0x8717`.
- Four manual ZDO NodeDescriptor requests to coordinator NWK `0x0000`, at intervals over a 95-second test, **all** returned `status=0x00` and `descriptor=1`. This confirms actual bidirectional Zigbee communication; it is not merely a cached NVS association.
- No new `Leave`, `steering failed`, short address churn or fatal restarts occurred in the 95-second test.
- EP1 ZCL battery attributes were updated from the ADC: `0x0001/0x0020=39` (3.9 V), `0x0001/0x0021=122` (61%), `0x0B04/0x0100≈3872` (3.872 V with multiplier 1/divisor 1000). Local report API calls returned success; receipt of these reports in the Sprut.Hub UI remains to be verified.
- EP1 HA Simple Sensor device ID `0x000C`, EP2 Dimmable Light `0x0101`. The Sprut.Hub cached older EP1 `METER_INTERFACE` needs a fresh interview before relying on mapped services.
- Existing optical flash envelope, saved brightness and user button behavior were retained.

The full 4 MiB flash backup made before changes is retained on the PC, not committed to GitHub. Only the application at flash offset `0x10000` was written; `nvs`, `zb_storage`, `zb_fct` were preserved.

## Reproducible build — critical

The Arduino Zigbee library owns `esp_zb_init()`, so changing only application `ZigbeeLight.cpp` is too late for this setting in this SDK version.

**Do not build the compatibility firmware with ordinary `pio run`: that bypasses the prestart fix.** Instead, after PlatformIO and the Arduino-ESP32 packages have been installed:

```sh
python scripts/build_spruthub.py
```

The build script temporarily injects the opt-out call after `esp_zb_init(zb_cfg);` in the installed ZigbeeCore.cpp, compiles the project, and then restores **the exact original library bytes**, even if the build fails. It validates that the expected SDK markers are present and refuses unknown framework variants. The project uses `-D SPRUT_TC_COMPAT_MODE`. The script prints a firmware SHA-256.

The tested build used Arduino-ESP32 3.3.11 and the ESP32-H2 PlatformIO environment. The actual test-build workstation needed local overrides for predownloaded Espressif packages due unreliable access to GitHub downloads. Those workstation-specific overrides are **not** part of this repository.

## Serial diagnostics at 115200

Type `help`, `status`, `battery`, `sample`, `report`, or `ping` followed by newline. The `ping` command sends a ZDO NodeDescriptorRequest to coordinator NWK `0x0000`. A success reply looks like:

```
[ZDO-PING] sent NodeDescriptorRequest to coordinator 0x0000
[ZDO-PING] coordinator reply status=0x00 address=0x0000 descriptor=1
```

`[NET]` reports PAN/channel/short-address/factory-new status; `[BAT]` shows ADC values and `[ZCL]` indicates reporting call result. Opening ESP32-H2 USB CDC on COM3 may reset the chip, so distinguish reset-generated logs from radio disconnections.

## Sprut.Hub still requiring live verification

- Perform a fresh Zigbee interview of the joined device and confirm EP1 Simple Sensor, EP2 Dimmable Light and current battery attributes.
- Read/show 61% battery, 3.87 V and verify EP2 OnOff/LevelControl, including a restart test.
- The custom template `templates/spruthub/Doobsky-DBL-BAT-TEST.json` was corrected and passes strict validation against `Templates/schema.json` extracted from the user's Sprut.Hub DevInfo archive (2026-10-19). It maps EP2 OnOff and CurrentLevel to Lightbulb, EP1 BatteryPercentageRemaining to BatteryLevel and low-battery status (raw threshold 40 = 20%), and EP1 DCVoltage to C_VoltMeter. **Validation is static only**: it has not yet been installed/tested in Sprut.Hub, so its runtime rendering, scaling and controls must be checked there. Use Catalog -> Zigbee -> custom template upload; do not edit templates on the hub flash filesystem.
- Network association and four coordinator responses are proved; fully working UI mapping and long-term (>95 s) availability are not yet proved.

Do not repeatedly factory reset the beacon or erase the Zigbee network as a debugging shortcut. Zigbee short addresses are assigned by the network and should not be pinned in firmware.
