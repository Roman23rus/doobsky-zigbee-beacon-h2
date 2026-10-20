#pragma once

#include <Arduino.h>
#include <Zigbee.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace Doobsky {

// Replaces the Arduino ZigbeeDimmableLight setter: that setter calls the
// physical-output callback BEFORE its two ZCL writes, even on failure.
class SafeDimmableLight : public ZigbeeEP {
 public:
  explicit SafeDimmableLight(uint8_t endpoint);
  bool addBuildId();
  bool setInitialBatteryPowerSource();
  void onLightChange(void (*callback)(bool, uint8_t)) { onChange_ = callback; }
  bool setLocalChecked(bool on, uint8_t level);
  bool reportOnOff();
  bool reportLevel();
  uint32_t localWriteErrors() const { return writeErrors_; }

  void zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) override;

 private:
  void publishChange();
  bool reportAttribute(uint16_t cluster, uint16_t attribute);
  bool state_ = false;
  uint8_t level_ = 254;
  void (*onChange_)(bool, uint8_t) = nullptr;
  uint32_t writeErrors_ = 0;
};

class ZigbeeLight {
 public:
  using StateChangedCallback = void (*)(bool on, uint8_t level);
  using IdentifyChangedCallback = void (*)(bool active);
  ZigbeeLight();
  ZigbeeLight(const ZigbeeLight &) = delete;
  ZigbeeLight &operator=(const ZigbeeLight &) = delete;

  bool begin(bool initialOn, uint8_t initialLevel);
  bool setLocalState(bool on, uint8_t level);
  void service(uint32_t nowMs);
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
  bool readTrace(LightTrace &trace, uint32_t &seenSequence);
  uint32_t droppedTraceCount() const;

  void setStateChangedCallback(StateChangedCallback callback);
  void setIdentifyChangedCallback(IdentifyChangedCallback callback);

 private:
  static void lightChangeThunk(bool state, uint8_t level);
  static void identifyThunk(uint16_t timeSeconds);
  void handleLightChange(bool state, uint8_t level);
  void handleIdentify(uint16_t timeSeconds);
  static uint8_t normalizeLevel(uint8_t level);

  static ZigbeeLight *instance_;
  SafeDimmableLight endpoint_;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  bool on_ = false;
  uint8_t level_ = 0;
  uint8_t rememberedLevel_ = 254;
  uint32_t traceSequence_ = 0;
  static constexpr uint8_t TRACE_CAPACITY = 12;
  LightTrace traces_[TRACE_CAPACITY] = {};
  uint8_t traceHead_ = 0;
  uint8_t traceTail_ = 0;
  uint8_t traceCount_ = 0;
  uint32_t traceDropped_ = 0;

  bool onReportPending_ = false;
  bool levelReportPending_ = false;
  uint8_t reportFailureStreak_ = 0;
  uint32_t lastReportAttemptMs_ = 0;
  StateChangedCallback stateChangedCallback_ = nullptr;
  IdentifyChangedCallback identifyChangedCallback_ = nullptr;
};

}  // namespace Doobsky