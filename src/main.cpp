#include <Arduino.h>
#include <Preferences.h>
#include <Zigbee.h>
#include <cstring>
#include "esp_zigbee_core.h"
#include "beacon_engine.h"
#include "battery_telemetry.h"
#include "zigbee_light.h"
#include "config.h"

#if !defined(ZIGBEE_MODE_ED)
#error "Zigbee End Device mode is required"
#endif

using namespace BeaconConfig;

namespace {

Doobsky::BatteryTelemetry battery;
BeaconEngine beacon;
Doobsky::ZigbeeLight zigbeeLight;
Preferences prefs;

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
bool requestedOn = false;
uint8_t requestedLevel = DEFAULT_LEVEL;
bool requestPending = false;
bool identifyActive = false;

bool activeOn = false;
uint8_t activeLevel = DEFAULT_LEVEL;
uint8_t lastStatusLedLevel = 0xFF;
uint32_t lastStatusUpdateMs = 0;
uint32_t lastButtonPollMs = 0;

bool prefsReady = false;
bool prefsOnDirty = false;
bool prefsLevelDirty = false;
uint32_t prefsDirtySinceMs = 0;

struct ButtonState {
  bool stablePressed = false;
  bool rawPressed = false;
  bool longActionDone = false;
  uint32_t rawChangedMs = 0;
  uint32_t pressedSinceMs = 0;
} button;

inline void writeStatusLedLevel(uint8_t level) {
  if (level == lastStatusLedLevel) return;
  const uint8_t duty =
      STATUS_LED_ACTIVE_HIGH ? level : static_cast<uint8_t>(255U - level);
  ledcWrite(STATUS_LED_PIN, duty);
  lastStatusLedLevel = level;
}

inline void writeStatusLed(bool on) {
  writeStatusLedLevel(on ? 255U : 0U);
}

inline bool backgroundWorkAllowed() {
  return beacon.isDarkWindowSafe();
}

void enqueueRequestedState(bool state, uint8_t level) {
  portENTER_CRITICAL(&stateMux);
  requestedOn = state;
  requestedLevel = level;
  requestPending = true;
  portEXIT_CRITICAL(&stateMux);
}

void onZigbeeLightChange(bool state, uint8_t level) {
  // Wake the optical engine immediately; housekeeping only persists the shadow state.
  beacon.request(state, level);
  enqueueRequestedState(state, level);
}

void onIdentify(bool active) {
  portENTER_CRITICAL(&stateMux);
  identifyActive = active;
  portEXIT_CRITICAL(&stateMux);
}

void applyRequestedState(uint32_t nowMs) {
  bool hasRequest = false;
  bool newOn = false;
  uint8_t newLevel = DEFAULT_LEVEL;

  portENTER_CRITICAL(&stateMux);
  if (requestPending) {
    newOn = requestedOn;
    newLevel = requestedLevel;
    requestPending = false;
    hasRequest = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (!hasRequest) return;

  // Some coordinators carry CurrentLevel=0 with OFF. OFF is represented by
  // activeOn; it must not erase the user's remembered non-zero brightness.
  if (!newOn && newLevel == 0) {
    newLevel = (activeLevel == 0 || activeLevel > ZIGBEE_MAX_LEVEL)
        ? DEFAULT_LEVEL
        : activeLevel;
  }

  const bool onChanged = newOn != activeOn;
  const bool levelChanged = newLevel != activeLevel;
  if (!onChanged && !levelChanged) return;

  activeOn = newOn;
  activeLevel = newLevel;
  Serial.printf("[LIGHT] applied on=%u level=%u changedOn=%u changedLevel=%u\n",
                activeOn, activeLevel, onChanged, levelChanged);

  if (onChanged) prefsOnDirty = true;
  if (levelChanged) prefsLevelDirty = true;
  prefsDirtySinceMs = nowMs;
}

void loadPersistentState() {
  prefsReady = prefs.begin("beacon", false);
  if (!prefsReady) {
    Serial.println("[WARN] NVS preferences unavailable; using defaults without persistence");
    activeOn = false;
    activeLevel = DEFAULT_LEVEL;
    enqueueRequestedState(activeOn, activeLevel);
    return;
  }

  activeLevel = prefs.getUChar("level", DEFAULT_LEVEL);
  activeOn = RESTORE_OUTPUT_AFTER_REBOOT ? prefs.getBool("on", false) : false;

  // Firmware before 0.8 could persist level 0 when a coordinator turned the
  // light off. Repair that state once; if the write fails, normal deferred
  // persistence will retry later.
  if (activeLevel == 0 || activeLevel > ZIGBEE_MAX_LEVEL) {
    activeLevel = DEFAULT_LEVEL;
    if (prefs.putUChar("level", activeLevel) == 0) {
      prefsLevelDirty = true;
      prefsDirtySinceMs = millis();
    }
  }

  enqueueRequestedState(activeOn, activeLevel);
}

void savePersistentStateIfNeeded(uint32_t nowMs) {
  if (!prefsReady || (!prefsOnDirty && !prefsLevelDirty)) return;
  if ((nowMs - prefsDirtySinceMs) < PREFS_WRITE_DELAY_MS) return;
  if (!backgroundWorkAllowed()) return;

  if (prefsOnDirty && prefs.putBool("on", activeOn) > 0) {
    prefsOnDirty = false;
  }
  if (prefsLevelDirty && prefs.putUChar("level", activeLevel) > 0) {
    prefsLevelDirty = false;
  }

  // Back off before retrying a failed NVS write instead of hammering flash on
  // every housekeeping iteration.
  if (prefsOnDirty || prefsLevelDirty) {
    prefsDirtySinceMs = nowMs;
  }
}

inline uint8_t smoothBreathingLevel(uint32_t nowMs) {
  const uint32_t phase = nowMs % ZIGBEE_BREATHE_PERIOD_MS;
  const uint32_t half = ZIGBEE_BREATHE_PERIOD_MS / 2U;
  const uint32_t ramp =
      phase <= half ? phase : ZIGBEE_BREATHE_PERIOD_MS - phase;
  const uint32_t x = (ramp * 255U + half / 2U) / half;
  return static_cast<uint8_t>(
      (x * x * (765U - 2U * x) + 32512U) / 65025U);
}

void updateIndicators(uint32_t nowMs) {
  if ((nowMs - lastStatusUpdateMs) < STATUS_UPDATE_INTERVAL_MS) return;
  lastStatusUpdateMs = nowMs;

  bool identify;
  portENTER_CRITICAL(&stateMux);
  identify = identifyActive;
  portEXIT_CRITICAL(&stateMux);

  if (identify) {
    writeStatusLed(((nowMs / 250U) & 1U) != 0);
  } else if (Zigbee.connected()) {
    writeStatusLed(false);
  } else {
    writeStatusLedLevel(smoothBreathingLevel(nowMs));
  }
}

// Passive diagnostics. Do not initiate commissioning or reset Zigbee NVS.
bool diagNetworkKnown = false;
bool diagLastStarted = false;
bool diagLastConnected = false;
uint32_t diagLastNetMs = 0;
char diagCommandBuffer[32] = {};
uint8_t diagCommandLength = 0;

void printNetworkDiagnostics(const char *reason) {
  const bool started = Zigbee.started();
  const bool connected = Zigbee.connected();
  Serial.printf("[NET] %s uptimeMs=%lu started=%u connected=%u rxOnIdle=%u\n",
                reason, static_cast<unsigned long>(millis()),
                started, connected, ZIGBEE_RX_ON_WHEN_IDLE);
  if (!started) return;

  // Consult stack-owned data only under the Zigbee lock.
  if (!esp_zb_lock_acquire(pdMS_TO_TICKS(30))) {
    Serial.println("[NET] snapshot: Zigbee lock timeout");
    return;
  }
  const uint16_t pan = esp_zb_get_pan_id();
  const uint16_t shortAddress = esp_zb_get_short_address();
  const uint8_t channel = esp_zb_get_current_channel();
  const bool factoryNew = esp_zb_bdb_is_factory_new();
  esp_zb_lock_release();
  Serial.printf("[NET] pan=0x%04X short=0x%04X channel=%u factoryNew=%u EP1=SimpleSensor EP2=DimmableLight\n",
                pan, shortAddress, channel, factoryNew);
}

void updateNetworkDiagnostics(uint32_t nowMs) {
  const bool started = Zigbee.started();
  const bool connected = Zigbee.connected();
  const bool changed = !diagNetworkKnown ||
                       diagLastStarted != started ||
                       diagLastConnected != connected;
  if (!changed && nowMs - diagLastNetMs < 30000U) return;
  // Network status must be visible even if optical safe-window detection is stuck.
  diagLastStarted = started;
  diagLastConnected = connected;
  diagNetworkKnown = true;
  diagLastNetMs = nowMs;
  printNetworkDiagnostics(changed ? "transition" : "heartbeat");
}

void coordinatorProbeCallback(esp_zb_zdp_status_t zdoStatus,
                              uint16_t address, esp_zb_af_node_desc_t *desc,
                              void *) {
  Serial.printf("[ZDO-PING] coordinator reply status=0x%02X address=0x%04X descriptor=%u\n",
                static_cast<unsigned>(zdoStatus), address, desc != nullptr);
}

void requestCoordinatorProbe() {
  if (!Zigbee.connected()) {
    Serial.println("[ZDO-PING] rejected: not connected");
    return;
  }
  if (!esp_zb_lock_acquire(pdMS_TO_TICKS(50))) {
    Serial.println("[ZDO-PING] rejected: lock busy");
    return;
  }
  esp_zb_zdo_node_desc_req_param_t request{};
  request.dst_nwk_addr = 0x0000; // Zigbee Trust Center / coordinator
  esp_zb_zdo_node_desc_req(&request, &coordinatorProbeCallback, nullptr);
  esp_zb_lock_release();
  Serial.println("[ZDO-PING] sent NodeDescriptorRequest to coordinator 0x0000");
}

void localToggle();

void executeDiagnosticCommand(const char *cmd) {
  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    Serial.println("[DIAG] Commands: status | battery | sample | report | ping | toggle | help");
  } else if (!strcmp(cmd, "status")) {
    printNetworkDiagnostics("serial");
    battery.printDiagnostics();
    Serial.printf("[DIAG] EP2 state=%u level=%u engine=%u engineLevel=%u buttonGPIO=%u darkSafe=%u pwmGPIO10=%u\n",
                  zigbeeLight.on(), zigbeeLight.level(), activeOn, activeLevel,
                  digitalRead(BOOT_BUTTON_PIN), beacon.isDarkWindowSafe(),
                  ledcRead(MOSFET_PWM_PIN));
  } else if (!strcmp(cmd, "ping")) {
    requestCoordinatorProbe();
  } else if (!strcmp(cmd, "toggle")) {
    localToggle();
  } else if (!strcmp(cmd, "battery")) {
    battery.printDiagnostics();
  } else if (!strcmp(cmd, "sample")) {
    battery.requestImmediateSample();
  } else if (!strcmp(cmd, "report")) {
    battery.requestImmediateReport();
  } else if (cmd[0]) {
    Serial.printf("[DIAG] Unknown command: %s (type help)\n", cmd);
  }
}

void processDiagnosticSerial() {
  // Non-blocking input. Never wait for a complete line or hinder the optical task.
  uint8_t count = 0;
  while (Serial.available() && ++count <= 48) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r' || ch == '\n') {
      if (diagCommandLength > 0) {
        diagCommandBuffer[diagCommandLength] = 0;
        executeDiagnosticCommand(diagCommandBuffer);
        diagCommandLength = 0;
      }
    } else if (ch >= 32 && ch <= 126 &&
               diagCommandLength + 1 < sizeof(diagCommandBuffer)) {
      diagCommandBuffer[diagCommandLength++] = ch;
    }
  }
}

void localToggle() {
  const bool next = !zigbeeLight.on();
  uint8_t level = zigbeeLight.level();
  if (level == 0) level = DEFAULT_LEVEL;
  Serial.printf("[BUTTON] toggle requested=%u level=%u\n", next, level);
  const bool ok = zigbeeLight.setLocalState(next, level);
  Serial.printf("[BUTTON] toggle ZCL accepted=%u newShadow=%u\n", ok, zigbeeLight.on());
}

void performFactoryReset() {
  beacon.forceOff();
  for (int i = 0; i < 6; ++i) {
    writeStatusLed((i & 1) == 0);
    delay(100);
  }
  writeStatusLed(false);

  if (prefsReady) {
    bool cleared = prefs.clear();
    if (!cleared) {
      delay(20);
      cleared = prefs.clear();
    }
    if (!cleared) {
      Serial.println("[WARN] Failed to clear beacon preferences during factory reset");
    }
    prefs.end();
    prefsReady = false;
  }

  Serial.println("[BUTTON] Zigbee factory reset");
  Serial.flush();
  delay(100);
  Zigbee.factoryReset();  // Official API resets Zigbee storage and reboots.
}

void updateButton(uint32_t nowMs) {
  if ((nowMs - lastButtonPollMs) < BUTTON_POLL_INTERVAL_MS) return;
  lastButtonPollMs = nowMs;

  const bool rawPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  if (rawPressed != button.rawPressed) {
    button.rawPressed = rawPressed;
    button.rawChangedMs = nowMs;
  }
  if ((nowMs - button.rawChangedMs) < BUTTON_DEBOUNCE_MS) return;

  if (button.stablePressed != button.rawPressed) {
    button.stablePressed = button.rawPressed;
    Serial.printf("[BUTTON] stablePressed=%u GPIO9=%u uptimeMs=%lu\n",
                  button.stablePressed, digitalRead(BOOT_BUTTON_PIN),
                  static_cast<unsigned long>(nowMs));
    if (button.stablePressed) {
      button.pressedSinceMs = nowMs;
      button.longActionDone = false;
    } else if (!button.longActionDone) {
      localToggle();
    }
  }

  if (button.stablePressed && !button.longActionDone &&
      (nowMs - button.pressedSinceMs) >= FACTORY_RESET_HOLD_MS) {
    button.longActionDone = true;
    performFactoryReset();
  }
}

void configureCpuFrequency() {
  if (getCpuFrequencyMhz() == CPU_FREQUENCY_MHZ) return;

  if (!setCpuFrequencyMhz(CPU_FREQUENCY_MHZ)) {
    Serial.printf("[WARN] Failed to set CPU frequency to %lu MHz; using %lu MHz\n",
                  static_cast<unsigned long>(CPU_FREQUENCY_MHZ),
                  static_cast<unsigned long>(getCpuFrequencyMhz()));
  }
}

uint8_t currentNodePowerLevel() {
  if (!battery.valid()) {
    // Zigbee Power Descriptor has no "unknown" level encoding. Until the ADC
    // is valid, keep the conservative mandatory encoding rather than inventing
    // a charge value.
    return ESP_ZB_AF_NODE_POWER_SOURCE_LEVEL_CRITICAL;
  }

  const uint8_t pct = battery.percent();
  if (pct <= 16) return ESP_ZB_AF_NODE_POWER_SOURCE_LEVEL_CRITICAL;
  if (pct <= 49) return ESP_ZB_AF_NODE_POWER_SOURCE_LEVEL_33_PERCENT;
  if (pct <= 82) return ESP_ZB_AF_NODE_POWER_SOURCE_LEVEL_66_PERCENT;
  return ESP_ZB_AF_NODE_POWER_SOURCE_LEVEL_100_PERCENT;
}

void syncZigbeePowerDescriptors() {
  if (!Zigbee.started() || !Zigbee.connected()) return;

  static bool basicPowerSourceSynced = false;
  static uint8_t lastNodePowerLevel = 0xFF;

  if (!basicPowerSourceSynced) {
    if (!zigbeeLight.setBasicPowerSourceBattery()) return;

    // The node descriptor and the Basic clusters agree on battery power.
    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(10))) return;
    esp_zb_set_node_descriptor_power_source(false);
    esp_zb_lock_release();
    basicPowerSourceSynced = true;
    Serial.println("[ZB] Battery power source synchronized");
  }

  const uint8_t level = currentNodePowerLevel();
  if (level == lastNodePowerLevel) return;

  esp_zb_af_node_power_desc_t power{};
  power.current_power_mode = ZIGBEE_RX_ON_WHEN_IDLE
      ? ESP_ZB_AF_NODE_POWER_MODE_SYNC_ON_WHEN_IDLE
      : ESP_ZB_AF_NODE_POWER_MODE_COME_ON_PERIODICALLY;
  power.available_power_sources =
      ESP_ZB_AF_NODE_POWER_SOURCE_RECHARGEABLE_BATTERY;
  power.current_power_source =
      ESP_ZB_AF_NODE_POWER_SOURCE_RECHARGEABLE_BATTERY;
  power.current_power_source_level = level;
  if (!esp_zb_lock_acquire(pdMS_TO_TICKS(10))) return;
  esp_zb_set_node_power_descriptor(power);
  esp_zb_lock_release();
  lastNodePowerLevel = level;
  Serial.printf("[BAT] adc=%umV valid=%u percent=%u ZDOlevel=%u\n",
                unsigned(battery.millivolts()),unsigned(battery.valid()),
                unsigned(battery.percent()),unsigned(level));
}

void printBanner() {
  Serial.println();
  Serial.println("========================================");
  Serial.println(" Doobsky Zigbee Beacon / ESP32-H2");
  Serial.printf(" Firmware: %s\n", FW_VERSION);
  Serial.println(" Light: Fl(3).W.16.5s");
  Serial.printf(" CPU: %lu MHz\n",
                static_cast<unsigned long>(getCpuFrequencyMhz()));
  Serial.printf(" PWM: GPIO%u, %lu Hz, %u-bit\n",
                MOSFET_PWM_PIN,
                static_cast<unsigned long>(PWM_FREQUENCY_HZ),
                PWM_RESOLUTION_BITS);
  Serial.println(" Zigbee role: End Device");
  Serial.printf(" Zigbee status: blue GPIO%u; beacon mirror: RGB GPIO%u\n",
                STATUS_LED_PIN, RGB_LED_PIN);
  Serial.printf(" Battery ADC: GPIO%u (2:1 divider)\n", BATTERY_ADC_PIN);
  Serial.println(" Endpoints: EP1 Battery Simple Sensor; EP2 Dimmable Light");
  Serial.println(" BOOT short: toggle beacon");
  Serial.println(" BOOT 5s: Zigbee factory reset");
  Serial.println("========================================");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  configureCpuFrequency();
  printBanner();

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  if (!ledcAttach(STATUS_LED_PIN,
                  STATUS_LED_PWM_FREQUENCY_HZ,
                  STATUS_LED_PWM_RESOLUTION_BITS)) {
    Serial.println("[FATAL] Zigbee status LED PWM attach failed");
    ESP.restart();
  }
  writeStatusLed(false);

  if (!beacon.begin()) {
    Serial.println("[FATAL] Beacon engine initialization failed");
    delay(100);
    ESP.restart();
  }

  zigbeeLight.setStateChangedCallback(onZigbeeLightChange);
  zigbeeLight.setIdentifyChangedCallback(onIdentify);

  // EP1: standards-based battery telemetry starts with unknown values.
  if (!battery.configureEndpoint() || !Zigbee.addEndpoint(battery.endpoint())) {
    Serial.println("[FATAL] Failed to configure/register EP1 battery sensor");
    ESP.restart();
  }

  // Battery baseline before enabling restored optical load (may draw high
  // current during a flash). Subsequent samples run in dark windows.
  loadPersistentState();
  beacon.request(activeOn, activeLevel);

  Serial.println("[ZB] Starting Zigbee stack...");
  if (!zigbeeLight.begin(activeOn, activeLevel)) {
    Serial.println("[FATAL] Zigbee light/stack startup failed; restarting");
    delay(500);
    ESP.restart();
  }

  if (!battery.startRuntime()) {
    Serial.println("[WARN] Battery initial Zigbee attribute sync failed; retry scheduled");
  }
  syncZigbeePowerDescriptors();

  Serial.println("[ZB] Stack started. Waiting for/joining network in background.");
  Serial.println("[DIAG] Sprut.Hub diagnostics: type help over 115200 baud");
}

void loop() {
  const uint32_t nowMs = millis();
  processDiagnosticSerial();  // non-blocking; must work even if optical task is not reaching dark windows
  updateNetworkDiagnostics(nowMs);
  static uint32_t seenLightTrace = 0;
  Doobsky::ZigbeeLight::LightTrace lightTrace{};
  if (zigbeeLight.readTrace(lightTrace, seenLightTrace)) {
    // The stock Arduino callback does not expose source cluster or TSN.
    // Changes are observations, not falsely attributed to a remote command.
    Serial.printf("[ZCL-OBS] EP2 seq=%lu on=%u rawLevel=%u logicalLevel=%u changedOn=%u changedLevel=%u tsn=unavailable\n",
                  static_cast<unsigned long>(lightTrace.sequence), lightTrace.on,
                  lightTrace.rawLevel, lightTrace.effectiveLevel,
                  lightTrace.onChanged, lightTrace.levelChanged);
  }
  applyRequestedState(nowMs);
  updateButton(nowMs);
  updateIndicators(nowMs);

  if (battery.needsService(nowMs)) {
    battery.service(nowMs, backgroundWorkAllowed());
  }
  syncZigbeePowerDescriptors();
  savePersistentStateIfNeeded(nowMs);

  delay(HOUSEKEEPING_INTERVAL_MS);
}