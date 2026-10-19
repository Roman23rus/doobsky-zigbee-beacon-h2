#include <cassert>
#include "light_state_transition.h"
using namespace Doobsky;
int main() {
  LogicalLightState a{false, 254, 254};
  auto on = applyLightAttribute(a, true, 254);
  assert(on.on && on.level == 254 && on.rememberedLevel == 254);
  auto off = applyLightAttribute(on, false, 0);
  assert(!off.on && off.level == 254 && off.rememberedLevel == 254);
  auto onZero = applyLightAttribute(off, true, 0);
  assert(onZero.on && onZero.level == 0 && onZero.rememberedLevel == 254);
  auto half = applyLightAttribute(onZero, true, 127);
  assert(half.on && half.level == 127 && half.rememberedLevel == 127);
  auto offZero = applyLightAttribute(half, false, 0);
  assert(!offZero.on && offZero.level == 127 && offZero.rememberedLevel == 127);
  auto one = applyLightAttribute(offZero, true, 1);
  assert(one.on && one.level == 1 && one.rememberedLevel == 1);
  auto max = applyLightAttribute(one, true, 254);
  assert(max.on && max.level == 254 && max.rememberedLevel == 254);
  auto unknownOld = applyLightAttribute({false,0,0},true,0);
  assert(unknownOld.on && unknownOld.level==0 && unknownOld.rememberedLevel==254);
  // A new ON command must not be overwritten by an older level "correction":
  // the transition is pure and has no out-of-band deferred ZCL mutation.
  auto latestOn = applyLightAttribute(offZero,true,127);
  assert(latestOn.on && latestOn.level==127);
  return 0;
}
