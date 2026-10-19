# Sprut.Hub Zigbee diagnostics (experimental)

This branch is an instrumented build of firmware 0.8.1-alpha.1, based on `audit/full-code-optimization-2026-10-04`. It has **not yet been certified as Sprut.Hub-compatible**, because end-to-end validation requires joining a real Zigbee network.

## Endpoints

- EP1: HA Simple Sensor (`0x000C`), model `Doobsky / DBL-BAT`. Power Configuration (`0x0001`) BatteryVoltage (`0x0020`, 100 mV/LSB), BatteryPercentageRemaining (`0x0021`, 0.5%/LSB); Electrical Measurement (`0x0B04`) DCVoltage (`0x0100`, with multiplier 1/divisor 1000).
- EP2: HA Dimmable Light (`0x0101`), model `Doobsky / DBL-01`. OnOff and LevelControl.
- BatteryVoltage is readable. Periodic explicit reporting is implemented for BatteryPercentageRemaining and DCVoltage, not BatteryVoltage.

## Serial debugging

Connect to USB CDC at 115200 baud. Commands followed by newline:

- `help` — command list
- `status` — stack started/connected, PAN ID, short address, channel, factory-new state, battery and light shadow
- `battery` — battery raw Zigbee values, ADC, reporting status
- `sample` — restart incremental battery sample
- `report` — request a report on the next permitted service cycle

Logs include `[NET]`, `[BAT]`, `[ZCL]`, `[DIAG]` and SDK Zigbee core status messages. Serial commands are non-blocking and do not invoke factory reset.

At boot battery ADC is sampled and initialized in EP1 before Zigbee begins, to prevent reporting cached `unknown` values during initial interview.

## Observed 2026-10-19

An actual ESP32-H2 on COM3 completed build/flash and reported valid battery readings (~3.87 V, 61%). Diagnostic snapshot:

```
[NET] started=1 connected=0
[NET] pan=0xFFFF short=0xFFFE channel=255 factoryNew=1
Network steering was not successful (status: ESP_FAIL)
```

This means the device had no usable Zigbee association at the time of diagnosis. The previously interviewed Sprut.Hub device entry is stale (EP1 previously Meter Interface); a new interview is required **after** commissioning.

## Commissioning / compatibility validation

1. In Sprut.Hub, open Controllers > Zigbee > Sprut zigbee (coordinator) > three dots > Search for devices, **without removing existing accessory tiles**.
2. Keep the beacon powered, within range, and monitor COM3 for successful network steering, PAN/channel/address changes, and `connected=1`.
3. Re-read device structure: EP1 Simple Sensor and EP2 Dimmable Light; battery attributes should no longer be `0xFF`/invalid.
4. Test reading battery voltage/percentage, report delivery/binding, and EP2 OnOff/brightness including after reboot.
5. Only then test the **draft** custom template `templates/spruthub/Doobsky-DBL-BAT-TEST.json`. It is structurally valid JSON but has not been loaded/validated in Sprut.Hub.

Do not erase whole flash or issue `Zigbee.factoryReset()` as a generic repair step. The live serial probe captured a `factoryNew=1` condition already.

The device flashing process uses only the active application slot, keeping `nvs`, `otadata`, `zb_storage`, and `zb_fct` unchanged. A full 4 MiB pre-flash backup and the serial log were retained locally on the host PC, not added to GitHub.
