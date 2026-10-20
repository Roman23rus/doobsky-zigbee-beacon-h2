# Zigbee 3.0 conformance improvements (2026-10-20)

This is a **Zigbee 3.0 conformance-oriented engineering implementation**, NOT a
Zigbee-certified product. Certification requires the CSA test program, real
product manufacturer code and over-the-air protocol tests.

## Profiles / security

Two separate builds are mandatory, to avoid disguising a compatibility exception
as normal Zigbee 3.0 security:

| Build | PlatformIO env | TCLK exchange |
|---|---|---|
| Zigbee 3.0 strict (default) | esp32-h2-zigbee3-strict | REQUIRED (true) |
| Sprut.Hub backward-compatible | esp32-h2-sprut-compat | NOT required (false) |

Build:
    python scripts/build_firmware.py --profile strict
    python scripts/build_firmware.py --profile sprut

The wrapper patches ZigbeeCore **after esp_zb_init and before esp_zb_start**,
then restores its original bytes. GitHub Actions independently builds both.
NWK AES encryption remains enabled in either profile. The Sprut profile
does NOT claim strict Zigbee 3.0 compliance: Zigbee 3.0 TCLK renewal is
expected after a join to a capable Trust Center. Do not flash the strict
variant onto an existing Sprut.Hub-paired node without deliberate testing.

Zigbee stack: Arduino-ESP32 3.3.12, pioarduino 55.03.312-1; ESP Zigbee
SDK 1.6.8. No SDK 2.x migration was attempted.

## Descriptor and cluster matrix

EP1 / HA profile 0x0104 / Simple Sensor 0x000C:
- 0x0000 Basic SERVER: standard version, manufacturer, model DBL-BAT,
  battery source, short profile-specific SWBuildID.
- 0x0003 Identify SERVER and CLIENT.
- 0x000F Binary Input SERVER: PresentValue = battery ADC sample is in
  configured valid voltage window, StatusFlags.Fault if invalid.
  This is **not** a certified physical battery-presence switch:
  a deeply discharged cell can report invalid.
- 0x0001 Power Configuration SERVER: battery voltage units 100 mV,
  battery percent in HALF-percent, Zigbee unknown sentinel 0xFF.
- 0x0B04 Electrical Measurement SERVER: DCVoltage integer mV,
  multiplier 1/divisor 1000, unknown 0x8000. MeasurementType 0x40.
- Existing optional 0x0019 OTA Upgrade CLIENT is retained.

EP2 / HA profile 0x0104 / Dimmable Light 0x0101:
- Mandatory SERVER clusters 0x0000 Basic, 0x0003 Identify, 0x0004 Groups,
  0x0005 Scenes, 0x0006 OnOff, 0x0008 Level Control, provided by the
  stock Arduino ZigbeeDimmableLight implementation.
- Application device version = 2; local dimmer range 1..254;
  no background CurrentLevel corrective write that could overwrite OnOff.
- Basic PowerSource = battery set **before endpoint registration**, consistent
  with EP1's battery Basic value. RxOnWhenIdle = true despite rechargeable
  battery source; battery-powered does NOT mean a sleepy receiver.
- The stock library may expose unsupported ZCL values from incoming peers.
  A fully certified device still needs automated invalid-value/command tests.

ZDO Power Descriptor: rechargeable battery, RX on when idle, power level
bands 0/33/66/100 derived from sampled battery. Numeric Level 0xFF and
DCVoltage 0x8000 are ZCL 'unknown', not zero or full charge.

## DIY commissioning and security limitations

This is a self-built device for personal use with a known home coordinator.
No factory provisioning or generated Install Code script is bundled.
The firmware currently does NOT implement per-device install-code
provisioning and must not claim full Zigbee 3.0 certification.
Use the strict TCLK-security mode only with a compatible Trust Center;
use the compatibility mode when required for existing Sprut.Hub pairing.

## To finish Zigbee 3.0 compliance testing

- Test factory-new join in strict security mode with Trust Center demanding
  a TCLK update, including secured rejoin, key expiry and Leave behavior.
- Verify complete Basic, Identify, Groups, Scenes, OnOff, Level Control and
  Binary Input mandatory attribute/command PICS using over-the-air
  Zigbee sniffer and a standards conformance test harness.
- Test LevelControl current level 0x00/0xFF invalid requests, transitions,
  reports after local switch, group commands, scene restore, Default Response.
- Verify battery reporting, Power Descriptor, unknown sentinels, bindings,
  link quality, non-sleepy polling, reboot/network recovery and resilience
  under heavy but legal Zigbee loads.
- Confirm manufacturer's approved ID, firmware update / OTA behavior, serial
  persistence, install-code manufacturing path and accurate power descriptors.

**Do not flash onto the paired ESP32-H2 yet**. Only the previous beta.2 was
tested on PC/COM3 with Sprut.Hub; these new images change the advertised
endpoint/cluster structure, and strict mode may trigger a new TC handshake.
Preserve the 4MB backup / Zigbee NVS. Use separate test hardware or request
specific permission before performing app-only flashing at 0x10000.

## References

- NXP Dimmable Light: https://mcuxpresso.nxp.com/mcuxsdk/25.09.00/html/middleware/wireless/zigbee/Docs/JNUG3131/topics/dimmable_light.html
- ZBOSS Simple Sensor: https://software-dl.ti.com/simplelink/esd/simplelink_lowpower_f3_sdk/latest/exports/docs/third_party/zboss_r23/doxygen/html/group__ha__simple__sensor.html
- Silicon Labs Security: https://docs.silabs.com/zigbee/8.2.2/zigbee-security/03-standard-security
- Silicon Labs install-code CRC: https://docs.silabs.com/zigbee/9.1.2/using-installation-codes-with-zigbee-devices/04-installation-code-crc
- Espressif Security API 1.x: https://docs.espressif.com/projects/esp-zigbee-sdk/en/release-v1.0/esp32/api-reference/esp_zigbee_secur.html
