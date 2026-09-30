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
volatile bool requestedOn = false;
volatile uint8_t requestedLevel = DEFAULT_LEVEL;
volatile bool requestPending = false;
volatile bool identifyActive = false;

bool activeOn = false;
uint8_t activeLevel = DEFAULT_LEVEL;
uint8_t lastStatusLedLevel = 0xFF;
uint32_t lastStatusUpdateMs = 0;
uint32_t lastButtonPollMs = 0;

bool prefsDirty = false;
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
  const uint8_t duty = STATUS_LED_ACTIVE_HIGH ? level : static_cast<uint8_t>(255U - level);
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
  // Zigbee callback: keep it short; no flash writes or PWM math here.
  enqueueRequestedState(state, level);
}

void onIdentify(bool active) {
  identifyActive = active;
}

void applyRequestedState(uint32_t nowMs) {
  if (!requestPending) return;

  bool newOn;
  uint8_t newLevel;
  portENTER_CRITICAL(&stateMux);
  newOn = requestedOn;
  newLevel = requestedLevel;
  requestPending = false;
  portEXIT_CRITICAL(&stateMux);

  const bool stateChanged = (newOn != activeOn) || (newLevel != activeLevel);
  activeOn = newOn;
  activeLevel = newLevel;
  beacon.request(activeOn, activeLevel);

  if (stateChanged) {
    prefsDirty = true;
    prefsDirtySinceMs = nowMs;
  }
}

void loadPersistentState() {
  prefs.begin("beacon", false);
  activeLevel = prefs.getUChar("level", DEFAULT_LEVEL);

  if (RESTORE_OUTPUT_AFTER_REBOOT) {
    activeOn = prefs.getBool("on", false);
  } else {
    activeOn = false;
  }

  if (activeLevel > ZIGBEE_MAX_LEVEL) {
    activeLevel = DEFAULT_LEVEL;
  }

  enqueueRequestedState(activeOn, activeLevel);
}

void savePersistentStateIfNeeded(uint32_t nowMs) {
  if (!prefsDirty || (nowMs - prefsDirtySinceMs) < PREFS_WRITE_DELAY_MS) return;
  if (!backgroundWorkAllowed()) return;
  prefs.putBool("on", activeOn);
  prefs.putUChar("level", activeLevel);
  prefsDirty = false;
}

inline uint8_t smoothBreathingLevel(uint32_t nowMs) {
  const uint32_t phase = nowMs % ZIGBEE_BREATHE_PERIOD_MS;
  const uint32_t half = ZIGBEE_BREATHE_PERIOD_MS / 2U;
  const uint32_t ramp = phase <= half ? phase : ZIGBEE_BREATHE_PERIOD_MS - phase;
  const uint32_t x = (ramp * 255U + half / 2U) / half;
  return static_cast<uint8_t>((x * x * (765U - 2U * x) + 32512U) / 65025U);
}

void updateIndicators(uint32_t nowMs) {
  if ((nowMs - lastStatusUpdateMs) < STATUS_UPDATE_INTERVAL_MS) return;
  lastStatusUpdateMs = nowMs;

  if (identifyActive) {
    writeStatusLed(((nowMs / 250U) & 1U) != 0);
  } else if (Zigbee.connected()) {
    writeStatusLed(true);
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

  prefs.clear();
  Serial.println("[BUTTON] Zigbee factory reset");
  Serial.flush();
  delay(100);
  Zigbee.factoryReset(); // Official API resets Zigbee storage and reboots.
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

void printBanner() {
  Serial.println();
  Serial.println("========================================");
  Serial.println(" Doobsky Zigbee Beacon / ESP32-H2");
  Serial.printf(" Firmware: %s\n", FW_VERSION);
  Serial.println(" Light: Fl(3).W.16.5s");
  Serial.printf(" PWM: GPIO%u, %lu Hz, %u-bit\n",
                MOSFET_PWM_PIN,
                static_cast<unsigned long>(PWM_FREQUENCY_HZ),
                PWM_RESOLUTION_BITS);
  Serial.println(" Zigbee role: End Device");
  Serial.printf(" Zigbee status: blue GPIO%u; beacon mirror: RGB GPIO%u\n", STATUS_LED_PIN, RGB_LED_PIN);
  Serial.printf(" Battery ADC: GPIO%u (2:1 divider)\n", BATTERY_ADC_PIN);
  Serial.println(" Endpoints: EP1 Battery Meter Interface; EP2 Dimmable Light");
  Serial.println(" BOOT short: toggle beacon");
  Serial.println(" BOOT 5s: Zigbee factory reset");
  Serial.println("========================================");
}

} // namespace

void setup() {
  Serial.begin(115200);
  printBanner();

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  if (!ledcAttach(STATUS_LED_PIN, STATUS_LED_PWM_FREQUENCY_HZ, STATUS_LED_PWM_RESOLUTION_BITS)) {
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
    Serial.println("[WARN] Battery runtime/reporting setup incomplete; retry/reporting may depend on coordinator");
  }

  Serial.println("[ZB] Stack started. Waiting for/joining network in background.");
}

void loop() {
  const uint32_t nowMs = millis();
  applyRequestedState(nowMs);
  updateButton(nowMs);
  updateIndicators(nowMs);
  battery.service(nowMs, backgroundWorkAllowed());
  savePersistentStateIfNeeded(nowMs);
  delay(5);
}
