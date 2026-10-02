#pragma once

#include "PhoneNotificationBridge.h"
#include "RuntimeSettings.h"

#if __has_include(<BLEDevice.h>)
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <strings.h>
#define ROBODESK_PHONE_BLE_AVAILABLE 1

class PhoneBleTransport {
 public:
  static constexpr const char* ServiceUuid="93de0001-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* WriteUuid="93de0002-2c7d-4a52-9f1c-6f4b4f424c45";
  static constexpr const char* AllowlistUuid="93de0003-2c7d-4a52-9f1c-6f4b4f424c45";
  bool begin(PhoneNotificationBridge* bridge,RuntimeSettings* settings){
    bridge_=bridge;settings_=settings;
    BLEDevice::init("RoboDesk");BLEDevice::setMTU(512);
    BLEDevice::setSecurityCallbacks(new BLESecurityCallbacks());
    BLESecurity::setAuthenticationMode(true,false,true);
    BLESecurity::setCapability(ESP_IO_CAP_NONE);
    server_=BLEDevice::createServer();if(!server_)return false;
    server_->setCallbacks(new ServerCallbacks(this));
    BLEService* service=server_->createService(ServiceUuid);if(!service)return false;
    BLECharacteristic* rx=service->createCharacteristic(WriteUuid,BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR|BLECharacteristic::PROPERTY_WRITE_ENC);
    if(!rx)return false;
    rx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
    rx->setCallbacks(new RxCallbacks(this));
    BLECharacteristic* allowlist=service->createCharacteristic(AllowlistUuid,BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_READ_ENC);
    if(!allowlist)return false;
    allowlist->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
    allowlist->setValue(settings_->notificationAllowlist);
    allowlist->setCallbacks(new AllowlistCallbacks(this));
    service->start();
    advertising_=BLEDevice::getAdvertising();advertising_->addServiceUUID(ServiceUuid);
    advertising_->setScanFilter(false,false);
    advertising_->start();
    return true;
  }
  bool connected()const{return server_&&server_->getConnectedCount()>0;}
  void openPairingWindow(uint32_t now){candidatePeer_[0]=0;pairingUntil_=now+60000u;if(advertising_)advertising_->setScanFilter(false,false);}
  bool pairingOpen(uint32_t now)const{return int32_t(pairingUntil_-now)>0;}
  bool hasCandidate()const{return candidatePeer_[0]!=0;}
  const char* candidatePeer()const{return candidatePeer_;}
  void approveCandidate(){if(!settings_||!candidatePeer_[0])return;RuntimeSettings::copy(settings_->phoneBlePeer,sizeof(settings_->phoneBlePeer),candidatePeer_);candidatePeer_[0]=0;pairingUntil_=0;if(advertising_)advertising_->setScanFilter(false,false);}
  void clearPeer(){if(settings_)settings_->phoneBlePeer[0]=0;candidatePeer_[0]=0;pairingUntil_=0;if(trustedConnId_>=0)rejectConnId_=trustedConnId_;if(advertising_)advertising_->setScanFilter(false,false);}
  void service(uint32_t now){if(pairingUntil_&&int32_t(now-pairingUntil_)>=0)pairingUntil_=0;int id=rejectConnId_;if(id>=0&&server_){rejectConnId_=-1;server_->disconnect(uint16_t(id));}if(advertisingRestartPending_&&advertising_){advertisingRestartPending_=false;advertising_->start();}}
 private:
  PhoneNotificationBridge* bridge_=nullptr;RuntimeSettings* settings_=nullptr;
  BLEServer* server_=nullptr;BLEAdvertising* advertising_=nullptr;
  volatile uint32_t pairingUntil_=0;volatile int rejectConnId_=-1,trustedConnId_=-1;volatile bool advertisingRestartPending_=false;char candidatePeer_[18]={};
  bool isTrustedConnection_(uint16_t id)const{return settings_&&settings_->phoneBlePeer[0]&&trustedConnId_==int(id);}
  class ServerCallbacks:public BLEServerCallbacks{
   public:explicit ServerCallbacks(PhoneBleTransport* owner):owner_(owner){}
    void onConnect(BLEServer*,esp_ble_gatts_cb_param_t* param)override{
      if(!owner_||!param||!owner_->settings_)return;
      String address=BLEAddress(param->connect.remote_bda).toString();address.toUpperCase();
      const bool trusted=owner_->settings_->phoneBlePeer[0]&&address.equals(owner_->settings_->phoneBlePeer);
      if(trusted){owner_->trustedConnId_=param->connect.conn_id;return;}
      if(owner_->pairingOpen(millis())&&!owner_->candidatePeer_[0])address.toCharArray(owner_->candidatePeer_,sizeof(owner_->candidatePeer_));
      owner_->rejectConnId_=param->connect.conn_id;
    }
    void onDisconnect(BLEServer*,esp_ble_gatts_cb_param_t* param)override{if(owner_&&param){if(owner_->trustedConnId_==param->disconnect.conn_id)owner_->trustedConnId_=-1;owner_->advertisingRestartPending_=true;}}
   private:PhoneBleTransport* owner_;
  };
  class AllowlistCallbacks:public BLECharacteristicCallbacks{
   public:explicit AllowlistCallbacks(PhoneBleTransport* owner):owner_(owner){}
    void onRead(BLECharacteristic* characteristic,esp_ble_gatts_cb_param_t* param)override{if(!owner_||!param||!owner_->isTrustedConnection_(param->read.conn_id)){characteristic->setValue("");return;}if(owner_->settings_)characteristic->setValue(owner_->settings_->notificationAllowlist);}
   private:PhoneBleTransport* owner_;
  };
  class RxCallbacks:public BLECharacteristicCallbacks{
   public:explicit RxCallbacks(PhoneBleTransport* owner):owner_(owner){}
    void onWrite(BLECharacteristic* characteristic,esp_ble_gatts_cb_param_t* param)override{if(!owner_||!param||!owner_->isTrustedConnection_(param->write.conn_id)||!owner_->bridge_||!owner_->settings_)return;String raw=characteristic->getValue();const size_t length=raw.length();if(!length||length>420)return;
      char data[421];memcpy(data,raw.c_str(),length);data[length]=0;char* fields[4]={data,nullptr,nullptr,nullptr};unsigned count=1;
      for(size_t i=0;i<length&&count<4;++i)if(data[i]=='\n'){data[i]=0;fields[count++]=data+i+1;}
      if(count!=4)return;
      owner_->bridge_->accept(fields[0],fields[1],fields[2],fields[3],owner_->settings_->notificationAllowlist,millis());
    }
   private:PhoneBleTransport* owner_;
  };
};

#else
#define ROBODESK_PHONE_BLE_AVAILABLE 0
class PhoneBleTransport {public:bool begin(PhoneNotificationBridge*,RuntimeSettings*){return false;}bool connected()const{return false;}void openPairingWindow(uint32_t){}bool pairingOpen(uint32_t)const{return false;}bool hasCandidate()const{return false;}const char*candidatePeer()const{return "";}void approveCandidate(){}void clearPeer(){}void service(uint32_t){}};
#endif
