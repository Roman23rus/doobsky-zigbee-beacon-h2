#pragma once

#include <Arduino.h>
#include <Zigbee.h>
#include "zcl/esp_zigbee_zcl_power_config.h"

namespace Doobsky {

class BatteryTelemetryEndpoint : public ZigbeeElectricalMeasurement {
public:
  explicit BatteryTelemetryEndpoint(uint8_t endpoint);
  bool addBatteryPowerConfiguration(uint8_t percentageRaw, uint8_t voltageRaw);
  bool setBatteryTelemetryRaw(uint8_t percentageRaw, uint8_t voltageRaw);
};

class BatteryTelemetry {
public:
  BatteryTelemetry();
  BatteryTelemetry(const BatteryTelemetry &) = delete;
  BatteryTelemetry &operator=(const BatteryTelemetry &) = delete;

  bool configureEndpoint();
  bool startRuntime();
  void service(uint32_t nowMs, bool backgroundAllowed);
  ZigbeeEP *endpoint();

  bool valid() const { return batteryValid_; }
  uint16_t millivolts() const { return batteryMv_; }
  uint8_t percent() const { return batteryPercent_; }

private:
  struct Sampler {
    uint32_t sumMv = 0;
    uint8_t samples = 0;
    bool active = true;
  };

  static uint8_t batteryPercentFromMv(uint16_t mv);
  static uint16_t batteryMvFromAdcSum(uint32_t sumMv, uint8_t samples);
  static uint8_t zigbeeBatteryVoltage(uint16_t mv);

  uint8_t batteryPercentageZclRaw() const;
  uint8_t batteryVoltageZclRaw() const;
  int16_t batteryDcVoltageZclRaw() const;
  bool serviceSample(uint32_t nowMs);
  void beginSample();

  BatteryTelemetryEndpoint endpoint_;
  Sampler sampler_;
  uint16_t batteryMv_ = 0;
  uint8_t batteryPercent_ = 0;
  bool batteryValid_ = false;

  uint8_t lastReportedBatteryPercent_ = 0;
  uint16_t lastReportedBatteryMv_ = 0;
  uint32_t lastBatterySampleMs_ = 0;
  uint32_t lastBatteryReportMs_ = 0;
  uint32_t lastBatteryReportAttemptMs_ = 0;
  bool batteryReportInitialized_ = false;
  bool lastReportedBatteryValid_ = false;
  bool batteryTelemetryDirty_ = true;
  bool runtimeStarted_ = false;
};

}  // namespace Doobsky
