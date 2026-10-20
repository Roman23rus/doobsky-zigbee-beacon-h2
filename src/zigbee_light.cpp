#include "zigbee_light.h"

#include "config.h"
#include "zigbee_build_id.h"
#include "light_state_transition.h"
#include "esp_zigbee_secur.h"

using namespace BeaconConfig;

namespace Doobsky {

bool BuildIdDimmableLight::addBuildId() {
  return addDoobskyBuildId(esp_zb_cluster_list_get_cluster(
      _cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
}

ZigbeeLight *ZigbeeLight::instance_ = nullptr;

ZigbeeLight::ZigbeeLight() : endpoint_(LIGHT_ENDPOINT) {}

bool ZigbeeLight::begin(bool initialOn, uint8_t initialLevel) {
  if (instance_ != nullptr && instance_ != this) return false;
  instance_ = this;

  initialLevel = normalizeLevel(initialLevel);
  portENTER_CRITICAL(&mux_);
  on_ = initialOn;
  level_ = initialLevel;
  rememberedLevel_ = initialLevel == 0 ? DEFAULT_LEVEL : initialLevel;
  portEXIT_CRITICAL(&mux_);

  endpoint_.onLightChange(&ZigbeeLight::lightChangeThunk);
  endpoint_.onIdentify(&ZigbeeLight::identifyThunk);

  if (!endpoint_.setManufacturerAndModel(MANUFACTURER, MODEL) ||
      !endpoint_.addBuildId()) return false;
  endpoint_.setVersion(ZIGBEE_APP_VERSION);
  endpoint_.setHardwareVersion(ZIGBEE_HW_VERSION);

  if (!Zigbee.addEndpoint(&endpoint_)) return false;

  Zigbee.setRxOnWhenIdle(ZIGBEE_RX_ON_WHEN_IDLE);
  if (!Zigbee.begin(ZIGBEE_END_DEVICE)) return false;

  return endpoint_.setLight(initialOn, initialLevel);
}

bool ZigbeeLight::setBasicPowerSourceBattery() {
  // Calls from the Arduino loop run outside the Zigbee task.
  // Bound the wait so the beacon's optical timing remains independent.
  if (!esp_zb_lock_acquire(pdMS_TO_TICKS(10))) return false;
  uint8_t source = static_cast<uint8_t>(ZB_POWER_SOURCE_BATTERY);
  const auto status = esp_zb_zcl_set_attribute_val(
      LIGHT_ENDPOINT,
      ESP_ZB_ZCL_CLUSTER_ID_BASIC,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID,
      &source, false);
  esp_zb_lock_release();
  return status == ESP_ZB_ZCL_STATUS_SUCCESS;
}

bool ZigbeeLight::setLocalState(bool on, uint8_t level) {
  return endpoint_.setLight(on, normalizeLevel(level));
}

bool ZigbeeLight::on() const {
  bool value;
  portENTER_CRITICAL(&mux_);
  value = on_;
  portEXIT_CRITICAL(&mux_);
  return value;
}

uint8_t ZigbeeLight::level() const {
  uint8_t value;
  portENTER_CRITICAL(&mux_);
  value = level_;
  portEXIT_CRITICAL(&mux_);
  return value;
}

void ZigbeeLight::setStateChangedCallback(StateChangedCallback callback) {
  portENTER_CRITICAL(&mux_);
  stateChangedCallback_ = callback;
  portEXIT_CRITICAL(&mux_);
}

void ZigbeeLight::setIdentifyChangedCallback(IdentifyChangedCallback callback) {
  portENTER_CRITICAL(&mux_);
  identifyChangedCallback_ = callback;
  portEXIT_CRITICAL(&mux_);
}

void ZigbeeLight::lightChangeThunk(bool state, uint8_t level) {
  if (instance_ != nullptr) instance_->handleLightChange(state, level);
}

void ZigbeeLight::identifyThunk(uint16_t timeSeconds) {
  if (instance_ != nullptr) instance_->handleIdentify(timeSeconds);
}

void ZigbeeLight::handleLightChange(bool state, uint8_t rawLevel) {
  const uint8_t normalized = normalizeLevel(rawLevel);
  StateChangedCallback callback = nullptr;
  uint8_t effectiveLevel = normalized;
  portENTER_CRITICAL(&mux_);
  // Compute ON/OFF/zero transition without any ZCL write from this task.
  const auto next = applyLightAttribute({on_, level_, rememberedLevel_},
                                        state, normalized, DEFAULT_LEVEL);
  effectiveLevel = next.level;
  const bool onChanged = on_ != next.on;
  const bool levelChanged = level_ != next.level;
  on_ = next.on;
  level_ = next.level;
  rememberedLevel_ = next.rememberedLevel;
  // Record only scalar data in the Zigbee callback. Serial logging is deferred
  // to main loop to avoid blocking a stack callback.
  lastTrace_ = {++traceSequence_, state, rawLevel, effectiveLevel,
                onChanged, levelChanged};
  callback = stateChangedCallback_;
  portEXIT_CRITICAL(&mux_);
  if (callback != nullptr) callback(state, effectiveLevel);
}

bool ZigbeeLight::readTrace(LightTrace &result, uint32_t &seenSequence) const {
  portENTER_CRITICAL(&mux_);
  if (lastTrace_.sequence == seenSequence) {
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  result = lastTrace_;
  seenSequence = lastTrace_.sequence;
  portEXIT_CRITICAL(&mux_);
  return true;
}

void ZigbeeLight::handleIdentify(uint16_t timeSeconds) {
  const bool active = (timeSeconds != 0);
  IdentifyChangedCallback callback;
  portENTER_CRITICAL(&mux_);
  callback = identifyChangedCallback_;
  portEXIT_CRITICAL(&mux_);
  if (callback != nullptr) callback(active);
}

uint8_t ZigbeeLight::normalizeLevel(uint8_t level) {
  return (level == 0xFF || level > ZIGBEE_MAX_LEVEL) ? DEFAULT_LEVEL : level;
}

}  // namespace Doobsky