#pragma once
#include <stdint.h>

namespace Doobsky {

// Pure ZCL write/rollback policy; does not call callbacks or block on radio.
// Write functions return true ONLY when the local attribute write succeeded.
enum class LightCommitResult : uint8_t {
  Committed, Rejected, RollbackFailed
};

template <typename WriteOn, typename WriteLevel>
LightCommitResult commitLocalLight(bool previousOn, bool requestedOn,
                                   uint8_t requestedLevel,
                                   WriteOn writeOn, WriteLevel writeLevel) {
  if (!writeOn(requestedOn)) {
    return LightCommitResult::Rejected;
  }
  if (!writeLevel(requestedLevel)) {
    return writeOn(previousOn)
        ? LightCommitResult::Rejected
        : LightCommitResult::RollbackFailed;
  }
  return LightCommitResult::Committed;
}
}  // namespace Doobsky
