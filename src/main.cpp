#include <Arduino.h>
#include <Preferences.h>
#include <Zigbee.h>
#include "zcl/esp_zigbee_zcl_power_config.h"
#include "beacon_engine.h"
#include "zigbee_light.h"
#include "config.h"

#if !defined(ZIGBEE_MODE_ED)
#error "Zigbee End Device mode is required"
#endif

using namespace BeaconConfig;

namespace {

static constexpr uint8_t BATTERY_ZCL_UNKNOWN = 0xFF;
static constexpr int16_t DC_VOLTAGE_UNKNOWN = INT16_MIN;

class BatteryTelemetryEndpoint : public ZigbeeElectricalMeasurement {
public:
  explicit BatteryTelemetryEndpoint(uint8_t endpoint) : ZigbeeElectricalMeasurement(endpoint) {
    _device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID;
    _ep_config.endpoint = endpoint;
    _ep_config.app_profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    _ep_config.app_device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID;
    _ep_config.app_device_version = 0;
  }

  bool addBatteryPowerConfiguration(uint8_t percentageRaw, uint8_t voltageRaw) {
    auto *basic = esp_zb_cluster_list_get_cluster(_cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
                                                   ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    if (!basic) return false;
    uint8_t source = static_cast<uint8_t>(ZB_POWER_SOURCE_BATTERY);
    if (esp_zb_cluster_update_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &source) != ESP_OK) return false;
    auto *power = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG);
    if (!power) return false;
    if (esp_zb_power_config_cluster_add_attr(power, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
                                              &percentageRaw) != ESP_OK) return false;
    if (esp_zb_power_config_cluster_add_attr(power, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
                                              &voltageRaw) != ESP_OK) return false;
    if (esp_zb_cluster_list_add_power_config_cluster(_cluster_list, power, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE) != ESP_OK) return false;
    _power_source = ZB_POWER_SOURCE_BATTERY;
    return true;
  }

  bool setBatteryTelemetryRaw(uint8_t percentageRaw, uint8_t voltageRaw) {
    const auto p = setClusterAttribute(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &percentageRaw, false);
    const auto v = setClusterAttribute(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &voltageRaw, false);
    return p == ESP_ZB_ZCL_STATUS_SUCCESS && v == ESP_ZB_ZCL_STATUS_SUCCESS;
  }
};

BatteryTelemetryEndpoint zbBatterySensor(BATTERY_SENSOR_ENDPOINT);
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

uint16_t batteryMv = 0;
uint8_t batteryPercent = 0;
bool batteryValid = false;
uint8_t lastReportedBatteryPercent = 0;
uint16_t lastReportedBatteryMv = 0;
uint32_t lastBatterySampleMs = 0;
uint32_t lastBatteryReportMs = 0;
uint32_t lastBatteryReportAttemptMs = 0;
bool batteryReportInitialized = false;
bool lastReportedBatteryValid = false;
bool batteryTelemetryDirty = true;

struct BatterySampler {
  uint32_t sumMv = 0;
  uint8_t samples = 0;
  bool active = false;
} batterySampler;

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

uint8_t batteryPercentFromMv(uint16_t mv) {
  struct Point { uint16_t mv; uint8_t pct; };
  static constexpr Point curve[] = {
    {3300, 0}, {3500, 7}, {3600, 15}, {3700, 30}, {3800, 50},
    {3900, 65}, {4000, 80}, {4100, 90}, {4200, 100},
  };
  if (mv <= curve[0].mv) return 0;
  const size_t last = (sizeof(curve) / sizeof(curve[0])) - 1;
  if (mv >= curve[last].mv) return 100;
  for (size_t i = 1; i <= last; ++i) {
    if (mv <= curve[i].mv) {
      const Point &lo = curve[i - 1], &hi = curve[i];
      const uint32_t span = hi.mv - lo.mv;
      const uint32_t num = static_cast<uint32_t>(mv - lo.mv) * (hi.pct - lo.pct);
      return static_cast<uint8_t>(lo.pct + (num + span / 2U) / span);
    }
  }
  return 100;
}

uint16_t batteryMvFromAdcSum(uint32_t sumMv, uint8_t samples) {
  const float adcMv = static_cast<float>(sumMv) / samples;
  const float scaledMv = adcMv * BATTERY_DIVIDER_RATIO * BATTERY_CALIBRATION;
  return static_cast<uint16_t>(scaledMv + 0.5f);
}

uint16_t readBatteryMv() {
  uint32_t sumMv = 0;
  for (uint8_t i = 0; i < BATTERY_ADC_SAMPLES; ++i) {
    sumMv += analogReadMilliVolts(BATTERY_ADC_PIN);
    delayMicroseconds(200);
  }
  return batteryMvFromAdcSum(sumMv, BATTERY_ADC_SAMPLES);
}

uint8_t zigbeeBatteryVoltage(uint16_t mv) {
  uint32_t value = (static_cast<uint32_t>(mv) + 50U) / 100U;
  if (value > 255U) value = 255U;
  return static_cast<uint8_t>(value);
}

uint8_t batteryPercentageZclRaw() {
  return batteryValid ? static_cast<uint8_t>(batteryPercent * 2U) : BATTERY_ZCL_UNKNOWN;
}

uint8_t batteryVoltageZclRaw() {
  return batteryValid ? zigbeeBatteryVoltage(batteryMv) : BATTERY_ZCL_UNKNOWN;
}

int16_t batteryDcVoltageZclRaw() {
  return batteryValid ? static_cast<int16_t>(batteryMv) : DC_VOLTAGE_UNKNOWN;
}

void sampleBattery() {
  batteryMv = readBatteryMv();
  batteryValid = batteryMv >= BATTERY_VALID_MIN_MV && batteryMv <= BATTERY_VALID_MAX_MV;
  batteryPercent = batteryValid ? batteryPercentFromMv(batteryMv) : 0;
  lastBatterySampleMs = millis();
  Serial.printf("[BAT] %u mV, %u%%, %s\n", batteryMv, batteryPercent, batteryValid ? "valid" : "not connected/invalid");
}

void beginRuntimeBatterySample() {
  batterySampler.sumMv = 0;
  batterySampler.samples = 0;
  batterySampler.active = true;
}

bool serviceRuntimeBatterySample(uint32_t nowMs) {
  if (!batterySampler.active || !backgroundWorkAllowed()) return false;
  batterySampler.sumMv += analogReadMilliVolts(BATTERY_ADC_PIN);
  if (++batterySampler.samples < BATTERY_ADC_SAMPLES) return false;
  batterySampler.active = false;
  batteryMv = batteryMvFromAdcSum(batterySampler.sumMv, batterySampler.samples);
  batteryValid = batteryMv >= BATTERY_VALID_MIN_MV && batteryMv <= BATTERY_VALID_MAX_MV;
  batteryPercent = batteryValid ? batteryPercentFromMv(batteryMv) : 0;
  lastBatterySampleMs = nowMs;
  batteryTelemetryDirty = true;
  return true;
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

void updateBatteryStatus(uint32_t nowMs) {
  if (!batterySampler.active && (nowMs - lastBatterySampleMs) >= BATTERY_SAMPLE_INTERVAL_MS) {
    beginRuntimeBatterySample();
  }
  if (serviceRuntimeBatterySample(nowMs)) {
    zbBatterySensor.setBatteryTelemetryRaw(batteryPercentageZclRaw(), batteryVoltageZclRaw());
    zbBatterySensor.setDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, batteryDcVoltageZclRaw());
  }
  if (!backgroundWorkAllowed()) return;

  const bool periodic = (nowMs - lastBatteryReportMs) >= BATTERY_REPORT_INTERVAL_MS;
  if (!batteryTelemetryDirty && !periodic) return;
  if ((nowMs - lastBatteryReportAttemptMs) < BATTERY_REPORT_RETRY_MS) return;
  lastBatteryReportAttemptMs = nowMs;
  if (!Zigbee.connected()) return;

  bool changed = !batteryReportInitialized || (batteryValid != lastReportedBatteryValid);
  if (batteryReportInitialized && batteryValid && lastReportedBatteryValid) {
    const uint8_t pctDelta = static_cast<uint8_t>(abs(static_cast<int>(batteryPercent) -
                                                      static_cast<int>(lastReportedBatteryPercent)));
    const uint16_t mvDelta = static_cast<uint16_t>(abs(static_cast<int>(batteryMv) -
                                                       static_cast<int>(lastReportedBatteryMv)));
    changed = changed || pctDelta >= BATTERY_REPORT_DELTA_PERCENT || mvDelta >= BATTERY_REPORT_DELTA_MV;
  }
  if (!changed && !periodic) {
    batteryTelemetryDirty = false;
    return;
  }

  const bool pctOk = zbBatterySensor.reportBatteryPercentage();
  const bool voltOk = zbBatterySensor.reportDC(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE);
  if (!(pctOk && voltOk)) return;

  lastReportedBatteryPercent = batteryPercent;
  lastReportedBatteryMv = batteryMv;
  lastReportedBatteryValid = batteryValid;
  batteryReportInitialized = true;
  batteryTelemetryDirty = false;
  lastBatteryReportMs = nowMs;
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

  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12);
  sampleBattery();

  loadPersistentState();

  zigbeeLight.setStateChangedCallback(onZigbeeLightChange);
  zigbeeLight.setIdentifyChangedCallback(onIdentify);

  // EP1: HA Meter Interface with standard Power Configuration and Electrical Measurement/DCVoltage.
  if (!zbBatterySensor.addDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE) ||
      !zbBatterySensor.setDCMultiplierDivisor(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, 1, 1000) ||
      !zbBatterySensor.setDCMinMaxValue(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, 2500, 4400) ||
      !zbBatterySensor.addBatteryPowerConfiguration(batteryPercentageZclRaw(), batteryVoltageZclRaw()) ||
      !zbBatterySensor.setManufacturerAndModel(MANUFACTURER, "DBL-BAT")) {
    Serial.println("[FATAL] Failed to configure EP1 battery sensor");
    ESP.restart();
  }
  zbBatterySensor.setVersion(ZIGBEE_APP_VERSION);
  zbBatterySensor.setHardwareVersion(ZIGBEE_HW_VERSION);

  if (!Zigbee.addEndpoint(&zbBatterySensor)) {
    Serial.println("[FATAL] Failed to register EP1 battery endpoint");
    ESP.restart();
  }

  Serial.println("[ZB] Starting Zigbee stack...");
  if (!zigbeeLight.begin(activeOn, activeLevel)) {
    Serial.println("[FATAL] Zigbee light/stack startup failed; restarting");
    delay(500);
    ESP.restart();
  }

  // Make battery endpoint attributes match the sampled state after stack start.
  zbBatterySensor.setBatteryTelemetryRaw(batteryPercentageZclRaw(), batteryVoltageZclRaw());
  zbBatterySensor.setDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, batteryDcVoltageZclRaw());
  if (!zbBatterySensor.setDCReporting(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, 0, 300, 20)) {
    Serial.println("[WARN] Could not install default DCVoltage reporting; coordinator may configure it later");
  }

  Serial.println("[ZB] Stack started. Waiting for/joining network in background.");
}

void loop() {
  const uint32_t nowMs = millis();
  applyRequestedState(nowMs);
  updateButton(nowMs);
  updateIndicators(nowMs);
  updateBatteryStatus(nowMs);
  savePersistentStateIfNeeded(nowMs);
  delay(5);
}
