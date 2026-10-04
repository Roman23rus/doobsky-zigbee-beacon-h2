#pragma once

#include <Arduino.h>
#include <Zigbee.h>

namespace Doobsky {

class ZigbeeLight {
public:
  using StateChangedCallback = void (*)(bool on, uint8_t level);
  using IdentifyChangedCallback = void (*)(bool active);

  ZigbeeLight();
  ZigbeeLight(const ZigbeeLight &) = delete;
  ZigbeeLight &operator=(const ZigbeeLight &) = delete;

  bool begin(bool initialOn, uint8_t initialLevel);
  bool setLocalState(bool on, uint8_t level);
  bool on() const;
  uint8_t level() const;

  void setStateChangedCallback(StateChangedCallback callback);
  void setIdentifyChangedCallback(IdentifyChangedCallback callback);

private:
  static void lightChangeThunk(bool state, uint8_t level);
  static void identifyThunk(uint16_t timeSeconds);
  void handleLightChange(bool state, uint8_t level);
  void handleIdentify(uint16_t timeSeconds);
  static uint8_t normalizeLevel(uint8_t level);

  static ZigbeeLight *instance_;
  ZigbeeDimmableLight endpoint_;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  bool on_ = false;
  uint8_t level_ = 0;
  StateChangedCallback stateChangedCallback_ = nullptr;
  IdentifyChangedCallback identifyChangedCallback_ = nullptr;
};

}  // namespace Doobsky
