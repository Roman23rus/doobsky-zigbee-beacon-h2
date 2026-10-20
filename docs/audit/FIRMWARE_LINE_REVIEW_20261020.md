# Post-line-by-line firmware audit — Doobsky ESP32-H2 (2026-10-20)

Reviewed integration head: `73afaaa5706517d2f27c8f94368fbc44fce64a1a`.
Cleanup branch: `cleanup/firmware-line-audit-20261020`.
Firmware version after audit: **0.9.0-alpha.2**.

## Scope

All runtime source files in `src/` and all seven headers in `include/`
were read in full, including the PWM timer/RTOS hot path, Zigbee callbacks,
persistent preferences, battery ADC ZCL reporting, descriptors and security.
The PlatformIO environment, build guard/wrapper, active CI workflow,
Sprut.Hub templates, user documentation and tests were cross-checked.
Tests and two full ESP32-H2 builds were rerun after cleanup.

This audit distinguishes code reachable in the firmware from historical
project documentation and bundled Arduino/Espressif library sources.
Old commits and change history are intentionally retained in Git.

## Verified cleanup (change set)

1. `src/main.cpp` — removed duplicate OFF+CurrentLevel=0 remediation.
   `ZigbeeLight::handleLightChange()` already calls the pure
   `applyLightAttribute()` helper. No longer two authoritative policies.
2. `src/zigbee_light.cpp` / `include/zigbee_light.h` — removed redundant
   post-start ZCL Basic.PowerSource setter, since
   `setInitialBatteryPowerSource()` initializes the Basic attribute before
   endpoint registration. Kept the separate ZDO node descriptor sync in
   `syncZigbeePowerDescriptors()`.
3. `src/main.cpp` — removed the early NVS brightness rewrite described as
   a migration for firmware before 0.8. Invalid stored values still normalize
   to default and are repaired later via the guarded normal NVS write path.
4. `src/zigbee_light.cpp` — removed an unused `esp_zigbee_secur.h` include.
   Security configuration remains in the scoped Arduino ZigbeeCore patch.
5. `scripts/`, `platformio.ini`, `tests/` and CI — renamed misleading
   Sprut-only builder/guard names to `build_firmware.py` and
   `require_profile_build.py`, and the compat environment to
   `esp32-h2-sprut-compat`. The strict security build is distinct.
6. `templates/spruthub/` — removed obsolete `TEST` and unverified
   `CONTROL-v4-POWER-EXPERIMENT` from the active tree. Retained only
   runtime-tested format `CONTROL-v3` (whether remote commands work is a
   separate outstanding hardware test).
7. `scripts/` and `tests/` — removed the unused manufacturing Install
   Code generator and test. The personal-use firmware does not provision
   install codes; it does NOT claim certification.
8. Corrected README EP1 descriptor from stale Meter Interface 0x0053 to actual
   Simple Sensor 0x000C with Basic/Identify/BinaryInput clusters.
9. `CHANGELOG.md`, `README.md`, `CONTRIBUTING.md`, CI and pull-request
   templates — repaired stale command names, versions and duplicate or
   misleading historical descriptions.
10. Added `tests/test_legacy_cleanup.py` to reject accidental reintroduction
   of the same dead-code paths or obsolete files.
11. Incremented firmware identity and application device version to reflect
    the advertised cluster revisions.

## Deliberately retained — NOT leftover code

- `BeaconEngine` and `FLASH_ENVELOPE_LUT`: active 16.5-second, three-flash
  optical sequencer, smooth PWM envelope, timer deadline scheduling.
- `RGB_LED_PIN` and `STATUS_LED_PIN`: mirror and network/Identify feedback.
  Red idle glow when optical output is enabled is the current intended UX.
- `ESP NVS` Preferences: stores last valid brightness and OnOff, coalesces
  flash writes into safe dark windows; keeps existing user settings.
- `BatteryTelemetry` and the EP1 Simple Sensor: PowerConfiguration,
  ElectricalMeasurement and BinaryInput/Identify service current Zigbee
  descriptors and the battery ADC path.
- `ZigbeeLight` and EP2 Dimmable Light: Groups, Scenes, OnOff and LevelControl
  come from the upstream Arduino wrapper. Keeping the wrapper avoids a
  risky SDK 2.x port.
- Rate-limited manual ZDO `ping`, serial `status`/`battery`/`sample`/
  `report`/`toggle`, and deferred `[ZCL-OBS]` capture: still useful to
  isolate unresolved Sprut.Hub remote-command delivery and do not perform
  autonomous Zigbee polling. Production debug policy may be further reduced
  after live hub control is confirmed.
- Physical BOOT button short-press and intentional 5-second Zigbee factory
  reset path; retained as the current recovery mechanism.
- Two explicit security profiles: strict TCLK exchange vs Sprut compatibility
  exception. Both preserve NWK AES. The latter is NOT certified Zigbee 3.0.

## Unresolved or hardware-only issues (not stale leftovers)

- Sprut.Hub remote ON/OFF, LevelControl, Scenes/Groups behavior has not
  been proven with current alpha firmware. Local serial toggle alone does
  not constitute remote Zigbee command testing.
- Unknown battery sentinels 0xFF and 0x8000 may still display misleadingly
  as 100%/0V in the SprutHub template/UI; firmware does not invent readings.
- Physical optical envelope (reconstructed subpulse durations), regulator
  and battery-load accuracy require measurement on assembled hardware.
- `esp_zb_secur_link_key_exchange_required_set(false)` in the Sprut
  compatibility build is an explicit network compatibility concession.
- No actual per-device install-code provisioning, independent radio/ZCL
  conformance suite or CSA certification, and none is required for this DIY
  project.
- The bundled Arduino framework compiles numerous endpoint implementation
  units; this is normal and does not imply unused application logic is
  linked into the firmware.

## Validation and deployment safeguards

- Automated tests plus strict and compat PlatformIO builds.
- Build manifest records both SHA-256 hashes and the security profile.
- ZigbeeCore source restored byte-for-byte after the scoped security patch.
- No factory reset, COM3 flashing, app-NVS erase or Zigbee re-pair performed.
- Keep the 4MB pre-flash image until the two-endpoint interview, remote
  ON/OFF/brightness, persistence and battery reporting pass on hardware.
