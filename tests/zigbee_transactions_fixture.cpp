#include <cassert>
#include <vector>
#include "light_command_transaction.h"
#include "zcl_report_queue.h"
using namespace Doobsky;

int main() {
  std::vector<int> writes;
  auto on = [&](bool v) { writes.push_back(v ? 1 : 0); return true; };
  auto level = [&](uint8_t v) { writes.push_back(1000+v); return true; };
  assert(commitLocalLight(false, true, 100, on, level) == LightCommitResult::Committed);
  assert((writes == std::vector<int>{1,1100}));
  writes.clear();
  auto failOn = [&](bool v) { writes.push_back(v ? 1 : 0); return false; };
  assert(commitLocalLight(false,true, 100,failOn,level) == LightCommitResult::Rejected);
  assert((writes == std::vector<int>{1}));
  writes.clear();
  auto failLevel = [&](uint8_t) { writes.push_back(1000); return false; };
  assert(commitLocalLight(false,true,120,on,failLevel)==LightCommitResult::Rejected);
  assert((writes==std::vector<int>{1,1000,0}));
  writes.clear();
  auto rollbackFails = [&](bool v) {writes.push_back(v?1:0);return v;};
  assert(commitLocalLight(false,true,120,rollbackFails,failLevel)
      == LightCommitResult::RollbackFailed);
  assert((writes==std::vector<int>{1,1000,0}));

  ReportQueue q;
  q.begin();
  assert(q.percentagePending && q.voltagePending && q.retryMs()==1000);
  q.finishAttempt(true,false);
  assert(!q.percentagePending && q.voltagePending);
  assert(q.retryMs()==2000);
  q.finishAttempt(true,false);
  assert(q.retryMs()==4000 && q.voltagePending);
  q.finishAttempt(true,false);
  q.finishAttempt(true,false);
  q.finishAttempt(true,false);
  assert(q.retryMs()==16000);
  q.finishAttempt(true,true);
  assert(q.complete() && q.retryMs()==1000);
  q.begin();
  assert(q.percentagePending && q.voltagePending);
  return 0;
}