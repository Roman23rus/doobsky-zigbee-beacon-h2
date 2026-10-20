# Doobsky ESP32-H2 — state/reports fixes (2026-10-21)

Firmware source candidate: **0.9.0-alpha.3**, derived from
`audit/deep-firmware-review-20261020` (`bff256e`).
Personal-use DIY project, not CSA-certified.

## Runtime changes

1. **Safe Dimmable Light endpoint** (`include/zigbee_light.h`,
   `src/zigbee_light.cpp`).
   The stock Arduino `ZigbeeDimmableLight::setLight()` invoked
   `lightChanged()` BEFORE validating two ZCL attribute writes and
   could therefore switch the physical lamp even after a failed Zigbee
   operation. The custom endpoint retains the same HA profile,
   device ID, endpoint 2 and mandatory server clusters
   Basic/Identify/Groups/Scenes/OnOff/LevelControl.
   Local writes are serialized under a bounded Zigbee lock, apply both
   attributes and roll back OnOff if LevelControl fails. ZCL attributes
   are read back under that lock and the optical callback is invoked
   only after observed state has changed.
   **Partial rollback failure** is still observable: the code reconciles
   the physical lamp to the actual ZCL attribute values and returns
   failure; it cannot guarantee two separate ZCL writes are a perfect
   distributed transaction.
2. **Local-change reports**: OnOff and CurrentLevel are queued
   independently after an accepted local button operation and retried
   with capped exponential delay if a request was not enqueued.
   Queued reports are not proof of coordinator delivery.
3. **Light telemetry**: 12-record bounded ring, overwrite counter;
   ZDO callbacks publish only scalar data and main loop prints them.
   Inbound LevelControl 0xFF (unknown) preserves existing brightness
   instead of switching to full output.
4. **Battery snapshot** (`src/battery_telemetry.cpp`):
   PowerConfiguration voltage/percentage, ElectricalMeasurement
   DCVoltage and BinaryInput present/status are updated during
   one bounded Zigbee lock; failed updates are retried later.
   This prevents interleaving during snapshot updates.
5. **Battery report queue** (`include/zcl_report_queue.h`):
   tracking, enqueue retry and error backoff are independent for
   percentage and DC voltage. If one queues successfully and the other
   fails, only the unsuccessful one is retried.
   Reports are zero-initialized before calling ZBOSS, unlike upstream
   Arduino 3.3.12 `ZigbeeElectricalMeasurement::reportDC()`.
   On reconnection a fresh battery report cycle is requested.
6. **Firmware provenance**: build manifest marks a build from a dirty
   tracked working tree. Strict and Sprut profiles remain distinct,
   including the exact same optional TCLK workaround that was used by
   the existing paired device. Zigbee NWK AES remains enabled.

## Tests and risks

- Portable host-side C++ fixture covers a successful local transaction,
  rejection at step 1, failure at step 2 with rollback, rollback failure,
  and isolated battery report retries/backoff.
- Python regression tests check endpoints, callback paths, lock
  bounds and no return to the precommit Arduino setter.
- Both profiles must compile independently in GitHub Actions.
- Full physical verification **not performed**: coordinator delivery,
  Sprut.Hub OnOff and LevelControl commands, Groups/Scenes, GPIO10
  waveform, battery reporting and recovery under real RF loss need testing.
- Sprut.Hub unknown-sentinel display (255 and -32768) remains a
  **hub template/runtime problem**. Do not invent undocumented JSON
  operators to hide or clamp these measurements.
- ESP32-H2 device currently installed with **0.8.3-beta.2-sprut**.
  Do not overwrite it yet; maintain the complete 4 MB backup, and
  never erase Zigbee NVS when planning app-only firmware trials.
