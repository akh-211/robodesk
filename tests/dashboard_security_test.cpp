#include "../DashboardSecurity.h"
#include <cassert>
#include <iostream>

int main(){
  assert(roboSameOriginHttp("robodesk.local","http://robodesk.local"));
  assert(roboSameOriginHttp("192.168.1.42:80","http://192.168.1.42:80"));
  assert(!roboSameOriginHttp("robodesk.local","http://attacker.example"));
  assert(!roboSameOriginHttp("robodesk.local","https://robodesk.local"));
  assert(!roboSameOriginHttp("robodesk.local","http://robodesk.local.evil"));
  assert(!roboSameOriginHttp("robodesk.local",""));
  assert(!roboSameOriginHttp("","http://robodesk.local"));
  assert(!roboSameOriginHttp(nullptr,"http://robodesk.local"));
  assert(!roboSameOriginHttp("robodesk.local",nullptr));
  std::cout<<"PASS: dashboard same-origin POST guard\n";
}
