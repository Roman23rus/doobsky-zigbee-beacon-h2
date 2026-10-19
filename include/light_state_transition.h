#pragma once
#include <stdint.h>

// Pure, portable part of Zigbee light state handling; callable from host tests.
namespace Doobsky {
struct LogicalLightState {
  bool on;
  uint8_t level;
  uint8_t rememberedLevel;
};

// The caller must normalize ZCL 0xFF before calling. Zero with ON is dark,
// zero with OFF preserves the last nonzero level for a future local toggle.
inline LogicalLightState applyLightAttribute(
    LogicalLightState oldState, bool on, uint8_t normalizedLevel,
    uint8_t defaultLevel = 254) {
  const uint8_t remembered = oldState.rememberedLevel != 0
      ? oldState.rememberedLevel : defaultLevel;
  LogicalLightState next{on, normalizedLevel, remembered};
  if (!on && normalizedLevel == 0) {
    next.level = remembered;
  } else if (normalizedLevel > 0) {
    next.rememberedLevel = normalizedLevel;
  }
  return next;
}
}  // namespace Doobsky
