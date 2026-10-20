> **Historical reference — not current build instructions.** For active firmware see README.md and docs/zigbee3-conformance-20261020.md. Obsolete TEST and POWER-EXPERIMENT templates were removed from the active tree.

# Doobsky ESP32-H2 / Sprut.Hub integration (2026-10-20)

This integration branch combines diagnostics/beacon-output-20261020 with the Sprut CONTROL-v3 template and fixes the dangerous OnOff/LevelControl correction race. No flashing, factory reset, Zigbee NVS erase or hub configuration change was performed.

## Firmware

Arduino-ESP32 3.3.12 ZigbeeDimmableLight::setLightLevel(level) calls setLight(oldOn, level), which also writes OnOff. The previous deferred zero-brightness correction could therefore overwrite a newer remote ON command. The corrective setter is removed. A portable pure-state transition (include/light_state_transition.h) remembers nonzero brightness with OFF and handles ON+0 as dark, without writing ZCL.

Main loop drains a lightweight [ZCL-OBS] event recorded by the Zigbee callback. The callback does not perform Serial IO. The stock callback exposes neither source-cluster ID nor TSN, so the diagnostic explicitly reports tsn=unavailable; it cannot prove RF command delivery.

EP1 and EP2 Basic clusters now publish software build version in SWBuildID (attribute 0x4000). Initial battery sampling occurs before enabling the saved optical load. Invalid measurements still use Zigbee unknown sentinels; they are not 100 percent charge.

## Reproducible profiles

The Sprut.Hub profile is esp32-h2-supermini-end-device. Build ONLY with:
    python scripts/build_spruthub.py

Plain pio run fails closed. The wrapper checks and applies the optional R21 TCLK exchange workaround after esp_zb_init, restores framework bytes even on failure, prevents concurrent SDK writes and produces build/firmware-manifest.json with SHA-256. Zigbee network AES encryption remains enabled. GitHub CI uses the wrapper.

The comparison-only esp32-h2-standard profile does NOT include the TCLK compatibility workaround. Do not install it on the paired hub.

Pinned components: pioarduino 55.03.312-1, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5, bundled Zigbee SDK 1.6.8. SDK 2.x is not used.

## Sprut.Hub templates

CONTROL-v3 is the runtime-accepted array-link template. CONTROL-v4-POWER-EXPERIMENT differs only by explicit powerMode=POWER and name. This is an unproven hypothesis for the sleepy/BATTERY classification and should not be installed automatically. The old TEST template is retained only for history. The hub JSON Schema and Jackson loader disagree about link objects and array scale; successful JSON validation alone is not sufficient.

USB-only readings of BatteryPercentageRemaining=255 and DCVoltage=-32768 are Zigbee unknown values. Sprut UI showing 100 percent and 0 V is not a valid hardware measurement.

## Pre-flash and hardware verification

1. Preserve the full flash backup and Zigbee NVS. BOOT GPIO9 must be released before powering up. USB COM3 can trigger USB_BOOT during a reset.
2. Flash only application at 0x10000 on explicit approval after reviewing this firmware. Do not use factory.bin or erase NVS.
3. Confirm SWBuildID and fresh coordinator ZDO reply.
4. Verify ON/OFF and brightness 1, 50, 100 percent from Sprut; compare ZCL attributes and physical GPIO10 PWM/LED output. Repeat local BOOT toggle and check Zigbee feedback.
5. Verify battery vs USB-only readings, power rails under flash load and brownout behavior.
6. Stress repeated interleaved local/remote commands, network rejoin, restart and long-term radio availability.

A successful build alone is not proof of runtime compatibility. Subsequent beta.1 hardware testing confirmed PAN retention and local toggling, but Sprut.Hub remote OnOff control and long-term operation remain unverified.

## 20 October 2026 — ZDO ping burst regression guard

During live serial testing a faulty host capture script emitted 34
NodeDescriptorRequests in under one second. The ESP Zigbee 1.6.8/ZBOSS APS
queue asserted at aps/aps_main.c:927 and the MCU rebooted. The firmware
then restored the same PAN 0x6D38, short 0x8717 and factoryNew=0.
A corrected single-ping test received status 0x00 from the coordinator,
and a 60-second local toggle OFF -> ON -> OFF test had no new resets.

Version 0.8.3-beta.2-sprut rate-limits the manual serial ZDO-PING command
to at most one enqueue per 15 seconds and throttles log warnings. The
limitation applies only to diagnostics: regular Zigbee device operation,
network security and commissioning are unchanged. A hardware stress
test of this guard is required before considering it production-stable.
