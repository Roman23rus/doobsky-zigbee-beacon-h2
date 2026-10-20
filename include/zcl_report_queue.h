#pragma once
#include <stdint.h>

namespace Doobsky {

// Tracks queue acceptance per ZCL attribute, not network delivery.
// Each accepted message is removed independently, with exponential backoff
// only for the ones that failed to enqueue.
struct ReportQueue {
  bool percentagePending = false;
  bool voltagePending = false;
  uint8_t failures = 0;

  void begin() {
    percentagePending = true;
    voltagePending = true;
    failures = 0;
  }

  void finishAttempt(bool percentageQueued, bool voltageQueued) {
    if (percentagePending && percentageQueued) percentagePending = false;
    if (voltagePending && voltageQueued) voltagePending = false;
    if (!percentagePending && !voltagePending) {
      failures = 0;
    } else if (failures < 5) {
      ++failures;
    }
  }

  bool complete() const {
    return !percentagePending && !voltagePending;
  }

  uint32_t retryMs() const {
    const uint8_t shift = failures > 4 ? 4 : failures;
    return 1000U << shift; // 1, 2, 4, 8, 16 sec capped
  }
};
}  // namespace Doobsky
