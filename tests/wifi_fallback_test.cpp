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
  assert(store.clear());
  RuntimeSettings freshDefaults;
  RuntimeSettings::copy(freshDefaults.wifiSsid,sizeof(freshDefaults.wifiSsid),"compiled-default");
  Preferences::failNextBytesForTest("wifiset");
  assert(!store.save(freshDefaults));
  RuntimeSettings freshLoaded;
  assert(!store.load(freshLoaded,freshDefaults));
  assert(std::strcmp(freshLoaded.wifiSsid,"compiled-default")==0);
  Preferences freshNvs;
  assert(freshNvs.begin("robodesk",true));
  assert(!freshNvs.isKey("init"));
  freshNvs.end();
  Preferences::failNextStringForTest("model");
  assert(!store.save(settings));
  assert(store.wifiCredentialsCommittedThisSave());
  assert(store.load(freshLoaded,freshDefaults));
  assert(std::strcmp(freshLoaded.wifiSsid2,"backup-two")==0);
  assert(std::strcmp(freshLoaded.wifiPassword3,"third-secret")==0);

  assert(store.save(settings));
  RuntimeSettings loaded;
  assert(store.load(loaded,defaults));
  assert(std::strcmp(loaded.wifiSsid2,"backup-two")==0);
  assert(std::strcmp(loaded.wifiPassword2,"second-secret")==0);
  assert(std::strcmp(loaded.wifiSsid3,"backup-three")==0);
  assert(std::strcmp(loaded.wifiPassword3,"third-secret")==0);
  assert(loaded.findWifiNetworkIndex(0)==1);
  assert(loaded.findWifiNetworkIndex(2)==2);

  RuntimeSettings failedWrite=settings;
  RuntimeSettings::copy(failedWrite.wifiSsid2,sizeof(failedWrite.wifiSsid2),"replacement-two");
  RuntimeSettings::copy(failedWrite.wifiPassword2,sizeof(failedWrite.wifiPassword2),"replacement-secret");
  Preferences::failNextBytesForTest("wifiset");
  assert(!store.save(failedWrite));
  assert(store.wifiSaveFailedSlot()==2);
  assert(store.wifiFreeEntriesAfterSave()==64);
  assert(store.load(loaded,defaults));
  assert(std::strcmp(loaded.wifiSsid2,"backup-two")==0);
  Preferences::failNextStringForTest("wpass2");
  assert(store.save(failedWrite));
  assert(store.wifiSaveFailedSlot()==0);
  assert(store.wifiLegacyMirrorFailedSlot()==2);
  assert(!store.wifiLegacyMirrorRestoreFailed());
  assert(store.load(loaded,defaults));
  assert(std::strcmp(loaded.wifiSsid2,"replacement-two")==0);
  assert(std::strcmp(loaded.wifiPassword2,"replacement-secret")==0);
  Preferences verifyLegacy;
  assert(verifyLegacy.begin("robodesk",true));
  assert(std::strcmp(verifyLegacy.getString("ssid2").c_str(),"backup-two")==0);
  assert(std::strcmp(verifyLegacy.getString("wpass2").c_str(),"second-secret")==0);
  verifyLegacy.end();
  assert(store.save(settings));
  assert(store.wifiSaveFailedSlot()==0);

  assert(store.clear());
  Preferences legacy;
  assert(legacy.begin("robodesk",false));
  legacy.putBool("init",true);
  legacy.putString("ssid","old-primary");
  legacy.putString("wpass","old-secret");
  legacy.end();
  assert(store.load(loaded,defaults));
  assert(std::strcmp(loaded.wifiSsid,"old-primary")==0);
  assert(store.save(loaded));
  assert(legacy.begin("robodesk",true));
  assert(legacy.isKey("wifiset"));
  legacy.end();
  assert(loaded.wifiSsid2[0]==0&&loaded.wifiSsid3[0]==0);
  assert(!loaded.wifiNetworkConfigured(1)&&!loaded.wifiNetworkConfigured(2));
  assert(loaded.findWifiNetworkIndex(0)==0);
  RuntimeSettings::copy(loaded.wifiSsid2,sizeof(loaded.wifiSsid2),"fallback");
  assert(loaded.findWifiNetworkIndex(0)==0&&loaded.findWifiNetworkIndex(1)==1&&loaded.findWifiNetworkIndex(2)==0);
  std::cout << "Wi-Fi fallback settings tests passed\n";
}
