#include "CompanionInteraction.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace companion;

int main(){
  float temperature=23.5f,humidity=46.f;uint32_t validAt=100;
  assert(!updateAhtSample(false,99.f,99.f,200,temperature,humidity,validAt));
  assert(temperature==23.5f&&humidity==46.f&&validAt==100);
  assert(!updateAhtSample(true,NAN,50.f,200,temperature,humidity,validAt));
  assert(temperature==23.5f&&humidity==46.f&&validAt==100);
  assert(!updateAhtSample(true,90.f,50.f,200,temperature,humidity,validAt));
  assert(!updateAhtSample(true,24.f,101.f,200,temperature,humidity,validAt));
  assert(updateAhtSample(true,24.f,51.f,200,temperature,humidity,validAt));
  assert(temperature==24.f&&humidity==51.f&&validAt==200);

  float pressure=1013.f;uint32_t pressureAt=300;
  assert(updatePressureSample(true,1014.f,400,pressure,pressureAt));
  assert(pressure==1014.f&&pressureAt==400);
  assert(!updatePressureSample(false,900.f,500,pressure,pressureAt));
  assert(pressure==1014.f&&pressureAt==400);
  assert(!updatePressureSample(true,NAN,500,pressure,pressureAt));
  assert(!updatePressureSample(true,0.f,500,pressure,pressureAt));
  assert(!updatePressureSample(true,1200.f,500,pressure,pressureAt));
  assert(updatePressureSample(true,1015.f,500,pressure,pressureAt));
  assert(pressure==1015.f&&pressureAt==500);
  assert(sampleFresh(500,500,1));
  assert(sampleFresh(0xfffffff0u,0x00000020u,49u));
  assert(!sampleFresh(0xfffffff0u,0x00000020u,48u));
  assert(!sampleFresh(0,100,1000));
  assert(!sampleFresh(500,501,1));

  EnvironmentTrend trend;trend.setMetricsEnabled(true);
  for(uint32_t i=0;i<5;++i){const uint32_t at=100u+i*2000u;assert(!trend.observe(24.f,50.f,1013.f,true,true,at,at+10));}
  assert(!trend.observe(26.5f,50.f,1013.f,true,true,10100,10110));
  assert(trend.observe(26.5f,50.f,1013.f,true,true,12100,12110));
  assert(!trend.observe(26.5f,50.f,1013.f,true,true,12100,12111));
  assert(!trend.observe(29.f,50.f,1013.f,true,true,14100,14110));
  assert(!trend.observe(29.f,50.f,1013.f,true,true,16100,16110));assert(trend.cooldownSuppressed()==1);
  assert(!trend.observe(29.f,50.f,1013.f,false,true,18100,18110));
  assert(!trend.observe(29.f,50.f,1013.f,true,true,18100,50000));

  std::cout<<"PASS: AHT/BMP sample validity, sensor freshness, environment cooldown and millis wraparound\n";
}
