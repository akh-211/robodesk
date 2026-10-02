#include "RuntimeSettings.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
  RuntimeSettings defaults;
  RuntimeSettings settings;
  assert(!settings.wifiConfigured());

  RuntimeSettings::copy(settings.wifiSsid2,sizeof(settings.wifiSsid2),"backup-two");
  RuntimeSettings::copy(settings.wifiPassword2,sizeof(settings.wifiPassword2),"second-secret");
  RuntimeSettings::copy(settings.wifiSsid3,sizeof(settings.wifiSsid3),"backup-three");
  RuntimeSettings::copy(settings.wifiPassword3,sizeof(settings.wifiPassword3),"third-secret");
  assert(settings.wifiConfigured());
  assert(!settings.wifiNetworkConfigured(0));
  assert(settings.wifiNetworkConfigured(1));
  assert(settings.wifiNetworkConfigured(2));
  assert(settings.findWifiNetworkIndex(0)==1);
  assert(settings.findWifiNetworkIndex(2)==2);
  assert(std::strcmp(settings.wifiSsidAt(1),"backup-two")==0);
  assert(std::strcmp(settings.wifiPasswordAt(2),"third-secret")==0);
  assert(settings.wifiSsidAt(3)==nullptr&&settings.wifiPasswordAt(3)==nullptr);

  RuntimeSettingsStore store;
  assert(store.save(settings));
  RuntimeSettings loaded;
  assert(store.load(loaded,defaults));
  assert(std::strcmp(loaded.wifiSsid2,"backup-two")==0);
  assert(std::strcmp(loaded.wifiPassword2,"second-secret")==0);
  assert(std::strcmp(loaded.wifiSsid3,"backup-three")==0);
  assert(std::strcmp(loaded.wifiPassword3,"third-secret")==0);
  assert(loaded.findWifiNetworkIndex(0)==1);
  assert(loaded.findWifiNetworkIndex(2)==2);

  assert(store.clear());
  Preferences legacy;
  assert(legacy.begin("robodesk",false));
  legacy.putBool("init",true);
  legacy.putString("ssid","old-primary");
  legacy.putString("wpass","old-secret");
  legacy.end();
  assert(store.load(loaded,defaults));
  assert(std::strcmp(loaded.wifiSsid,"old-primary")==0);
  assert(loaded.wifiSsid2[0]==0&&loaded.wifiSsid3[0]==0);
  assert(!loaded.wifiNetworkConfigured(1)&&!loaded.wifiNetworkConfigured(2));
  assert(loaded.findWifiNetworkIndex(0)==0);
  RuntimeSettings::copy(loaded.wifiSsid2,sizeof(loaded.wifiSsid2),"fallback");
  assert(loaded.findWifiNetworkIndex(0)==0&&loaded.findWifiNetworkIndex(1)==1&&loaded.findWifiNetworkIndex(2)==0);
  std::cout << "Wi-Fi fallback settings tests passed\n";
}
