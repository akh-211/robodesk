#pragma once
#include <IPAddress.h>
#include "RoboDualRuntime.h"
#define WL_CONNECTED 3
#define WIFI_STA 1
#define WIFI_AP 2
#define WIFI_AP_STA 3
#define WIFI_POWER_8_5dBm 34
struct RoboOfflineWiFi {
  int status()const{return RoboDual.link.connected()&&RoboDual.internet?WL_CONNECTED:0;}
  int RSSI()const{return 0;}IPAddress localIP()const{return IPAddress();}IPAddress softAPIP()const{return IPAddress();}
  String SSID()const{return String("C3 gateway");}
  void disconnect(bool,bool){}void begin(const char*,const char*){}void mode(int){}void setSleep(bool){}void setTxPower(int){}void setAutoReconnect(bool){}
  bool softAP(const char*,const char*){return false;}
};
inline RoboOfflineWiFi WiFi;
struct RoboOfflineMdns {bool begin(const char*){return false;}void addService(const char*,const char*,int){}};
inline RoboOfflineMdns MDNS;
