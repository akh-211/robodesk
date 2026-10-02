#include "../OtaWorkflowPolicy.h"
#include <cassert>
#include <iostream>

int main() {
  using Result=RoboOtaStartResult;
  assert(roboOtaStartDecision(true,false,false,false,false)==Result::Accepted);
  assert(roboOtaStartDecision(true,true,false,false,false)==Result::Busy);
  assert(roboOtaStartDecision(true,false,true,false,false)==Result::Busy);
  assert(roboOtaStartDecision(true,true,false,true,false)==Result::Busy);
  assert(roboOtaStartDecision(true,false,false,true,false)==Result::NoCandidate);
  assert(roboOtaStartDecision(true,false,false,true,true)==Result::Accepted);
  assert(roboOtaStartDecision(false,false,false,false,false)==Result::RollbackDisabled);
  using Preparation=RoboOtaPreparationStep;
  assert(roboOtaPreparationDecision(true,false,0,false,0)==Preparation::Rejected);
  assert(roboOtaPreparationDecision(false,false,12999,false,0)==Preparation::Wait);
  assert(roboOtaPreparationDecision(false,false,13000,false,0)==Preparation::TimedOut);
  assert(roboOtaPreparationDecision(false,true,200,false,0)==Preparation::Wait);
  assert(roboOtaPreparationDecision(false,true,299,true,99)==Preparation::Wait);
  assert(roboOtaPreparationDecision(false,true,300,true,100)==Preparation::Launch);
  assert(!roboOtaVersionEligible(6,2,6));
  assert(roboOtaVersionEligible(7,2,6));
  assert(!roboOtaVersionEligible(7,7,6));
  assert(!roboOtaVersionEligible(7,8,6));
  assert(roboOtaVersionEligible(9,8,6));
  assert(!roboOtaVersionEligible(0,0,0));
  assert(roboOtaVersionEligible(UINT32_MAX,UINT32_MAX-1,6));
  std::cout << "PASS: OTA start gates and signed-release version floor\n";
}
