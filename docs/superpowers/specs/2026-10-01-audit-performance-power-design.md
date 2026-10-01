# Doobsky Zigbee Beacon: performance, power, and Zigbee isolation design

Date: 2026-10-01

## Goal

Refactor and optimize the ESP32-H2 firmware for minimum control latency and lower background power consumption without changing the lighthouse optical behavior. The physical beacon must continue to reproduce the existing `Fl(3).W.16.5s` pattern with the same timing, 20 kHz PWM, 1 ms `sin²` envelope LUT, and WS2812 mirror.

The Zigbee light must behave as a normal static dimmable lamp in the coordinator UI. Physical flashing is an implementation detail and must never change Zigbee On/Off or Level attributes during the 16.5 s lighthouse cycle.

## Fixed product decisions

- Zigbee remains an always-listening End Device; no sleepy mode in this change.
- `rx_on_when_idle=true` will be set explicitly before Zigbee startup.
- Fast coordinator response has priority over maximum battery lifetime.
- The blue GPIO13 status LED breathes only while joining/searching, may blink for Identify, and is OFF after a successful connection.
- The onboard WS2812 on GPIO8 remains a real-time visual mirror of the lighthouse flash because it is currently the only practical flash monitor before the external boost converter arrives.
- The external beacon timing and visual envelope must not change.
- Battery telemetry remains standards-based Zigbee HA/ZCL telemetry and must not be attached to the light endpoint.

## Audit summary

The current firmware is functionally compact but concentrates nearly all responsibilities in `src/main.cpp`: Zigbee endpoints, optical timing, GPIO/PWM, WS2812, battery sampling/reporting, local button handling, status LED, NVS persistence, and startup/error handling.

The optical path is already strong. It uses absolute `esp_timer_get_time()` time, corrects whole-cycle overruns without cumulative drift, uses a precomputed 354-sample LUT instead of runtime trigonometry, and keeps battery/NVS work out of the flash window. Those properties must be preserved.

The current Zigbee callback is short and defers physical output changes, which is also good. However, the separation between logical Zigbee state and physical flash state is only a convention inside one large translation unit. The architecture does not make it impossible for future optical changes to update Zigbee attributes. This change will make that boundary structural and testable.

The main loop currently executes continuously with `delay(1)`, invoking state, beacon, button, indicator, battery, and persistence services even during long dark intervals or while the beacon is OFF. This is unnecessary wakeup/work. The design replaces periodic high-rate application polling with event-driven task wakeups while preserving always-on Zigbee radio reception.

The current startup performs a blocking 32-sample ADC battery read before Zigbee initialization. Runtime sampling is already incremental; startup sampling will use the same non-blocking model so Zigbee startup is not delayed by ADC work.

The blue status LED is currently held ON while Zigbee is connected. It will instead be OFF in the steady connected state.

Tests cover important source-level regressions but there is no repository CI and most tests inspect source text rather than validating a real firmware build. CI will run both the regression suite and a PlatformIO ESP32-H2 build.

## Architecture

### 1. BeaconEngine: physical optics only

`BeaconEngine` owns the physical lighthouse output:

- GPIO10 LEDC PWM at 20 kHz.
- GPIO8 WS2812 flash mirror.
- The existing `Fl(3).W.16.5s` schedule.
- The existing `FLASH_ENVELOPE_LUT` and 1 ms optical samples.
- The absolute cycle epoch and next optical deadline.

`BeaconEngine` has no Zigbee dependency and must not include `Zigbee.h`. Its public control surface is intentionally small:

- enable/disable the beacon;
- set peak brightness level;
- notify/wake the engine when the requested logical state changes.

It never writes Zigbee attributes, never sends Zigbee reports, and never calls a Zigbee endpoint.

The engine preserves the current semantics:

- OFF forces PWM and WS2812 to zero immediately;
- an OFF -> ON transition starts the first flash immediately;
- a brightness change while ON changes peak output without restarting the 16.5 s cycle;
- a duplicate ON while already ON does not restart the cycle;
- delayed execution uses absolute time and selects the correct LUT sample rather than accumulating drift.

### 2. ZigbeeLight: logical lamp only

`ZigbeeLight` owns EP2, a standard HA Dimmable Light. Its persistent logical state is only:

- `on`;
- `level` (0..254).

The coordinator sees those values as a normal lamp. While the beacon physically goes from zero output to a flash peak and back to zero, the Zigbee On/Off attribute remains ON and CurrentLevel remains the user's requested peak brightness.

A Zigbee light callback performs no optical work, no ADC work, no NVS write, and no logging. It stores the latest logical command and wakes `BeaconEngine` using a task notification. Zigbee therefore has one-way control over the optical engine:

`Zigbee -> logical shadow state -> BeaconEngine`

There is no `BeaconEngine -> Zigbee` path.

A local BOOT-button toggle may update the logical Zigbee lamp once so the coordinator can reflect the user's local action. It must not expose individual flashes.

### 3. Event-driven optical scheduling

The existing polling loop is replaced by a dedicated `BeaconEngine` FreeRTOS task. The Espressif Zigbee task remains higher priority than the beacon task.

Target priority order:

1. Espressif Zigbee task: existing priority 5.
2. BeaconEngine task: priority 4.
3. Low-priority housekeeping/battery work: below the beacon task.

The beacon task blocks whenever there is no optical work. It wakes for either:

- a logical light-state notification from Zigbee/local control; or
- the next optical deadline.

To avoid doing hardware work in timer callback context, any `esp_timer` used for precise wakeups only notifies the BeaconEngine task. LEDC and WS2812 writes remain in task context.

During a 353.571 ms flash, the engine continues using the same 1 ms LUT. During the 3.064286 s and 9.310715 s dark intervals, it blocks until the next deadline unless a new control command arrives first. When OFF, it blocks until a command arrives.

This reduces application CPU activity without changing Zigbee radio availability. No claim is made that this change can approach sleepy-device battery life; the always-on 802.15.4 receiver remains the dominant radio cost.

### 4. BatteryTelemetry: low-priority background work

EP1 remains standards-based battery telemetry with Power Configuration and Electrical Measurement/DCVoltage. The existing unknown-value behavior is preserved:

- invalid battery percentage -> `0xFF`;
- invalid battery voltage -> `0xFF`;
- invalid DCVoltage -> `0x8000` / `INT16_MIN`.

Sampling remains 32 ADC samples, but startup uses the same incremental/non-blocking sampler as runtime. Battery work is permitted only outside the optical critical window. Reports remain change-driven plus periodic, with retries on failure.

Where the calculation result can be preserved exactly, battery scaling will use integer/fixed-point arithmetic instead of float. No calibration behavior will be changed as part of this optimization.

### 5. Local UI and persistence

`main.cpp` becomes orchestration plus low-rate housekeeping rather than the optical hot path.

BOOT/GPIO9 keeps the same behavior:

- short press toggles logical lamp ON/OFF;
- 5 s hold performs Zigbee factory reset.

NVS persistence keeps delayed/coalesced writes. Flash timing must never wait for NVS.

GPIO13 behavior becomes:

- joining/searching: current breathing indication;
- Identify: temporary identify indication;
- connected steady state: OFF;
- disconnected after previously being connected: joining/search indication may resume when detected.

The WS2812 behavior is unchanged.

## Proposed file layout

```text
src/
  main.cpp
  beacon_engine.cpp
  zigbee_light.cpp
  battery_telemetry.cpp

include/
  config.h
  flash_envelope_lut.h
  beacon_engine.h
  zigbee_light.h
  battery_telemetry.h
```

`main.cpp` owns startup ordering, button/status/persistence housekeeping, and composition of the three modules. Module interfaces must not leak unnecessary implementation details.

## Timing invariants

These values are frozen by this change:

```text
CYCLE_US       = 16,500,000
FLASH_US       =    353,571
SHORT_DARK_US  =  3,064,286
LONG_DARK_US   =  9,310,715
PWM            = 20 kHz
Envelope step  = 1 ms
LUT size       = 354 samples
```

The existing LUT values must remain byte-for-byte equivalent unless a future, separately reviewed lighthouse-accuracy change intentionally replaces them.

## Zigbee invariants

- EP2 remains HA Dimmable Light, device ID `0x0101`.
- EP1 remains separate battery telemetry; no battery cluster is attached to EP2.
- No vendor/model spoofing is introduced.
- Physical PWM/WS2812 updates cannot alter EP2 On/Off or Level attributes.
- EP2 On/Off changes only for coordinator commands, local toggle, restored startup state, or an intentional application state change.
- EP2 Level represents requested peak flash brightness, not instantaneous optical output.
- `rx_on_when_idle=true` is explicit before Zigbee startup.

## Error handling

Safety behavior is fail-dark where practical:

- LEDC/PWM initialization failure: beacon output remains OFF and firmware enters/restarts through the existing fatal path.
- Beacon task or precision timer creation failure: keep output OFF, log the fatal error, restart rather than run an un-timed fallback pattern.
- Zigbee endpoint configuration/start failure: preserve current restart behavior.
- Invalid battery measurement: publish ZCL unknown sentinels rather than fabricated values.
- Battery report failure: keep telemetry dirty and retry later; never block optical timing.
- Multiple fast Zigbee commands: store the latest logical shadow state and wake the engine. The engine applies the latest state promptly; task notification is a wake mechanism, not the authoritative state store.

## Performance rules

The following operations are forbidden from the 1 ms optical hot path:

- `Serial` output;
- NVS writes;
- ADC reads;
- Zigbee attribute/report operations;
- dynamic allocation;
- floating-point trigonometry;
- blocking delays.

The hot path may use absolute timer reads, LUT lookup, integer scaling, LEDC write, and the existing throttled WS2812 mirror update.

## Testing strategy

### Optical regression

Tests must prove that refactoring does not change:

- all timing constants;
- LUT length and values;
- OFF -> ON immediate first flash behavior;
- duplicate ON behavior;
- brightness changes not restarting the cycle;
- phase recovery after delayed execution.

A complete simulated 16.5 s cycle should produce the same logical PWM/LUT sequence as the pre-refactor implementation at the defined sample points.

### Zigbee isolation regression

Tests must prove that:

- BeaconEngine does not include or call Zigbee APIs;
- repeated PWM/WS2812 changes cannot update Zigbee On/Off or Level;
- Zigbee remains logically ON for the full physical flash cycle;
- CurrentLevel remains requested peak brightness throughout the physical cycle;
- local toggle updates the logical lamp once, not once per flash.

### Performance regression

Tests preserve/enforce:

- no runtime trigonometry;
- no logging/blocking I/O in Zigbee callbacks;
- no ADC/NVS/Zigbee reporting in the optical hot path;
- dark intervals and OFF state use blocking/event waits instead of a 1 ms application polling loop;
- Zigbee task priority remains above the beacon task;
- always-on Zigbee is explicit.

### Battery regression

Existing standards/unknown-value/reporting tests remain, adjusted to module boundaries as needed.

### GitHub CI

Add GitHub Actions for every pull request and relevant push:

1. run Python `unittest` regression tests;
2. install PlatformIO;
3. run the real ESP32-H2 PlatformIO build;
4. fail CI on any test or build failure.

Hardware timing/current measurements remain a post-build bench validation and are not replaced by CI.

## Bench validation after implementation

Before merging, flash the feature branch to the ESP32-H2 and verify:

- pair/rejoin succeeds;
- Yandex exposes a normal lamp;
- lamp UI remains steadily ON while WS2812 performs the three-flash lighthouse cycle;
- ON starts the first flash immediately;
- OFF extinguishes output immediately;
- brightness changes peak flash brightness without changing the pattern;
- blue LED turns OFF after successful connection;
- WS2812 flash appearance is indistinguishable from the current firmware;
- factory reset and BOOT short press still work;
- battery invalid/valid telemetry behavior remains correct.

Power measurements will be repeated after the external boost converter arrives. Sleepy Zigbee is explicitly out of scope and can be evaluated later as a separate change.

## Git workflow

Implementation will stay on `feature/audit-performance-power` and will not write directly to `main`. Tests are written or strengthened before implementation changes. After unit tests, PlatformIO build, and hardware verification pass, changes are presented as a pull request into `main` with an audit summary, measured evidence, and deferred items.

## Success criteria

The change is complete only when all of the following are true:

- lighthouse timing and visible flash shape are unchanged;
- WS2812 remains the control mirror;
- coordinator sees a steady logical lamp while the physical beacon flashes;
- Zigbee commands remain immediate with always-on RX;
- connected blue status LED is OFF;
- unnecessary 1 ms application polling outside active flashes is removed;
- battery/NVS/background operations cannot disturb optical timing;
- source regression tests and a real PlatformIO build run in GitHub CI;
- bench verification passes before merge to `main`.
