#include "zigbee_light.h"

#include "config.h"
#include "zigbee_build_id.h"
#include "light_state_transition.h"
#include "light_command_transaction.h"
#include "ep/ZigbeeDimmableLight.h"
#include <cstring>

using namespace BeaconConfig;

namespace Doobsky {

SafeDimmableLight::SafeDimmableLight(uint8_t endpoint) : ZigbeeEP(endpoint) {
  _device_id = ESP_ZB_HA_DIMMABLE_LIGHT_DEVICE_ID;
  zigbee_dimmable_light_cfg_t cfg = ZIGBEE_DEFAULT_DIMMABLE_LIGHT_CONFIG();
  _cluster_list = esp_zb_zcl_cluster_list_create();
  esp_zb_cluster_list_add_basic_cluster(
      _cluster_list, esp_zb_basic_cluster_create(&cfg.basic_cfg),
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_identify_cluster(
      _cluster_list, esp_zb_identify_cluster_create(&cfg.identify_cfg),
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_groups_cluster(
      _cluster_list, esp_zb_groups_cluster_create(&cfg.groups_cfg),
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_scenes_cluster(
      _cluster_list, esp_zb_scenes_cluster_create(&cfg.scenes_cfg),
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_on_off_cluster(
      _cluster_list, esp_zb_on_off_cluster_create(&cfg.on_off_cfg),
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_level_cluster(
      _cluster_list, esp_zb_level_cluster_create(&cfg.level_cfg),
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  _ep_config = {.endpoint = endpoint,
                .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
                .app_device_id = ESP_ZB_HA_DIMMABLE_LIGHT_DEVICE_ID,
                .app_device_version = ZIGBEE_APP_VERSION};
}

bool SafeDimmableLight::addBuildId() {
  return addDoobskyBuildId(esp_zb_cluster_list_get_cluster(
      _cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
}

bool SafeDimmableLight::setInitialBatteryPowerSource() {
  auto *basic = esp_zb_cluster_list_get_cluster(
      _cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  if (!basic) return false;
  uint8_t source = static_cast<uint8_t>(ZB_POWER_SOURCE_BATTERY);
  if (esp_zb_cluster_update_attr(
          basic, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID,
          &source) != ESP_OK) return false;
  _power_source = ZB_POWER_SOURCE_BATTERY;
  return true;
}

void SafeDimmableLight::publishChange() {
  if (onChange_ != nullptr) onChange_(state_, level_);
}

void SafeDimmableLight::zbAttributeSet(
    const esp_zb_zcl_set_attr_value_message_t *message) {
  if (!message || !message->attribute.data.value) return;
  bool changed = false;
  if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF &&
      message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
      message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
    const bool next = *static_cast<bool *>(message->attribute.data.value);
    changed = (next != state_);
    state_ = next;
  } else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL &&
             message->attribute.id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID &&
             message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
    const uint8_t next = *static_cast<uint8_t *>(message->attribute.data.value);
    changed = (next != level_);
    level_ = next;
  }
  if (changed) publishChange();
}

bool SafeDimmableLight::setLocalChecked(bool on, uint8_t level) {
  if (!Zigbee.initialized() ||
      !esp_zb_lock_acquire(pdMS_TO_TICKS(100))) {
    ++writeErrors_;
    return false;
  }
  const bool oldState = state_;
  const auto writeOn = [&](bool value) {
    return esp_zb_zcl_set_attribute_val(
        _endpoint, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &value, false)
        == ESP_ZB_ZCL_STATUS_SUCCESS;
  };
  const auto writeLevel = [&](uint8_t value) {
    return esp_zb_zcl_set_attribute_val(
        _endpoint, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, &value, false)
        == ESP_ZB_ZCL_STATUS_SUCCESS;
  };
  const auto result = commitLocalLight(
      oldState, on, level, writeOn, writeLevel);

  // On failure (including rollback failure) read actual attributes while
  // still holding the Zigbee lock, not guessed shadow values.
  bool observedOn = result == LightCommitResult::Committed ? on : state_;
  uint8_t observedLevel = result == LightCommitResult::Committed ? level : level_;
  const auto *onAttr = esp_zb_zcl_get_attribute(
      _endpoint, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
  const auto *levelAttr = esp_zb_zcl_get_attribute(
      _endpoint, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID);
  if (onAttr && onAttr->data_p) memcpy(&observedOn, onAttr->data_p, sizeof(observedOn));
  if (levelAttr && levelAttr->data_p) memcpy(&observedLevel, levelAttr->data_p, sizeof(observedLevel));

  const bool changed = (observedOn != state_ || observedLevel != level_);
  state_ = observedOn;
  level_ = observedLevel;
  // Callback is Zigbee API-free (only a short mux/optical task notify).
  // Publish before releasing lock, preserving ordering vs remote writes.
  if (changed) publishChange();
  esp_zb_lock_release();
  if (result != LightCommitResult::Committed) ++writeErrors_;
  return result == LightCommitResult::Committed;
}

bool SafeDimmableLight::reportAttribute(
    uint16_t cluster, uint16_t attribute) {
  if (!Zigbee.connected() || !Zigbee.started() ||
      !esp_zb_lock_acquire(pdMS_TO_TICKS(80))) return false;
  esp_zb_zcl_report_attr_cmd_t req{};
  req.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  req.attributeID = attribute;
  req.clusterID = cluster;
  req.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  req.zcl_basic_cmd.src_endpoint = _endpoint;
  req.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
  const bool queued = esp_zb_zcl_report_attr_cmd_req(&req) == ESP_OK;
  esp_zb_lock_release();
  return queued;
}

bool SafeDimmableLight::reportOnOff() {
  return reportAttribute(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                         ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
}

bool SafeDimmableLight::reportLevel() {
  return reportAttribute(ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                         ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID);
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
  rememberedLevel_ = initialLevel;
  portEXIT_CRITICAL(&mux_);

  endpoint_.onLightChange(&ZigbeeLight::lightChangeThunk);
  endpoint_.onIdentify(&ZigbeeLight::identifyThunk);
  if (!endpoint_.setManufacturerAndModel(MANUFACTURER, MODEL) ||
      !endpoint_.addBuildId() ||
      !endpoint_.setInitialBatteryPowerSource()) return false;
  endpoint_.setVersion(ZIGBEE_APP_VERSION);
  endpoint_.setHardwareVersion(ZIGBEE_HW_VERSION);
  if (!Zigbee.addEndpoint(&endpoint_)) return false;
  Zigbee.setRxOnWhenIdle(ZIGBEE_RX_ON_WHEN_IDLE);
  if (!Zigbee.begin(ZIGBEE_END_DEVICE)) return false;
  return endpoint_.setLocalChecked(initialOn, initialLevel);
}

bool ZigbeeLight::setLocalState(bool on, uint8_t level) {
  const bool ok = endpoint_.setLocalChecked(on, normalizeLevel(level));
  if (ok) {
    portENTER_CRITICAL(&mux_);
    onReportPending_ = true;
    levelReportPending_ = true;
    reportFailureStreak_ = 0;
    lastReportAttemptMs_ = millis() - 1000U;
    portEXIT_CRITICAL(&mux_);
  }
  return ok;
}

void ZigbeeLight::service(uint32_t nowMs) {
  bool reportOn;
  bool reportLevel;
  uint8_t failures;
  uint32_t previous;
  portENTER_CRITICAL(&mux_);
  reportOn = onReportPending_;
  reportLevel = levelReportPending_;
  failures = reportFailureStreak_;
  previous = lastReportAttemptMs_;
  portEXIT_CRITICAL(&mux_);
  if (!reportOn && !reportLevel) return;
  const uint32_t retryMs = 1000U << (failures > 4 ? 4 : failures);
  if (nowMs - previous < retryMs || !Zigbee.connected()) return;
  const bool onOk = !reportOn || endpoint_.reportOnOff();
  const bool levelOk = !reportLevel || endpoint_.reportLevel();
  portENTER_CRITICAL(&mux_);
  lastReportAttemptMs_ = nowMs;
  if (reportOn && onOk) onReportPending_ = false;
  if (reportLevel && levelOk) levelReportPending_ = false;
  if (onReportPending_ || levelReportPending_) {
    if (reportFailureStreak_ < 5) ++reportFailureStreak_;
  } else {
    reportFailureStreak_ = 0;
  }
  portEXIT_CRITICAL(&mux_);
}

bool ZigbeeLight::on() const {
  portENTER_CRITICAL(&mux_);
  const bool value = on_;
  portEXIT_CRITICAL(&mux_);
  return value;
}

uint8_t ZigbeeLight::level() const {
  portENTER_CRITICAL(&mux_);
  const uint8_t value = level_;
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

void ZigbeeLight::identifyThunk(uint16_t seconds) {
  if (instance_ != nullptr) instance_->handleIdentify(seconds);
}

void ZigbeeLight::handleLightChange(bool state, uint8_t rawLevel) {
  StateChangedCallback callback = nullptr;
  portENTER_CRITICAL(&mux_);
  // 0xFF is ZCL "unknown": preserve remembered value, never set 100%!
  const uint8_t normalized = rawLevel == 0xFF ? level_ : rawLevel;
  const auto next = applyLightAttribute(
      {on_, level_, rememberedLevel_}, state, normalized, DEFAULT_LEVEL);
  const bool onChanged = on_ != next.on;
  const bool levelChanged = level_ != next.level;
  on_ = next.on;
  level_ = next.level;
  rememberedLevel_ = next.rememberedLevel;

  LightTrace &trace = traces_[traceHead_];
  trace = {++traceSequence_, state, rawLevel, next.level,
           onChanged, levelChanged};
  traceHead_ = static_cast<uint8_t>((traceHead_ + 1U) % TRACE_CAPACITY);
  if (traceCount_ == TRACE_CAPACITY) {
    traceTail_ = static_cast<uint8_t>((traceTail_ + 1U) % TRACE_CAPACITY);
    ++traceDropped_;
  } else {
    ++traceCount_;
  }
  callback = stateChangedCallback_;
  portEXIT_CRITICAL(&mux_);
  if (callback) callback(state, next.level);
}

bool ZigbeeLight::readTrace(LightTrace &result, uint32_t &seenSequence) {
  portENTER_CRITICAL(&mux_);
  if (traceCount_ == 0) {
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  result = traces_[traceTail_];
  traceTail_ = static_cast<uint8_t>((traceTail_ + 1U) % TRACE_CAPACITY);
  --traceCount_;
  seenSequence = result.sequence;
  portEXIT_CRITICAL(&mux_);
  return true;
}

uint32_t ZigbeeLight::droppedTraceCount() const {
  portENTER_CRITICAL(&mux_);
  const uint32_t count = traceDropped_;
  portEXIT_CRITICAL(&mux_);
  return count;
}

void ZigbeeLight::handleIdentify(uint16_t seconds) {
  IdentifyChangedCallback callback;
  portENTER_CRITICAL(&mux_);
  callback = identifyChangedCallback_;
  portEXIT_CRITICAL(&mux_);
  if (callback) callback(seconds != 0);
}

uint8_t ZigbeeLight::normalizeLevel(uint8_t level) {
  return (level == 0 || level == 0xFF || level > ZIGBEE_MAX_LEVEL)
      ? DEFAULT_LEVEL : level;
}

}  // namespace Doobsky
