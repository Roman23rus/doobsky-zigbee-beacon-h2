#pragma once

#include <Arduino.h>
#include <Zigbee.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace Doobsky {

class BuildIdDimmableLight : public ZigbeeDimmableLight {
public:
  using ZigbeeDimmableLight::ZigbeeDimmableLight;
  bool addBuildId();
};

class ZigbeeLight {
public:
  using StateChangedCallback = void (*)(bool on, uint8_t level);
  using IdentifyChangedCallback = void (*)(bool active);

  ZigbeeLight();
  ZigbeeLight(const ZigbeeLight &) = delete;
  ZigbeeLight &operator=(const ZigbeeLight &) = delete;

  bool begin(bool initialOn, uint8_t initialLevel);
  bool setBasicPowerSourceBattery();
  bool setLocalState(bool on, uint8_t level);
  bool on() const;
  uint8_t level() const;
  struct LightTrace {
    uint32_t sequence;
    bool on;
    uint8_t rawLevel;
    uint8_t effectiveLevel;
    bool onChanged;
    bool levelChanged;
  };
  bool readTrace(LightTrace &trace, uint32_t &seenSequence) const;

  void setStateChangedCallback(StateChangedCallback callback);
  void setIdentifyChangedCallback(IdentifyChangedCallback callback);

private:
  static void lightChangeThunk(bool state, uint8_t level);
  static void identifyThunk(uint16_t timeSeconds);
  void handleLightChange(bool state, uint8_t level);
  void handleIdentify(uint16_t timeSeconds);
  static uint8_t normalizeLevel(uint8_t level);

  static ZigbeeLight *instance_;
  BuildIdDimmableLight endpoint_;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  bool on_ = false;
  uint8_t level_ = 0;

  // Track physical brightness independently of ZCL zero while OFF.
  // Never call Arduino setLightLevel() for correction: its implementation also
  // rewrites OnOff and can overwrite a newer coordinator command.
  uint8_t rememberedLevel_ = 254;
  uint32_t traceSequence_ = 0;
  LightTrace lastTrace_ = {};

  StateChangedCallback stateChangedCallback_ = nullptr;
  IdentifyChangedCallback identifyChangedCallback_ = nullptr;
};

}  // namespace Doobsky
