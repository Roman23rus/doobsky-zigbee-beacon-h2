# История изменений

Формат основан на принципах [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/).

Проект использует предварительные версии до завершения аппаратной и оптической валидации.

## [Unreleased]

### 0.9.0-alpha.3 — Zigbee transaction and report reliability (unflashed)

- Replaced Arduino ZigbeeDimmableLight setter with a locally transactional
  HA Dimmable Light endpoint: bounded Zigbee locks, ZCL read-back,
  partial-write rollback and no physical callback before result.
- Added separate bounded OnOff/Level reporting after local button actions.
- Added bounded light observation ring and deferred ZDO logging.
- Updated battery ZCL snapshot atomically under a bounded lock and split
  percentage/DC report retries using backoff, with zero-init ZCL requests.
- Marked tracked-source dirty state in firmware provenance manifest.
- Added C++ host behavioral tests (including partial failures and rollbacks).
- No change to app endpoint IDs, physical pins, Zigbee storage or TCLK profile.
- Candidate requires fresh Sprut.Hub RF tests; not flashed to paired COM3.



### 0.9.0-alpha.2 — post-line-by-line firmware cleanup (unflashed)

- Removed redundant OFF+CurrentLevel=0 handling in main; state handling is
  centralized in the ZigbeeLight state transition helper.
- Removed the duplicate Basic PowerSource attribute write after Zigbee start.
  Both Basic endpoints are initialized before registration; the ZDO Node
  Descriptor synchronization is preserved.
- Removed legacy startup NVS migration write. Invalid brightness in saved
  preferences is now repaired via normal deferred NVS persistence.
- Removed unused ESP Zigbee security include and two outdated Sprut templates.
- Removed unused manufacturing install-code generator for this home DIY product.
- Renamed build script, profile guard and Sprut build environment to reflect
  strict and compatibility builds; updated CI and contributor instructions.
- Added regression checks guarding against the old code's return.
- Both strict and Sprut-compatible ESP32-H2 images compile on pinned Arduino
  3.3.12/pioarduino 55.03.312-1. No device flash or Zigbee NVS change.
- Retained the operational PWM beacon generator, RGB/blue indicators,
  ZCL battery telemetry, factory reset, NVS, and serial diagnostics.

### 0.9.0-alpha.1 — Zigbee 3.0 profile work (unflashed)

- Added Simple Sensor Binary Input and Identify client; kept EP1/EP2 IDs.
- Added strict TCLK-exchange-required security profile alongside Sprut
  compatibility and profile-specific SWBuildID.
- Explicitly initialized EP2 battery PowerSource before endpoint registration.
- Not CSA certified; install-code provisioning not implemented.

### 0.8.3-beta.2-sprut — installed and tested on ESP32-H2

- Fixed deferred CurrentLevel writes overwriting newer OnOff commands.
- Added a 15-second throttle for manual ZDO diagnostic ping after a
  host-side ping burst caused a ZBOSS APS assertion.
- Preserved Zigbee NVS and backed up the entire 4MB flash before changes.

## [0.8.0-alpha.1] — 2026-10-04

### Добавлено

- Разделение оптического движка, Zigbee-логики и батарейной телеметрии.
- Стандартный Zigbee EP1 Meter Interface для аккумулятора.
- Стандартный EP2 Dimmable Light.
- Событийный BeaconEngine без накопления временного дрейфа.
- Предвычисленная `sin²`-огибающая вспышки.
- Синий статусный LED для Zigbee.
- Встроенный RGB/WS2812 как статусный индикатор и зеркало вспышки.
- CI для регрессионных тестов и PlatformIO-сборки.

### Изменено

- Рабочая частота PWM внешнего 12-вольтового LED изменена с 20 кГц на **1 кГц** после аппаратной проверки.
- В текущем прототипе силовой ключ заменён на **AO3400A**.
- RGB при включённом маяке горит красным на 30% между вспышками и плавно переходит в белый во время вспышки.
- При выключенном маяке RGB полностью гаснет.

### Исправлено

- Яркость больше не сбрасывается после отключения питания.
- `OFF + CurrentLevel=0` от координатора больше не стирает последнюю ненулевую яркость.
- Старое сохранённое значение яркости 0 автоматически мигрирует на корректное значение.
- Устранён USB-backfeed в силовую 5-вольтовую шину установкой Schottky 1N5819.
