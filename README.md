# Doobsky Zigbee Beacon — ESP32-H2 SuperMini

Firmware for a 12 V LED replica of the Doobsky lighthouse light characteristic, controlled over Zigbee 3.0.

## What it does

- EP1 is a Zigbee HA **Meter Interface** (profile `0x0104`, device `0x0053`) for battery telemetry.
- EP1 uses **Power Configuration `0x0001`** for `BatteryVoltage` and `BatteryPercentageRemaining`.
- EP1 uses **Electrical Measurement `0x0B04` / DCVoltage `0x0100`**, with multiplier/divisor `1/1000` for millivolt resolution.
- EP2 is a Zigbee HA **Dimmable Light** (profile `0x0104`, device `0x0101`).
- Standard clusters only; no manufacturer-specific clusters and no spoofed third-party model IDs.
- `ON` starts a local, drift-free `Fl(3).W.16.5s` cycle.
- `OFF` immediately forces PWM to zero.
- Zigbee brightness controls peak flash brightness.
- Each flash uses a raised-cosine (`sin²`) optical envelope to imitate a rotating Fresnel/EMV-3 beam rather than a hard electronic blink.
- Uses the ESP32-H2 hardware LEDC PWM at 20 kHz.
- Uses only **on-board UI** on the ESP32-H2 SuperMini:
  - BOOT / GPIO9: short press = local toggle, hold 5 s = Zigbee factory reset.
  - Blue user LED / GPIO13: Zigbee status. WS2812 / GPIO8: mirrors the lighthouse flash envelope.
- Saves On/Off and level to NVS with delayed writes.
- Default build is **Zigbee End Device**. It does not route traffic for other Zigbee nodes.
- EP1 identifies as `Doobsky / DBL-BAT`; EP2 identifies as `Doobsky / DBL-01`.

## Hardware connection

| ESP32-H2 SuperMini | External circuit |
|---|---|
| `5V` | TPS61088 boost `VIN` |
| `GND` | TPS61088 `GND`, AO3400A `Source` |
| `GPIO10` | 100 ohm -> AO3400A `Gate` |
| `GPIO1` | midpoint of 100 kOhm / 100 kOhm battery divider |
| AO3400A Gate | 100 kOhm -> GND |
| TPS61088 `12V OUT` | 12 V constant-voltage COB LED `+` |
| COB LED `-` | AO3400A `Drain` |

Recommended prototype load: 12 V constant-voltage LED module. For battery sensing connect `BAT+ -> 100k -> GPIO1 -> 100k -> GND` and add `100 nF` from GPIO1 to GND. Use 1% resistors.

### Battery monitoring

The firmware samples the 1S Li-ion cell every 60 s using 32 incremental calibrated ADC readings scheduled outside the flash window, and converts the cell voltage to an estimated 0-100% state of charge using a piecewise Li-ion discharge curve. Zigbee exposes `BatteryVoltage` and `BatteryPercentageRemaining` in Power Configuration and high-resolution `DCVoltage` in Electrical Measurement. If the ADC reading is invalid, firmware publishes the ZCL unknown sentinels (`0xFF` for the battery fields and `0x8000` for DCVoltage) instead of fabricated zero values. Battery percentage is explicitly reported when it changes by at least 2% or every 5 minutes; DC voltage uses standard attribute reporting. ADC, NVS writes, and Zigbee telemetry are deferred when a flash is active or less than 50 ms away.

## Build environment

This project uses the current pioarduino PlatformIO platform because official PlatformIO support for ESP32-H2/modern Arduino-ESP32 has historically lagged. The current pioarduino stable line uses Arduino-ESP32 3.3.x.

Default (recommended):

```bash
pio run -e esp32-h2-supermini-end-device
pio run -e esp32-h2-supermini-end-device -t upload
pio device monitor -b 115200
```

## Pairing

1. Put your Zigbee coordinator/SprutHub into device-add mode.
2. Power the ESP32-H2 SuperMini from USB-C.
3. Pairing progress is visible in USB serial log.
4. Blue LED indicates Zigbee state; the WS2812 mirrors the lighthouse flashes.
5. To erase Zigbee network data and pair again, hold **BOOT for 5 seconds**.

No external pairing button or status LED is required.

## Timing profile

The full period is exactly **16.500000 s** in firmware and phase is calculated from `esp_timer_get_time()`, so loop execution time does not accumulate cycle drift.

Current segment table:

| Segment | Duration |
|---|---:|
| Flash 1 | 353.571 ms |
| Dark | 3064.286 ms |
| Flash 2 | 353.571 ms |
| Dark | 3064.286 ms |
| Flash 3 | 353.571 ms |
| Long dark | 9310.715 ms |
| **Total** | **16500.000 ms** |

### Important accuracy note

`Fl(3).W.16.5s` and the 16.5 s period are confirmed characteristics. The individual segment values above are still an **engineering reconstruction** obtained by proportionally scaling the known EMV-3 14.0 s pattern (0.3 / 2.6 / 0.3 / 2.6 / 0.3 / 7.9 s). They are intentionally isolated in `include/config.h` so they can be replaced immediately when a primary Doobsky light-list entry with exact element durations is found.

The flash shape is a fixed `sin²(pi*x)` raised-cosine pass. Firmware uses a precomputed 1 ms duty-cycle lookup table, preserving that envelope while removing runtime trigonometry from the time-critical path.

## Pin notes for ESP32-H2 SuperMini

- BOOT: GPIO9.
- Blue user LED: GPIO13.
- Main PWM output: GPIO10.
- Battery ADC: GPIO1 via 100k/100k divider and 100 nF filter capacitor.
- Native USB is on GPIO26/27; firmware does not touch them.
- On-board WS2812: GPIO8. It is only driven after boot, after the strapping state has already been sampled.

## Firmware version

`0.7.3-alpha.1`

Alpha status is intentional: Zigbee/PWM architecture is ready, but the exact Doobsky per-element timing still awaits primary-source confirmation and the final LED/optics should be visually calibrated on the physical model.


### Green LED note
The physical green LED is the battery/charger indicator and is not connected to an ESP32 GPIO, so firmware cannot use it as an ON/OFF indicator without a hardware modification.


## Resistor power
For through-hole parts, standard 0.25 W resistors are recommended. R1/R2 (100 kOhm battery divider) should be 1%; R3/R4 may be 5%.

## Hub compatibility

The firmware uses standard Zigbee HA/ZCL clusters only. Hub UI support is coordinator-specific: direct pairing with the Yandex Zigbee hub currently exposes the Dimmable Light endpoint, while the separate battery Meter Interface endpoint may remain hidden in the Yandex app. SprutHub, ZHA, Zigbee2MQTT and other coordinators can inspect the standard EP1 clusters independently of that Yandex UI limitation.
