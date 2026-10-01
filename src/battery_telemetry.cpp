#include "battery_telemetry.h"

#include "config.h"

using namespace BeaconConfig;

namespace Doobsky {

static constexpr uint8_t BATTERY_ZCL_UNKNOWN = 0xFF;
static constexpr int16_t DC_VOLTAGE_UNKNOWN = INT16_MIN;

BatteryTelemetryEndpoint::BatteryTelemetryEndpoint(uint8_t endpoint)
    : ZigbeeElectricalMeasurement(endpoint) {
  _device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID;
  _ep_config.endpoint = endpoint;
  _ep_config.app_profile_id = ESP_ZB_AF_HA_PROFILE_ID;
  _ep_config.app_device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID;
  _ep_config.app_device_version = 0;
}

bool BatteryTelemetryEndpoint::addBatteryPowerConfiguration(
    uint8_t percentageRaw, uint8_t voltageRaw) {
  auto *basic = esp_zb_cluster_list_get_cluster(
      _cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  if (!basic) return false;
  uint8_t source = static_cast<uint8_t>(ZB_POWER_SOURCE_BATTERY);
  if (esp_zb_cluster_update_attr(
          basic, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &source) != ESP_OK) {
    return false;
  }

  auto *power = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG);
  if (!power) return false;
  if (esp_zb_power_config_cluster_add_attr(
          power,
          ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
          &percentageRaw) != ESP_OK) {
    return false;
  }
  if (esp_zb_power_config_cluster_add_attr(
          power,
          ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
          &voltageRaw) != ESP_OK) {
    return false;
  }
  if (esp_zb_cluster_list_add_power_config_cluster(
          _cluster_list, power, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE) != ESP_OK) {
    return false;
  }
  _power_source = ZB_POWER_SOURCE_BATTERY;
  return true;
}

bool BatteryTelemetryEndpoint::setBatteryTelemetryRaw(
    uint8_t percentageRaw, uint8_t voltageRaw) {
  const auto p = setClusterAttribute(
      ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
      &percentageRaw, false);
  const auto v = setClusterAttribute(
      ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
      &voltageRaw, false);
  return p == ESP_ZB_ZCL_STATUS_SUCCESS &&
         v == ESP_ZB_ZCL_STATUS_SUCCESS;
}

BatteryTelemetry::BatteryTelemetry()
    : endpoint_(BATTERY_SENSOR_ENDPOINT) {}

bool BatteryTelemetry::configureEndpoint() {
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12);

  if (!endpoint_.addDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE) ||
      !endpoint_.setDCMultiplierDivisor(
          ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, 1, 1000) ||
      !endpoint_.setDCMinMaxValue(
          ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE,
          BATTERY_VALID_MIN_MV, BATTERY_VALID_MAX_MV)) {
    return false;
  }
  if (!endpoint_.addBatteryPowerConfiguration(
          BATTERY_ZCL_UNKNOWN, BATTERY_ZCL_UNKNOWN) ||
      !endpoint_.setManufacturerAndModel(MANUFACTURER, "DBL-BAT")) {
    return false;
  }

  endpoint_.setVersion(ZIGBEE_APP_VERSION);
  endpoint_.setHardwareVersion(ZIGBEE_HW_VERSION);
  sampler_.active = true;
  sampler_.sumMv = 0;
  sampler_.samples = 0;
  return true;
}

bool BatteryTelemetry::startRuntime() {
  if (!endpoint_.setBatteryTelemetryRaw(
          batteryPercentageZclRaw(), batteryVoltageZclRaw())) {
    return false;
  }
  if (!endpoint_.setDCMeasurement(
          ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE,
          batteryDcVoltageZclRaw())) {
    return false;
  }
  runtimeStarted_ = true;
  return endpoint_.setDCReporting(
      ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, 0, 300, 20);
}

ZigbeeEP *BatteryTelemetry::endpoint() {
  return &endpoint_;
}

uint8_t BatteryTelemetry::batteryPercentFromMv(uint16_t mv) {
  struct Point {
    uint16_t mv;
    uint8_t pct;
  };
  static constexpr Point curve[] = {
      {3300, 0}, {3500, 7}, {3600, 15}, {3700, 30}, {3800, 50},
      {3900, 65}, {4000, 80}, {4100, 90}, {4200, 100},
  };
  if (mv <= curve[0].mv) return 0;
  const size_t last = (sizeof(curve) / sizeof(curve[0])) - 1;
  if (mv >= curve[last].mv) return 100;
  for (size_t i = 1; i <= last; ++i) {
    if (mv <= curve[i].mv) {
      const Point &lo = curve[i - 1];
      const Point &hi = curve[i];
      const uint32_t span = hi.mv - lo.mv;
      const uint32_t num =
          static_cast<uint32_t>(mv - lo.mv) * (hi.pct - lo.pct);
      return static_cast<uint8_t>(
          lo.pct + (num + span / 2U) / span);
    }
  }
  return 100;
}

uint16_t BatteryTelemetry::batteryMvFromAdcSum(
    uint32_t sumMv, uint8_t samples) {
  const float adcMv = static_cast<float>(sumMv) / samples;
  const float scaledMv =
      adcMv * BATTERY_DIVIDER_RATIO * BATTERY_CALIBRATION;
  return static_cast<uint16_t>(scaledMv + 0.5f);
}

uint8_t BatteryTelemetry::zigbeeBatteryVoltage(uint16_t mv) {
  uint32_t value = (static_cast<uint32_t>(mv) + 50U) / 100U;
  if (value > 255U) value = 255U;
  return static_cast<uint8_t>(value);
}

uint8_t BatteryTelemetry::batteryPercentageZclRaw() const {
  return batteryValid_
      ? static_cast<uint8_t>(batteryPercent_ * 2U)
      : BATTERY_ZCL_UNKNOWN;
}

uint8_t BatteryTelemetry::batteryVoltageZclRaw() const {
  return batteryValid_
      ? zigbeeBatteryVoltage(batteryMv_)
      : BATTERY_ZCL_UNKNOWN;
}

int16_t BatteryTelemetry::batteryDcVoltageZclRaw() const {
  return batteryValid_ ? static_cast<int16_t>(batteryMv_) : DC_VOLTAGE_UNKNOWN;
}

void BatteryTelemetry::beginSample() {
  sampler_.sumMv = 0;
  sampler_.samples = 0;
  sampler_.active = true;
}

bool BatteryTelemetry::serviceSample(uint32_t nowMs) {
  if (!sampler_.active) return false;

  sampler_.sumMv += analogReadMilliVolts(BATTERY_ADC_PIN);
  ++sampler_.samples;
  if (sampler_.samples < BATTERY_ADC_SAMPLES) return false;

  sampler_.active = false;
  batteryMv_ = batteryMvFromAdcSum(sampler_.sumMv, sampler_.samples);
  batteryValid_ = batteryMv_ >= BATTERY_VALID_MIN_MV &&
                  batteryMv_ <= BATTERY_VALID_MAX_MV;
  batteryPercent_ = batteryValid_ ? batteryPercentFromMv(batteryMv_) : 0;
  lastBatterySampleMs_ = nowMs;
  batteryTelemetryDirty_ = true;
  return true;
}

void BatteryTelemetry::service(uint32_t nowMs, bool backgroundAllowed) {
  if (!runtimeStarted_ || !backgroundAllowed) return;

  if (!sampler_.active &&
      (nowMs - lastBatterySampleMs_) >= BATTERY_SAMPLE_INTERVAL_MS) {
    beginSample();
  }

  if (serviceSample(nowMs)) {
    endpoint_.setBatteryTelemetryRaw(
        batteryPercentageZclRaw(), batteryVoltageZclRaw());
    endpoint_.setDCMeasurement(
        ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE,
        batteryDcVoltageZclRaw());
  }

  const bool periodic =
      (nowMs - lastBatteryReportMs_) >= BATTERY_REPORT_INTERVAL_MS;
  if (!batteryTelemetryDirty_ && !periodic) return;
  if ((nowMs - lastBatteryReportAttemptMs_) < BATTERY_REPORT_RETRY_MS) return;
  lastBatteryReportAttemptMs_ = nowMs;
  if (!Zigbee.connected()) return;

  bool changed = !batteryReportInitialized_ ||
                 (batteryValid_ != lastReportedBatteryValid_);
  if (batteryReportInitialized_ && batteryValid_ &&
      lastReportedBatteryValid_) {
    const uint8_t pctDelta = static_cast<uint8_t>(abs(
        static_cast<int>(batteryPercent_) -
        static_cast<int>(lastReportedBatteryPercent_)));
    const uint16_t mvDelta = static_cast<uint16_t>(abs(
        static_cast<int>(batteryMv_) -
        static_cast<int>(lastReportedBatteryMv_)));
    changed = changed || pctDelta >= BATTERY_REPORT_DELTA_PERCENT ||
              mvDelta >= BATTERY_REPORT_DELTA_MV;
  }

  if (!changed && !periodic) {
    batteryTelemetryDirty_ = false;
    return;
  }

  const bool pctOk = endpoint_.reportBatteryPercentage();
  const bool voltOk =
      endpoint_.reportDC(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE);
  if (!(pctOk && voltOk)) return;

  lastReportedBatteryPercent_ = batteryPercent_;
  lastReportedBatteryMv_ = batteryMv_;
  lastReportedBatteryValid_ = batteryValid_;
  batteryReportInitialized_ = true;
  batteryTelemetryDirty_ = false;
  lastBatteryReportMs_ = nowMs;
}

}  // namespace Doobsky
