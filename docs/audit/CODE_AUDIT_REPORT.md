# Итоговый аудит кода и оптимизации

Дата: 2026-10-04  
Ветка: `audit/full-code-optimization-2026-10-04`  
База: `main` @ `53eca551`  
PR: #2

## Итог

Полностью просмотрены исполняемый код, заголовочные файлы, конфигурация PlatformIO, Zigbee-профиль, батарейная телеметрия, NVS, таймеры, FreeRTOS-синхронизация, тесты и GitHub Actions.

Оптимизация выполнена без изменения основной оптической характеристики:

- `Fl(3).W.16.5s` сохранена;
- период остаётся ровно 16,500000 с;
- LUT огибающей не изменён;
- внешний PWM остаётся 1 кГц / 10 бит;
- Zigbee остаётся стандартным HA/ZCL End Device;
- `RxOnWhenIdle=true` сохранён для быстрой реакции на команды.

## Измеримый результат сборки

Для корректного сравнения использованы две GitHub Actions-сборки на **одинаковой** среде:

- pioarduino **55.03.312**;
- Arduino-ESP32 **3.3.12**.

| Показатель | main `53eca551` | аудит | Разница |
|---|---:|---:|---:|
| Регрессионные тесты | 46 | 58 | **+12** |
| RAM | 32 752 байт | 32 752 байт | **0 байт** |
| Flash | 669 645 байт | 670 117 байт | **+472 байта** |
| Total image | 690 385 байт | 691 025 байт | **+640 байт** |
| Сборка | SUCCESS | SUCCESS | — |

Рост Flash на 472 байта — около 0,04% application partition. Дополнительная RAM не потребовалась.

Время CI-сборки не используется как показатель производительности прошивки, так как оно зависит от состояния GitHub runner и кэша.

## Исправленные проблемы

### 1. Межзадачная синхронизация

Исправлены нестрого синхронизированные состояния между Zigbee callback и main loop:

- `requestPending`;
- requested On/Off и level;
- Identify state.

Убрано использование `volatile` как замены синхронизации. Состояние теперь передаётся через короткие critical sections.

### 2. Сохранение яркости Zigbee

Обнаружена дополнительная причина сброса яркости: Arduino `ZigbeeDimmableLight` обновляет свой внутренний `CurrentLevel` до вызова пользовательского callback.

Теперь:

- `OFF + CurrentLevel=0` не стирает последнюю яркость;
- `ON + CurrentLevel=0` также не восстанавливает ложный нулевой уровень;
- корректировка Zigbee-атрибута ставится в очередь;
- запись в endpoint выполняется из normal task context, а не внутри callback;
- сохраняется последний ненулевой уровень.

### 3. NVS / ресурс Flash

Persistence переработан:

- отдельный dirty-флаг для On/Off;
- отдельный dirty-флаг для brightness;
- при изменении одного поля второе больше не записывается повторно;
- ошибка NVS не вызывает частый retry;
- проверяется `Preferences::begin()`;
- factory reset закрывает Preferences handle;
- coalescing delay сокращён с 1500 до 750 мс.

Для типичного изменения только On/Off или только яркости количество логических записываемых ключей уменьшено с двух до одного.

### 4. BeaconEngine

Упрощён критический оптический путь:

- удалено неиспользуемое runtime-состояние;
- исключён лишний вызов `esp_timer_get_time()`;
- убраны ненужные critical sections вокруг task-owned state;
- исключён повторный stop one-shot timer;
- duplicate requests не будят BeaconEngine без необходимости;
- ранний выход в PWM/RGB path не ломает отложенное RGB-обновление;
- pending transition учитывается при разрешении фоновой работы.

Оптический hot path по-прежнему не содержит Zigbee, NVS, ADC, Serial и блокирующих задержек.

### 5. Battery / ADC

- float scaling заменён на fixed-point;
- добавлена защита от деления на ноль;
- введён `needsService()`;
- main loop не выполняет проверку безопасного оптического окна для батареи, когда работа батареи вообще не требуется;
- sampling остаётся инкрементальным;
- 32 ADC samples сохранены;
- runtime sampling продолжает работать даже после transient failure начальной настройки Zigbee attributes/reporting.

### 6. Энергопотребление

Изменения, которые можно сделать программно без ухудшения Zigbee latency:

| Параметр | Было | Стало |
|---|---:|---:|
| CPU target | 96 МГц | **64 МГц** |
| Main housekeeping | 100 Гц | **50 Гц** |
| Status/button polling | 100 Гц | **50 Гц** |
| Максимальная частота RGB update | 200 Гц | **100 Гц** |
| Zigbee RX while idle | ON | **ON** |

CPU clock снижен на 33,3%. Это не означает снижение общего тока устройства на 33,3%: итог зависит от 802.15.4 RX, RGB, преобразователей и внешнего LED. Реальный выигрыш должен измеряться амперметром на готовом устройстве.

Sleepy End Device намеренно не включён, потому что это изменило бы задержку управления.

### 7. Воспроизводимость сборки

До аудита использовалась плавающая ссылка pioarduino `stable`, из-за чего toolchain мог меняться без изменения репозитория.

Теперь зафиксированы:

- pioarduino **55.03.312**;
- PlatformIO CI **6.2.0**.

Добавлены тесты, запрещающие случайный возврат к floating toolchain.

## Что сознательно не оптимизировалось

### Beacon task stack

`4096` байт не уменьшались вслепую. Для безопасного уменьшения нужен runtime `uxTaskGetStackHighWaterMark()` на реальном устройстве.

### Zigbee sleepy mode

Не включён. Always-on RX обеспечивает текущую отзывчивость хаба.

### Более низкая CPU frequency

ESP32-H2 поддерживает и другие режимы, но 48/32 МГц не выбраны без аппаратного latency/stability теста. 64 МГц — консервативный power profile.

### Background guard

`BACKGROUND_WORK_GUARD_US = 50 ms` оставлен без изменения. Увеличивать его имеет смысл только после измерения worst-case NVS latency и проверки оптического jitter.

### Аппаратные потребители

Код не может устранить постоянный ток:

- делителя аккумулятора 100 кОм / 100 кОм;
- встроенного RGB при требуемом красном статусе 30%;
- always-on Zigbee RX;
- LX-LBC01 / TPS61088;
- внешнего 12-вольтового LED-модуля.

Если приоритетом станет максимальная автономность, следующий этап должен быть аппаратно-программным и сопровождаться измерением тока.

## Тесты

Финальный CI:

- **58/58 PASS**;
- PlatformIO build **SUCCESS**;
- pinned toolchain;
- ошибок компиляции проекта нет.

Покрыты отдельными регрессиями:

- точные lighthouse timings;
- LUT;
- отсутствие Zigbee из optical engine;
- отсутствие блокирующих операций в hot path;
- CurrentLevel persistence;
- concurrency;
- NVS dirty-state;
- battery unknown values;
- fixed-point scaling;
- background-work gating;
- CPU/RX power policy;
- build reproducibility.

## Что осталось проверить на железе

Перед переносом power-sensitive изменений в `main` необходимо:

1. прошить audit build в ESP32-H2;
2. проверить join/rejoin Zigbee;
3. проверить ON/OFF и изменение brightness;
4. проверить восстановление brightness после полного отключения питания;
5. проверить визуальную плавность RGB при throttle 10 мс;
6. проверить точность трёх вспышек и отсутствие визуального jitter;
7. подтвердить стабильность ESP32-H2 на 64 МГц;
8. при возможности измерить ток старой и audit-прошивки в одинаковых режимах;
9. снять high-water mark BeaconEngine task перед решением об уменьшении stack.

До выполнения этих пунктов PR #2 должен оставаться draft и не должен автоматически объединяться в `main`.

## Повторный независимый аудит — 2026-10-05

После завершения первоначального аудита выполнен второй проход по всей ветке: исходники, заголовки, Zigbee-профиль, FreeRTOS/esp_timer, NVS, ADC, тесты, CI, PlatformIO и публичное оформление репозитория. Дополнительно сверены используемые API с Arduino-ESP32 3.3.12.

### Итог по серьёзности

Критических дефектов уровня «немедленно повреждает устройство / гарантированно ломает Zigbee / нарушает Fl(3).W.16.5s» не найдено. Ветка собирается и проходит CI, однако до merge остаются несколько замечаний средней важности и обязательная аппаратная проверка power-sensitive изменений.

### Замечания средней важности

#### A0. Редкое race-окно в deferred CurrentLevel correction

В `ZigbeeLight::service()` correction снимается из очереди под `mux_`, после чего `getLightLevel()` и `setLightLevel()` выполняются уже без этого application mutex. Zigbee task Arduino-ESP32 работает с приоритетом 5, а main/service может быть прерван новым Zigbee callback.

Возможный редкий сценарий при быстрой последовательности команд:

1. service забирает старую correction и очищает pending;
2. Zigbee callback успевает поставить более новую correction;
3. service применяет старый `setLightLevel()`;
4. Arduino `setLightLevel()` синхронно вызывает `lightChanged()`;
5. callback с ненулевым уровнем очищает `levelCorrectionPending_`, из-за чего более новая correction может потеряться.

Это не проявляется в обычном одиночном OFF/ON и не покрывается текущими source-text тестами, но формально оставляет race при burst-командах яркости/Off. Перед merge рекомендуется добавить generation/sequence token для correction или другой механизм, гарантирующий принцип «последняя команда авторитетна», и отдельный runtime/HIL regression test.

#### A1. Результат обновления батарейных атрибутов игнорируется

В `src/battery_telemetry.cpp:234-239` после завершения ADC-серии вызываются:

- `setBatteryTelemetryRaw(...)`;
- `setDCMeasurement(...)`.

Их возвращаемые значения не проверяются.

Если обновление локального Zigbee-атрибута временно завершится ошибкой, дальнейший manual report может отправить старое значение, а успешный report затем способен очистить `batteryTelemetryDirty_`. Рекомендация: считать телеметрию успешно обновлённой только после успешной записи всех необходимых локальных атрибутов; иначе оставлять dirty и повторять попытку.

#### A2. BatteryVoltage обновляется, но явно не репортится

Power Configuration `BatteryVoltage` обновляется локально, однако ручной report отправляется только для:

- `BatteryPercentageRemaining` через `reportBatteryPercentage()`;
- `DCVoltage` через `reportDC(...)`.

В Arduino-ESP32 3.3.12 `reportBatteryPercentage()` репортит только attribute `BatteryPercentageRemaining`; отдельного штатного `reportBatteryVoltage()` в этом API нет.

Это не ломает чтение атрибута координатором по запросу и не мешает DCVoltage, но координатор, ожидающий именно unsolicited-report Power Configuration/BatteryVoltage, может получать его не сразу. Нужно либо сознательно оставить DCVoltage главным точным каналом и задокументировать это, либо добавить стандартный generic report для BatteryVoltage.

#### A3. DCVoltage одновременно использует automatic reporting и manual reporting

В `startRuntime()` вызывается `setDCReporting(...)`, а в `service()` дополнительно выполняется `reportDC(...)`.

Оба механизма валидны, но одновременно они потенциально создают дублирующий Zigbee-трафик. Для battery/power optimization лучше выбрать одну политику и проверить её на реальном координаторе.

#### A4. 58/58 тестов не являются runtime-тестами C++

Большая часть Python regression suite проверяет структуру исходников, наличие/отсутствие токенов, константы и LUT. Это полезная защита архитектуры, но она не исполняет C++-логику BeaconEngine, FreeRTOS-синхронизацию, NVS и Zigbee callbacks.

CI дополнительно компилирует реальную прошивку, поэтому синтаксис/API проверены. Но формулировка «58/58 PASS» не должна трактоваться как полное доказательство runtime-корректности. Для наиболее критичных частей полезны host-native pure-logic tests и/или HIL-тест на реальном ESP32-H2.

#### A5. Аппаратная проверка остаётся блокером merge

Следующие изменения невозможно полностью подтвердить только CI:

- CPU 64 МГц;
- RGB throttle 10 мс;
- timing/jitter на реальном устройстве;
- Zigbee join/rejoin и latency;
- power-cycle persistence;
- фактическое энергопотребление.

Официальный Arduino-ESP32 Zigbee validation также требует реального 802.15.4 hardware для interop-сценариев. Поэтому PR #2 правильно остаётся Draft до HIL-проверки.

### Замечания низкой/средней важности

#### A6. Сборка стала существенно воспроизводимее, но не полностью immutable

Зафиксированы PlatformIO 6.2.0 и pioarduino 55.03.312. При этом остаются плавающими:

- `ubuntu-latest`;
- Python `3.13` без patch-version;
- GitHub Actions по major tags (`checkout@v4`, `setup-python@v5`, `cache@v4`).

Для обычного проекта этого достаточно. Для supply-chain/byte-for-byte reproducibility можно закрепить runner, patch Python и SHA конкретных Actions.

#### A7. Используются внутренние поля Zigbee endpoint

`BatteryTelemetryEndpoint` напрямую использует protected/internal поля Arduino Zigbee: `_device_id`, `_ep_config`, `_cluster_list`, `_power_source`.

На pinned Arduino-ESP32 3.3.12 это работает и CI это подтверждает, но это точка повышенной связности с внутренней реализацией библиотеки. При обновлении framework этот модуль нужно проверять первым.

#### A8. Factory reset не проверяет результат app-NVS clear

`prefs.clear()` вызывается без проверки результата. Редкая ошибка очистки может оставить app-настройки яркости/OnOff после Zigbee factory reset. Рекомендация: логировать ошибку и при необходимости повторять/обрабатывать её до вызова `Zigbee.factoryReset()`.

### Репозиторий / GitHub

#### A9. `docs/superpowers/` уже отслеживается Git, хотя находится в `.gitignore`

`.gitignore` действует только на новые untracked-файлы. В audit-ветке internal planning/spec-файлы уже отслеживаются и поэтому останутся в истории/PR. Для публичного репозитория стоит либо осознанно оставить их как engineering history, либо удалить tracked-копии отдельным коммитом.

#### A10. В публичном репозитории нет LICENSE

README, SECURITY и CONTRIBUTING присутствуют, но лицензия отсутствует. Это означает, что сторонним пользователям не предоставлены явные права на копирование/модификацию/распространение. Выбор MIT/Apache-2.0/GPL и т.п. должен сделать владелец проекта; в рамках аудита лицензия автоматически не назначается.

#### A11. `main` не защищён правилами GitHub

На момент аудита branch protection для `main` не включён. CI существует, но GitHub не требует его прохождения перед merge/push. Для нормального PR-процесса рекомендуется защитить `main`, запретить force-push/delete и потребовать успешный Firmware CI.

### Что подтверждено вторым проходом

- `Fl(3).W.16.5s`, LUT и 1 кГц PWM в audit-ветке не изменены.
- BeaconEngine не зависит от Zigbee и не пишет Zigbee state из optical hot path.
- Zigbee callback будит BeaconEngine напрямую.
- CPU 64 МГц является поддерживаемой конфигурацией ESP32-H2 API, но runtime stability всё равно должна быть проверена на конкретной плате.
- `analogReadMilliVolts()` использует калиброванное ADC-измерение Arduino-ESP32.
- Последний CI audit-ветки завершён успешно: **58/58 PASS**, build **SUCCESS**, RAM **32 752 байта (10,0%)**, Flash **670 117 байт (51,1%)**.
- Audit-ветка на момент проверки: **47 коммитов впереди main, 0 позади**, PR #2 mergeable и Draft.

### Решение по merge

До аппаратного теста ветку **не объединять в main**. После HIL-проверки A1-A3 желательно исправить или явно принять как документированную политику телеметрии. Остальные пункты не блокируют функциональный тест прошивки, но должны быть учтены перед стабильным release.

## Исправления по повторному аудиту — 2026-10-05

Замечания A0–A3 исправлены в audit-ветке и повторно проверены CI.

### A0 — deferred CurrentLevel race: исправлено

Коррекция `CurrentLevel=0` теперь использует generation/in-flight tracking:

- каждая внешняя/локальная логическая команда получает новое поколение;
- self-generated callback от `setLightLevel()` распознаётся по task handle и не меняет application shadow;
- если во время старой correction приходит более новая команда, после завершения старой операции автоматически ставится correction к последнему логическому уровню;
- действует принцип **latest command wins**.

Это закрывает редкое race-окно burst-команд без вызова Zigbee stack API из Zigbee callback.

### A1 — ошибки обновления battery/DC attributes: исправлено

Добавлен отдельный `batteryAttributeSyncPending_` и retry state. После нового ADC sample:

- Power Configuration attributes и DCVoltage синхронизируются с локальным Zigbee endpoint;
- результаты обеих операций проверяются;
- при ошибке dirty/sync state сохраняется;
- reports не отправляются, пока локальные атрибуты не синхронизированы;
- retry выполняется с существующим `BATTERY_REPORT_RETRY_MS`, без tight loop.

### A2 — BatteryVoltage report: исправлено

Добавлен явный стандартный report Power Configuration / `BatteryVoltage` через `reportClusterAttribute()`.

Теперь report cycle включает:

1. `BatteryPercentageRemaining`;
2. `BatteryVoltage`;
3. Electrical Measurement / `DCVoltage`.

Цикл считается успешным только если успешно отправлены все три стандартных reports.

### A3 — дублирующий DCVoltage reporting: исправлено

`setDCReporting()` удалён. Для батарейной телеметрии используется одна предсказуемая политика — ручной report по change/periodic policy. Это исключает потенциальное дублирование automatic + manual DC reports.

### Повторная валидация

GitHub Actions run #94:

- **61/61 tests PASS**;
- ESP32-H2 production build: **SUCCESS**;
- RAM: **32 768 / 327 680 байт (10,0%)**;
- Flash: **669 847 / 1 310 720 байт (51,1%)**;
- предупреждений компилятора из кода проекта нет.

По сравнению с исходным `main @ 53eca551` на одинаковом toolchain:

- RAM: **+16 байт**;
- Flash: **+202 байта**;
- regression tests: **46 → 61**.

Остаётся обязательный аппаратный HIL/runtime тест перед merge: Zigbee join/rejoin, burst ON/OFF/brightness, power-cycle persistence, 64 МГц, RGB/PWM smoothness и timing/jitter.

