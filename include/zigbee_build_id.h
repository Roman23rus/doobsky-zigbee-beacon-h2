#pragma once
#include "config.h"
#include "esp_zigbee_cluster.h"
#include <Zigbee.h>
#include <cstring>

// Basic SWBuildID (0x4000), Pascal-style ZCL string with static lifetime.
inline bool addDoobskyBuildId(esp_zb_attribute_list_t *basic) {
  if (basic == nullptr) return false;
  constexpr size_t len = sizeof(BeaconConfig::FW_VERSION) - 1;
  static_assert(len > 0 && len <= 31, "ZCL build identifier must be 1..31 bytes");
  static uint8_t buildId[len + 1] = {};
  buildId[0] = static_cast<uint8_t>(len);
  memcpy(buildId + 1, BeaconConfig::FW_VERSION, len);
  return esp_zb_basic_cluster_add_attr(
      basic, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, buildId) == ESP_OK;
}
