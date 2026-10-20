#include "battery_telemetry.h"

#include "config.h"
#include "zigbee_build_id.h"
#include "zcl/esp_zigbee_zcl_binary_input.h"

using namespace BeaconConfig;

namespace Doobsky {

static constexpr uint8_t BATTERY_ZCL_UNKNOWN = 0xFF;
static constexpr int16_t DC_VOLTAGE_UNKNOWN = INT16_MIN;

BatteryTelemetryEndpoint::BatteryTelemetryEndpoint(uint8_t endpoint)
    : ZigbeeElectricalMeasurement(endpoint) {
  _device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID;
  _ep_config.endpoint = endpoint;
  _ep_config.app_profile_id = ESP_ZB_AF_HA_PROFILE_ID;
  _ep_config.app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID;
  // HA Simple Sensor profile, descriptor revision 1.
  _ep_config.app_device_version = 1;
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

  if (!addDoobskyBuildId(basic)) return false;

  // HA Simple Sensor 0x000C mandatory server clusters: Basic, Identify,
  // Binary Input. Report whether the battery measurement is in-range.
  // (This is NOT an assertion that a discharged battery is physically absent.)
  esp_zb_binary_input_cluster_cfg_t input{};
  input.out_of_service = false;
  // Initial validity is derived from ZCL's unknown sentinel.
  input.present_value = percentageRaw != BATTERY_ZCL_UNKNOWN;
  input.status_flags = input.present_value ? 0U :
      ESP_ZB_ZCL_BINARY_INPUT_STATUS_FLAG_FAULT;
  auto *binary = esp_zb_binary_input_cluster_create(&input);
  if (!binary || esp_zb_cluster_list_add_binary_input_cluster(
          _cluster_list, binary, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE) != ESP_OK) {
    return false;
  }
  // Simple Sensor also requires Identify client: no server-side IdentifyTime.
  auto *identifyClient = esp_zb_zcl_attr_list_create(
      ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY);
  if (!identifyClient || esp_zb_cluster_list_add_identify_cluster(
          _cluster_list, identifyClient,
          ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE) != ESP_OK) {
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

bool BatteryTelemetryEndpoint::syncBatterySnapshot(
    uint8_t percentageRaw, uint8_t voltageRaw,
    int16_t dcMillivolts, bool valid) {
  if (!Zigbee.initialized() ||
      !esp_zb_lock_acquire(pdMS_TO_TICKS(100))) return false;

  // All five related attributes are committed while holding ONE Zigbee
  // lock; readers cannot observe mixed validity/voltage pairs mid-update.
  const auto set = [&](uint16_t cluster, uint16_t attr, void *value) {
    return esp_zb_zcl_set_attribute_val(
        _endpoint, cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        attr, value, false) == ESP_ZB_ZCL_STATUS_SUCCESS;
  };
  const uint16_t power = ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG;
  const bool pctOk = set(power,
      ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
      &percentageRaw);
  const bool voltageOk = set(power,
      ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &voltageRaw);
  const bool dcOk = set(ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
      ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_DC_VOLTAGE_ID, &dcMillivolts);
  const uint16_t binary = ESP_ZB_ZCL_CLUSTER_ID_BINARY_INPUT;
  uint8_t status = valid ? 0U : ESP_ZB_ZCL_BINARY_INPUT_STATUS_FLAG_FAULT;
  const bool presentOk = set(binary,
      ESP_ZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID, &valid);
  const bool flagsOk = set(binary,
      ESP_ZB_ZCL_ATTR_BINARY_INPUT_STATUS_FLAGS_ID, &status);
  esp_zb_lock_release();
  return pctOk && voltageOk && dcOk && presentOk && flagsOk;
}

bool BatteryTelemetryEndpoint::reportAttributeQueued(
    uint16_t cluster, uint16_t attribute) {
  if (!Zigbee.started() || !Zigbee.connected() ||
      !esp_zb_lock_acquire(pdMS_TO_TICKS(80))) return false;
  // Upstream reportDC() used an uninitialized report_attr_cmd_t.
  esp_zb_zcl_report_attr_cmd_t req{};
  req.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  req.attributeID = attribute;
  req.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  req.clusterID = cluster;
  req.zcl_basic_cmd.src_endpoint = _endpoint;
  req.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
  const bool queued = esp_zb_zcl_report_attr_cmd_req(&req) == ESP_OK;
  esp_zb_lock_release();
  return queued;
}

bool BatteryTelemetryEndpoint::reportBatteryPercentQueued() {
  return reportAttributeQueued(
      ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
      ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID);
}

bool BatteryTelemetryEndpoint::reportDCVoltageQueued() {
  return reportAttributeQueued(
      ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
      ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_DC_VOLTAGE_ID);
}

BatteryTelemetry::BatteryTelemetry()
    : endpoint_(BATTERY_SENSOR_ENDPOINT) {}

bool BatteryTelemetry::configureEndpoint() {
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12);

  // Measure before registering Zigbee endpoints, before optical work starts,
  // to expose valid ADC readings on the first coordinator interview.
  beginSample();
  while (sampler_.active) {
    serviceSample(millis());
  }
  Serial.printf("[BAT] preload=%umV valid=%u pct=%u ZCLpct=%u ZCLvoltage=%u\n",
                batteryMv_, batteryValid_, batteryPercent_,
                batteryPercentageZclRaw(), batteryVoltageZclRaw());

  if (!endpoint_.addDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE) ||
      !endpoint_.setDCMultiplierDivisor(
          ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, 1, 1000) ||
      !endpoint_.setDCMinMaxValue(
          ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE,
          BATTERY_VALID_MIN_MV, BATTERY_VALID_MAX_MV)) {
    return false;
  }
  if (!endpoint_.addBatteryPowerConfiguration(
          batteryPercentageZclRaw(), batteryVoltageZclRaw()) ||
      !endpoint_.setManufacturerAndModel(MANUFACTURER, "DBL-BAT")) {
    return false;
  }

  endpoint_.setVersion(ZIGBEE_APP_VERSION);
  endpoint_.setHardwareVersion(ZIGBEE_HW_VERSION);
  sampler_.active = false;
  return true;
}

bool BatteryTelemetry::startRuntime() {
  // Runtime sampling should continue even if one initial Zigbee attribute
  // operation fails transiently. Attribute synchronization is retried later
  // and reports are never sent until the local attributes are current.
  runtimeStarted_ = true;
  batteryAttributeSyncPending_ = true;
  return syncAttributes(millis(), true);
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
  if (samples == 0) return 0;

  // Fixed-point arithmetic avoids pulling floating-point work into the
  // low-frequency battery path on the RISC-V ESP32-H2.
  const uint64_t numerator =
      static_cast<uint64_t>(sumMv) *
      BATTERY_DIVIDER_NUMERATOR *
      BATTERY_CALIBRATION_PERMILLE;
  const uint64_t denominator =
      static_cast<uint64_t>(samples) *
      BATTERY_DIVIDER_DENOMINATOR *
      1000ULL;
  return static_cast<uint16_t>(
      (numerator + denominator / 2ULL) / denominator);
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
  Serial.printf("[BAT] sample=%umV valid=%u pct=%u rawPct=%u rawVoltage=%u rawDC=%d\n",
                batteryMv_, batteryValid_, batteryPercent_,
                batteryPercentageZclRaw(), batteryVoltageZclRaw(),
                batteryDcVoltageZclRaw());
  lastBatterySampleMs_ = nowMs;
  batteryTelemetryDirty_ = true;
  batteryAttributeSyncPending_ = true;
  // If another sample finishes during a partial report cycle, restart the
  // cycle for a fresh consistent snapshot (do not reuse stale queue success).
  reportCycleActive_ = false;
  reportQueue_.begin();
  return true;
}

bool BatteryTelemetry::syncAttributes(uint32_t nowMs, bool force) {
  if (!batteryAttributeSyncPending_) return true;
  if (!force &&
      (nowMs - lastBatteryAttributeAttemptMs_) < BATTERY_REPORT_RETRY_MS) {
    return false;
  }

  lastBatteryAttributeAttemptMs_ = nowMs;
  const bool snapshotOk = endpoint_.syncBatterySnapshot(
      batteryPercentageZclRaw(), batteryVoltageZclRaw(),
      batteryDcVoltageZclRaw(), batteryValid_);

  batteryAttributeSyncPending_ = !snapshotOk;
  static bool lastOk = false;
  const bool allOk = !batteryAttributeSyncPending_;
  if (force || allOk != lastOk) {
    Serial.printf("[ZCL] sync EP1 snapshot=%u value=%umV percent=%u\n",
                  snapshotOk, batteryMv_, batteryPercent_);
  }
  lastOk = allOk;
  return allOk;
}

bool BatteryTelemetry::needsService(uint32_t nowMs) const {
  if (!runtimeStarted_) return false;
  if (sampler_.active) return true;

  if ((nowMs - lastBatterySampleMs_) >= BATTERY_SAMPLE_INTERVAL_MS) {
    return true;
  }

  if (batteryAttributeSyncPending_) {
    return (nowMs - lastBatteryAttributeAttemptMs_) >=
           BATTERY_REPORT_RETRY_MS;
  }

  const bool periodic =
      (nowMs - lastBatteryReportMs_) >= BATTERY_REPORT_INTERVAL_MS;
  if (!batteryTelemetryDirty_ && !periodic && !reportCycleActive_) return false;

  return (nowMs - lastBatteryReportAttemptMs_) >=
      (reportCycleActive_ ? reportQueue_.retryMs() : BATTERY_REPORT_RETRY_MS);
}

void BatteryTelemetry::service(uint32_t nowMs, bool backgroundAllowed) {
  if (!runtimeStarted_ || !backgroundAllowed) return;

  if (!sampler_.active &&
      (nowMs - lastBatterySampleMs_) >= BATTERY_SAMPLE_INTERVAL_MS) {
    beginSample();
  }

  const bool sampleCompleted = serviceSample(nowMs);
  if (sampleCompleted) {
    if (!syncAttributes(nowMs, true)) return;
  } else if (batteryAttributeSyncPending_) {
    if (!syncAttributes(nowMs, false)) return;
  }

  const bool periodic =
      (nowMs - lastBatteryReportMs_) >= BATTERY_REPORT_INTERVAL_MS;
  if (!batteryTelemetryDirty_ && !periodic && !reportCycleActive_) return;
  if ((nowMs - lastBatteryReportAttemptMs_) <
      (reportCycleActive_ ? reportQueue_.retryMs() : BATTERY_REPORT_RETRY_MS)) return;

  lastBatteryReportAttemptMs_ = nowMs;
  if (!Zigbee.connected()) {
    static uint32_t lastNotConnectedLog = 0;
    if (lastNotConnectedLog == 0 ||
        nowMs - lastNotConnectedLog >= 30000U) {
      Serial.println("[ZCL] battery report deferred: Zigbee disconnected");
      lastNotConnectedLog = nowMs;
    }
    return;
  }

  if (!reportCycleActive_) {
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
    reportQueue_.begin();
    reportCycleActive_ = true;
  }

  // API return values mean "queued locally", NOT acknowledged by coordinator.
  // Keep separate pending bits: a failure in one attribute must not cause
  // rapid re-enqueue of the other attribute into the ZBOSS APS queue.
  const bool pctQueued = !reportQueue_.percentagePending ||
      endpoint_.reportBatteryPercentQueued();
  const bool dcQueued = !reportQueue_.voltagePending ||
      endpoint_.reportDCVoltageQueued();
  reportQueue_.finishAttempt(pctQueued, dcQueued);
  static uint32_t lastReportLog = 0;
  if (reportQueue_.complete() || lastReportLog == 0 ||
      nowMs - lastReportLog >= 15000U) {
    Serial.printf("[ZCL] EP1 reports queuedPct=%u queuedDC=%u pendingPct=%u pendingDC=%u retryMs=%lu\n",
                  pctQueued, dcQueued,
                  reportQueue_.percentagePending, reportQueue_.voltagePending,
                  static_cast<unsigned long>(reportQueue_.retryMs()));
    lastReportLog = nowMs;
  }
  if (!reportQueue_.complete()) return;

  lastReportedBatteryPercent_ = batteryPercent_;
  lastReportedBatteryMv_ = batteryMv_;
  lastReportedBatteryValid_ = batteryValid_;
  batteryReportInitialized_ = true;
  reportCycleActive_ = false;
  batteryTelemetryDirty_ = false;
  lastBatteryReportMs_ = nowMs;
}

void BatteryTelemetry::printDiagnostics() const {
  Serial.printf("[DIAG] EP1 BAT valid=%u mv=%u pct=%u attrPending=%u "
                "reportInit=%u dirty=%u sampleMs=%lu reportMs=%lu "
                "reportAttemptMs=%lu\n",
                batteryValid_, batteryMv_, batteryPercent_,
                batteryAttributeSyncPending_, batteryReportInitialized_,
                batteryTelemetryDirty_,
                static_cast<unsigned long>(lastBatterySampleMs_),
                static_cast<unsigned long>(lastBatteryReportMs_),
                static_cast<unsigned long>(lastBatteryReportAttemptMs_));
  Serial.printf("[DIAG] EP1 ZCL 0001/0020=%u 0001/0021=%u 0B04/0100=%d "
                "scale=1/1000\n",
                batteryVoltageZclRaw(), batteryPercentageZclRaw(),
                batteryDcVoltageZclRaw());
}

void BatteryTelemetry::requestImmediateReport() {
  batteryTelemetryDirty_ = true;
  batteryReportInitialized_ = false;
  reportCycleActive_ = false;
  reportQueue_.begin();
  lastBatteryReportAttemptMs_ = millis() - BATTERY_REPORT_RETRY_MS;
  Serial.println("[DIAG] Manual battery report requested");
}

void BatteryTelemetry::requestImmediateSample() {
  beginSample();
  Serial.println("[DIAG] Manual battery sample requested");
}

}  // namespace Doobsky