# Doobsky Zigbee Beacon Performance and Power Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the ESP32-H2 firmware into event-driven modules that preserve the current lighthouse flash exactly, keep Zigbee always-on and responsive, isolate physical flashing from Zigbee lamp state, turn the blue LED off after join, and add CI/build regression coverage.

**Architecture:** Split the monolithic firmware into `BeaconEngine`, `ZigbeeLight`, and `BatteryTelemetry`, with `main.cpp` acting as composition/housekeeping. Zigbee owns only the logical ON/OFF + level shadow state and can notify the optical task; the optical task owns PWM/WS2812 and has no Zigbee dependency. A dedicated priority-4 BeaconEngine task blocks until the next optical deadline or state notification while Espressif Zigbee remains priority 5 and `rx_on_when_idle=true`.

**Tech Stack:** ESP32-H2, Arduino-ESP32 3.3.11, Espressif Zigbee, FreeRTOS task notifications, `esp_timer`, LEDC, PlatformIO/pioarduino, Python `unittest`, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-01-audit-performance-power-design.md`

## Global Constraints

- Preserve `CYCLE_US=16,500,000`, `FLASH_US=353,571`, `SHORT_DARK_US=3,064,286`, `LONG_DARK_US=9,310,715`.
- Preserve 20 kHz PWM, 1 ms envelope sampling, and the current 354-value `FLASH_ENVELOPE_LUT` byte-for-byte.
- Preserve WS2812 as the flash mirror.
- Zigbee stays always-on; call `Zigbee.setRxOnWhenIdle(true)` before `Zigbee.begin(...)`.
- EP2 remains HA Dimmable Light `0x0101`; EP1 remains separate battery telemetry.
- Physical PWM/WS2812 activity must never change Zigbee On/Off or CurrentLevel.
- GPIO13 is OFF after successful Zigbee connection, except during Identify; pairing/search breathing remains.
- No sleepy mode in this change.
- No ADC, NVS, Zigbee reporting, logging, dynamic allocation, or blocking delays in the 1 ms optical hot path.
- Implementation stays on `feature/audit-performance-power`; do not write directly to `main`.

## Review Focus

- Rapid ON/OFF/level command bursts: latest logical state must win without restarting the cycle except OFF->ON.
- Delayed task wake during a flash or dark gap: absolute phase recovery must avoid drift and select the correct LUT point.
- Coordinator reconnect/join transitions: blue LED must become OFF only when connected and resume search indication when disconnected.
- Local BOOT toggle: coordinator shadow must update once per user action and never per flash.
- Timer/task creation or PWM initialization failure: output must fail dark and restart rather than run an uncontrolled pattern.

---

### Task 1: Lock optical and Zigbee-isolation behavior with failing tests

**Files:**
- Modify: `tests/test_flash_envelope_lut.py`
- Modify: `tests/test_light_profile.py`
- Modify: `tests/test_hotpath_optimization.py`
- Modify: `tests/test_performance_regressions.py`
- Create: `tests/test_architecture_isolation.py`

**Interfaces:**
- Consumes: current monolithic behavior in `src/main.cpp` and constants in `include/config.h`.
- Produces: regression contract for later module files.

- [ ] **Step 1: Add failing architecture tests**
  - `test_beacon_engine_has_no_zigbee_dependency`: require `src/beacon_engine.cpp`/`include/beacon_engine.h` and assert they contain no `Zigbee`, `setLight`, `setClusterAttribute`, or `report` calls.
  - `test_light_shadow_is_not_driven_by_pwm`: assert Zigbee light update calls do not appear in BeaconEngine.
  - `test_always_on_rx_is_explicit`: assert `Zigbee.setRxOnWhenIdle(true)` exists before `Zigbee.begin` in Zigbee startup code.
  - `test_connected_status_led_is_off`: assert connected-state status path writes GPIO13 OFF.

- [ ] **Step 2: Strengthen optical invariants**
  - Assert all timing constants and LUT size/value sequence remain unchanged.
  - Add source-level assertion that the optical module uses absolute `esp_timer_get_time()` deadlines and no `% CYCLE_US` hot-path modulo.
  - Add assertions for OFF->ON immediate start and duplicate ON not forcing a restart.

- [ ] **Step 3: Run tests and verify RED**
  - Run: `python -m unittest discover -s tests -v`
  - Expected: new module/isolation tests fail because the modules do not yet exist and explicit always-on configuration is absent.

- [ ] **Step 4: Commit tests**
  - Commit message: `test: lock beacon timing and zigbee isolation`

### Task 2: Extract BeaconEngine and make optical scheduling event-driven

**Files:**
- Create: `include/beacon_engine.h`
- Create: `src/beacon_engine.cpp`
- Modify: `src/main.cpp`
- Modify: `include/config.h`
- Test: `tests/test_architecture_isolation.py`, `tests/test_performance_regressions.py`, `tests/test_flash_envelope_lut.py`

**Interfaces:**
- Consumes: `BeaconConfig` timing/PWM constants and `FLASH_ENVELOPE_LUT`.
- Produces: `bool BeaconEngine::begin()`, `void BeaconEngine::request(bool on, uint8_t level)`, `bool BeaconEngine::isDarkWindowSafe() const`, and `void BeaconEngine::forceOff()`.

- [ ] **Step 1: Run Task 1 tests to confirm RED baseline**
  - Run: `python -m unittest tests.test_architecture_isolation tests.test_performance_regressions -v`
  - Expected: FAIL on missing BeaconEngine/event-driven structure.

- [ ] **Step 2: Implement `BeaconEngine` state and hardware ownership**
  - Move GPIO10 LEDC setup, GPIO8 WS2812 writes, cycle epoch, duty cache, RGB cache, and LUT sampling into `BeaconEngine`.
  - Keep exact timing constants and LUT unchanged.
  - `request(false, ...)` must wake the task and force output to zero promptly.
  - OFF->ON sets the cycle epoch to current absolute time; level-only changes do not restart the cycle.

- [ ] **Step 3: Implement priority-4 task + precise deadline wakeup**
  - Create one BeaconEngine FreeRTOS task at priority 4.
  - Use task notification for state changes.
  - Use `esp_timer` only to wake/notify the task at the next optical deadline; perform LEDC/WS2812 writes in task context.
  - While OFF or in long dark intervals, block instead of polling every 1 ms.
  - On late wake, derive phase from absolute `esp_timer_get_time()` and catch up without drift.

- [ ] **Step 4: Remove optical polling from `loop()`**
  - Delete `updateBeaconOutput()` and `delay(1)`-driven optical service from `main.cpp`.
  - Keep loop/housekeeping low-rate only.

- [ ] **Step 5: Run optical/performance tests**
  - Run: `python -m unittest tests.test_flash_envelope_lut tests.test_architecture_isolation tests.test_performance_regressions -v`
  - Expected: PASS for BeaconEngine/timing tests; Zigbee module tests may still fail until Task 3.

- [ ] **Step 6: Build firmware**
  - Run: `pio run -e esp32-h2-supermini-end-device`
  - Expected: `SUCCESS`.

- [ ] **Step 7: Commit**
  - Commit message: `refactor: isolate event driven beacon engine`

### Task 3: Extract ZigbeeLight and enforce a static logical lamp shadow

**Files:**
- Create: `include/zigbee_light.h`
- Create: `src/zigbee_light.cpp`
- Modify: `src/main.cpp`
- Test: `tests/test_light_profile.py`, `tests/test_architecture_isolation.py`, `tests/test_zigbee3_profile.py`

**Interfaces:**
- Consumes: `BeaconEngine::request(bool,uint8_t)` and persisted startup state.
- Produces: `bool ZigbeeLight::begin(bool initialOn, uint8_t initialLevel)`, `bool ZigbeeLight::setLocalState(bool on, uint8_t level)`, `bool ZigbeeLight::on() const`, `uint8_t ZigbeeLight::level() const`, `void ZigbeeLight::setStateChangedCallback(...)`.

- [ ] **Step 1: Add/adjust failing tests for static coordinator state**
  - Assert EP2 remains `ZigbeeDimmableLight` and only logical command/local-toggle code calls `setLight`.
  - Assert BeaconEngine cannot mutate Zigbee attributes.
  - Assert level represents requested peak, not instantaneous duty.

- [ ] **Step 2: Run focused tests to verify RED**
  - Run: `python -m unittest tests.test_light_profile tests.test_architecture_isolation -v`
  - Expected: FAIL until ZigbeeLight is extracted.

- [ ] **Step 3: Implement ZigbeeLight**
  - Own `ZigbeeDimmableLight` inside the module.
  - Callback only stores latest `on/level` and wakes/notifies application/BeaconEngine; no logging, ADC, NVS, PWM, or reports.
  - Local BOOT toggle updates Zigbee shadow exactly once using `setLight`; physical flashes never call it.
  - Preserve manufacturer/model/version metadata and EP2 device type.

- [ ] **Step 4: Make always-on RX explicit**
  - Call `Zigbee.setRxOnWhenIdle(true)` before `Zigbee.begin(ZIGBEE_END_DEVICE)`.
  - Preserve endpoint registration order and current battery endpoint behavior.

- [ ] **Step 5: Run Zigbee/isolation tests**
  - Run: `python -m unittest tests.test_light_profile tests.test_architecture_isolation tests.test_zigbee3_profile -v`
  - Expected: PASS.

- [ ] **Step 6: Build firmware**
  - Run: `pio run -e esp32-h2-supermini-end-device`
  - Expected: `SUCCESS`.

- [ ] **Step 7: Commit**
  - Commit message: `refactor: isolate zigbee logical light state`

### Task 4: Extract BatteryTelemetry and remove blocking startup ADC work

**Files:**
- Create: `include/battery_telemetry.h`
- Create: `src/battery_telemetry.cpp`
- Modify: `src/main.cpp`
- Modify: `include/config.h`
- Test: `tests/test_battery_clusters.py`, `tests/test_battery_unknowns.py`, `tests/test_hotpath_optimization.py`

**Interfaces:**
- Consumes: `BeaconEngine::isDarkWindowSafe()` and Zigbee connection status.
- Produces: `bool BatteryTelemetry::configureEndpoint()`, `void BatteryTelemetry::service(uint32_t nowMs, bool backgroundAllowed)`, and access to its Zigbee endpoint for registration.

- [ ] **Step 1: Adjust tests to module boundaries and add startup non-blocking assertion**
  - Preserve Power Configuration/DCVoltage/unknown sentinel assertions.
  - Assert no 32-sample blocking battery loop runs before Zigbee startup.

- [ ] **Step 2: Run focused tests to verify RED**
  - Run: `python -m unittest tests.test_battery_clusters tests.test_battery_unknowns tests.test_hotpath_optimization -v`
  - Expected: FAIL until BatteryTelemetry is extracted and startup sampling becomes incremental.

- [ ] **Step 3: Implement BatteryTelemetry**
  - Move EP1 class, ADC sampler, SOC curve, dirty/report state, and report scheduling into the module.
  - Preserve `0xFF` battery unknowns and `INT16_MIN` DCVoltage unknown.
  - Start with unknown battery values, start Zigbee, then acquire 32 samples incrementally during safe background windows.
  - Replace float scaling with integer arithmetic only where the exact rounded result remains unchanged for current `ratio=2.0`, `calibration=1.0`; otherwise retain the current calculation.

- [ ] **Step 4: Run battery/hot-path tests**
  - Run: `python -m unittest tests.test_battery_clusters tests.test_battery_unknowns tests.test_hotpath_optimization -v`
  - Expected: PASS.

- [ ] **Step 5: Build firmware**
  - Run: `pio run -e esp32-h2-supermini-end-device`
  - Expected: `SUCCESS`.

- [ ] **Step 6: Commit**
  - Commit message: `refactor: isolate background battery telemetry`

### Task 5: Housekeeping, status LED behavior, persistence, and command-burst safety

**Files:**
- Modify: `src/main.cpp`
- Modify: `include/config.h`
- Modify: `tests/test_performance_regressions.py`
- Create: `tests/test_status_and_control.py`

**Interfaces:**
- Consumes: BeaconEngine, ZigbeeLight, BatteryTelemetry interfaces from Tasks 2-4.
- Produces: final application orchestration and low-rate housekeeping.

- [ ] **Step 1: Add failing tests**
  - Connected Zigbee state -> blue GPIO13 OFF.
  - Identify -> temporary LED activity then OFF again while connected.
  - Button short press -> one logical toggle.
  - Rapid commands -> latest state is authoritative; duplicate ON does not restart cycle.
  - NVS remains delayed/coalesced and outside optical critical window.

- [ ] **Step 2: Run tests to verify RED**
  - Run: `python -m unittest tests.test_status_and_control tests.test_performance_regressions -v`
  - Expected: FAIL on new connected-LED/control assertions.

- [ ] **Step 3: Simplify `main.cpp`**
  - Keep setup/composition, BOOT debounce/factory reset, status LED, persistence, and low-rate service only.
  - Connected state writes status LED OFF; search mode breathes; Identify overrides temporarily.
  - Do not reintroduce 1 ms application polling.

- [ ] **Step 4: Run full unit suite**
  - Run: `python -m unittest discover -s tests -v`
  - Expected: all tests PASS.

- [ ] **Step 5: Build firmware**
  - Run: `pio run -e esp32-h2-supermini-end-device`
  - Expected: `SUCCESS`.

- [ ] **Step 6: Commit**
  - Commit message: `perf: reduce housekeeping wakeups and status power`

### Task 6: Add GitHub Actions CI and update documentation/version

**Files:**
- Create: `.github/workflows/firmware-ci.yml`
- Modify: `README.md`
- Modify: `include/config.h`
- Modify: tests that pin firmware version.

**Interfaces:**
- Consumes: complete refactored firmware.
- Produces: CI contract and documented behavior.

- [ ] **Step 1: Add CI workflow**
  - Trigger on PRs and pushes to `main`/`feature/**`.
  - Set up Python, install PlatformIO, run `python -m unittest discover -s tests -v`, then `pio run -e esp32-h2-supermini-end-device`.

- [ ] **Step 2: Update firmware version and README**
  - Bump alpha version once for this refactor.
  - Document always-on Zigbee, static logical lamp behavior, event-driven optical scheduling, blue LED OFF after join, and unchanged WS2812/lighthouse pattern.

- [ ] **Step 3: Run full verification locally**
  - Run tests and PlatformIO build.
  - Expected: all tests PASS and build `SUCCESS`.

- [ ] **Step 4: Commit**
  - Commit message: `ci: verify zigbee beacon firmware`

### Task 7: Whole-branch review, flash, and hardware validation

**Files:**
- No product-code changes unless review finds a defect; any fix gets its own test and commit.

**Interfaces:**
- Consumes: complete feature branch.
- Produces: flashed test firmware and PR evidence.

- [ ] **Step 1: Compare branch against `main`**
  - Review every changed file for timing changes, Zigbee coupling, task priority mistakes, unsafe timer context calls, and accidental LUT changes.

- [ ] **Step 2: Run final tests/build from a clean checkout/worktree**
  - `python -m unittest discover -s tests -v`
  - `pio run -e esp32-h2-supermini-end-device`
  - Expected: all PASS / `SUCCESS`.

- [ ] **Step 3: Flash ESP32-H2 on COM5**
  - Use PlatformIO/esptool with UTF-8 environment settings required on this Windows host.
  - Preserve Zigbee NVS for an upgrade/rejoin test first; do not erase unless pairing state prevents validation.
  - Verify esptool reports successful write/hash verification.

- [ ] **Step 4: Capture serial and bench-test**
  - Confirm firmware version, Zigbee rejoin, EP1/EP2 startup, no fatal errors.
  - Confirm Yandex lamp remains logically ON while WS2812 executes the full three-flash cycle.
  - Confirm ON starts first flash immediately; OFF extinguishes immediately; brightness changes peak only; duplicate ON does not restart.
  - Confirm blue LED is OFF after successful connection and WS2812 appearance matches current firmware.
  - Confirm BOOT short/5 s behaviors and invalid battery handling.

- [ ] **Step 5: Open PR into `main`**
  - Include audit findings, test/build evidence, hardware results, and deferred sleepy-mode note.
  - Do not merge until hardware behavior is accepted.
