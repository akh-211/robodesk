#pragma once
#include <math.h>
#include <stdint.h>

namespace companion {
inline bool updateAhtSample(bool readOk,float temperatureC,float humidityPct,uint32_t now,float& lastTemperatureC,float& lastHumidityPct,uint32_t& lastGoodAt){
  if(!readOk||!isfinite(temperatureC)||temperatureC<-40.f||temperatureC>85.f||!isfinite(humidityPct)||humidityPct<0.f||humidityPct>100.f)return false;
  lastTemperatureC=temperatureC;lastHumidityPct=humidityPct;lastGoodAt=now;return true;
}

inline bool updatePressureSample(bool readOk,float pressureHpa,uint32_t now,float& lastPressureHpa,uint32_t& lastGoodAt){
  if(!readOk||!isfinite(pressureHpa)||pressureHpa<300.f||pressureHpa>1100.f)return false;
  lastPressureHpa=pressureHpa;lastGoodAt=now;return true;
}

inline bool sampleFresh(uint32_t sampleAt,uint32_t now,uint32_t maxAge){
  return sampleAt!=0&&uint32_t(now-sampleAt)<maxAge;
}


class EnvironmentTrend {
 public:
  void setMetricsEnabled(bool enabled){metricsEnabled_=enabled;if(!enabled)cooldownSuppressed_=0;}
  uint32_t cooldownSuppressed()const{return cooldownSuppressed_;}
  bool observe(float temperatureC,float humidityPct,float pressureHpa,bool ahtValid,bool pressureValid,uint32_t sampleAt,uint32_t now){
    if(!ahtValid||!sampleAt||sampleAt==lastSampleAt_||uint32_t(now-sampleAt)>=30000u||!isfinite(temperatureC)||!isfinite(humidityPct))return false;
    if(pressureValid&&!isfinite(pressureHpa))pressureValid=false;
    lastSampleAt_=sampleAt;
    if(samples_<5){addBaseline(temperatureC,humidityPct,pressureHpa,pressureValid);return false;}
    const bool tempChange=fabsf(temperatureC-temperatureMean_)>=2.0f;
    const bool humidityChange=fabsf(humidityPct-humidityMean_)>=10.0f;
    const bool pressureChange=pressureValid&&pressureSamples_>=5&&fabsf(pressureHpa-pressureMean_)>=5.0f;
    const bool changed=tempChange||humidityChange||pressureChange;
    if(changed){if(candidateSamples_<2)++candidateSamples_;}else candidateSamples_=0;
    if(changed&&candidateSamples_>=2){
      if(!lastReactionAt_||uint32_t(now-lastReactionAt_)>=900000u){lastReactionAt_=now;candidateSamples_=0;cooldownBlocked_=false;temperatureMean_=temperatureC;humidityMean_=humidityPct;if(pressureValid)pressureMean_=pressureHpa;return true;}
      if(!cooldownBlocked_&&metricsEnabled_)++cooldownSuppressed_;
      cooldownBlocked_=true;
    }else cooldownBlocked_=false;
    temperatureMean_+=(temperatureC-temperatureMean_)*.08f;
    humidityMean_+=(humidityPct-humidityMean_)*.08f;
    if(pressureValid){pressureMean_+=(pressureHpa-pressureMean_)*.08f;if(pressureSamples_<5)++pressureSamples_;}
    return false;
  }
 private:
  uint32_t lastSampleAt_=0,lastReactionAt_=0,cooldownSuppressed_=0;
  uint8_t samples_=0,pressureSamples_=0,candidateSamples_=0;
  bool metricsEnabled_=false,cooldownBlocked_=false;
  float temperatureMean_=0,humidityMean_=0,pressureMean_=0;
  void addBaseline(float temperatureC,float humidityPct,float pressureHpa,bool pressureValid){
    ++samples_;const float n=float(samples_);temperatureMean_+=(temperatureC-temperatureMean_)/n;humidityMean_+=(humidityPct-humidityMean_)/n;
    if(pressureValid){++pressureSamples_;pressureMean_+=(pressureHpa-pressureMean_)/float(pressureSamples_);}
  }
};
} // namespace companion
