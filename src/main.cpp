#include <Arduino.h>
#include <Preferences.h>
#include <Zigbee.h>
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

void localToggle() {
  const bool next = !zigbeeLight.on();
  uint8_t level = zigbeeLight.level();
  if (level == 0) level = DEFAULT_LEVEL;
  zigbeeLight.setLocalState(next, level);
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
  Serial.println(" Endpoints: EP1 Battery Meter Interface; EP2 Dimmable Light");
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

  loadPersistentState();
  beacon.request(activeOn, activeLevel);

  zigbeeLight.setStateChangedCallback(onZigbeeLightChange);
  zigbeeLight.setIdentifyChangedCallback(onIdentify);

  // EP1: standards-based battery telemetry starts with unknown values.
  if (!battery.configureEndpoint() || !Zigbee.addEndpoint(battery.endpoint())) {
    Serial.println("[FATAL] Failed to configure/register EP1 battery sensor");
    ESP.restart();
  }

  Serial.println("[ZB] Starting Zigbee stack...");
  if (!zigbeeLight.begin(activeOn, activeLevel)) {
    Serial.println("[FATAL] Zigbee light/stack startup failed; restarting");
    delay(500);
    ESP.restart();
  }

  if (!battery.startRuntime()) {
    Serial.println("[WARN] Battery initial Zigbee attribute sync failed; retry scheduled");
  }

  Serial.println("[ZB] Stack started. Waiting for/joining network in background.");
}

void loop() {
  const uint32_t nowMs = millis();
  zigbeeLight.service();
  applyRequestedState(nowMs);
  updateButton(nowMs);
  updateIndicators(nowMs);

  if (battery.needsService(nowMs)) {
    battery.service(nowMs, backgroundWorkAllowed());
  }
  savePersistentStateIfNeeded(nowMs);

  delay(HOUSEKEEPING_INTERVAL_MS);
}
