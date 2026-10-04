#include "zigbee_light.h"

#include "config.h"

using namespace BeaconConfig;

namespace Doobsky {

ZigbeeLight *ZigbeeLight::instance_ = nullptr;

ZigbeeLight::ZigbeeLight() : endpoint_(LIGHT_ENDPOINT) {}

bool ZigbeeLight::begin(bool initialOn, uint8_t initialLevel) {
  if (instance_ != nullptr && instance_ != this) return false;
  instance_ = this;

  initialLevel = normalizeLevel(initialLevel);
  portENTER_CRITICAL(&mux_);
  on_ = initialOn;
  level_ = initialLevel;
  portEXIT_CRITICAL(&mux_);

  endpoint_.onLightChange(&ZigbeeLight::lightChangeThunk);
  endpoint_.onIdentify(&ZigbeeLight::identifyThunk);

  if (!endpoint_.setManufacturerAndModel(MANUFACTURER, MODEL)) return false;
  endpoint_.setVersion(ZIGBEE_APP_VERSION);
  endpoint_.setHardwareVersion(ZIGBEE_HW_VERSION);

  if (!Zigbee.addEndpoint(&endpoint_)) return false;

  Zigbee.setRxOnWhenIdle(true);
  if (!Zigbee.begin(ZIGBEE_END_DEVICE)) return false;

  return endpoint_.setLight(initialOn, initialLevel);
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

void ZigbeeLight::handleLightChange(bool state, uint8_t level) {
  level = normalizeLevel(level);
  StateChangedCallback callback;
  portENTER_CRITICAL(&mux_);

  // Some coordinators drive CurrentLevel to 0 when sending OFF. Treat that
  // as an OFF transport detail, not as the user's remembered brightness.
  // This keeps the last non-zero level available across OFF and power loss.
  if (!state && level == 0) {
    level = (level_ == 0 || level_ > ZIGBEE_MAX_LEVEL) ? DEFAULT_LEVEL : level_;
  }

  on_ = state;
  level_ = level;
  callback = stateChangedCallback_;
  portEXIT_CRITICAL(&mux_);
  if (callback != nullptr) callback(state, level);
}

void ZigbeeLight::handleIdentify(uint16_t timeSeconds) {
  const bool active = (timeSeconds != 0);
  IdentifyChangedCallback callback;
  portENTER_CRITICAL(&mux_);
  identifyActive_ = active;
  callback = identifyChangedCallback_;
  portEXIT_CRITICAL(&mux_);
  if (callback != nullptr) callback(active);
}

uint8_t ZigbeeLight::normalizeLevel(uint8_t level) {
  return (level == 0xFF || level > ZIGBEE_MAX_LEVEL) ? DEFAULT_LEVEL : level;
}

}  // namespace Doobsky
