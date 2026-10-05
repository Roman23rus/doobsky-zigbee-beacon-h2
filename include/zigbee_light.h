#pragma once

#include <Arduino.h>
#include <Zigbee.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
  void service();
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

  // A coordinator can write CurrentLevel=0 before the Arduino callback runs.
  // The correction is deferred out of the Zigbee callback to avoid stack-lock
  // re-entry. Generation/in-flight tracking makes "latest command wins" even
  // if a new Zigbee command arrives while an older correction is executing.
  bool levelCorrectionPending_ = false;
  uint8_t levelCorrection_ = 0;
  uint32_t levelCorrectionGeneration_ = 0;
  TaskHandle_t levelCorrectionTask_ = nullptr;
  uint32_t levelCorrectionInFlightGeneration_ = 0;

  StateChangedCallback stateChangedCallback_ = nullptr;
  IdentifyChangedCallback identifyChangedCallback_ = nullptr;
};

}  // namespace Doobsky
