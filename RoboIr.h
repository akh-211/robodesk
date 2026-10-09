#pragma once
#include <esp32-hal-rmt.h>
#include <Preferences.h>
#include "RoboBoardProfile.h"
// Local 38 kHz IR. RMT performs capture/transmit without blocking audio tasks.
class RoboIr {
  static constexpr uint32_t ProfileMagic=0x52495231u;static constexpr unsigned ProfileCapacity=4;
  struct Profile {uint32_t magic=0;char name[16]{};uint8_t count=0;rmt_data_t data[64]{};};
  rmt_data_t rx_[64]{},tx_[64]{};Profile profiles_[ProfileCapacity]{};size_t rxCount_=64;
  uint32_t learnUntil_=0;uint8_t learnProfile_=0,lastProfile_=0;char learnName_[16]{};bool ready_=false,reading_=false,transmitting_=false;
  bool arm(){rxCount_=64;reading_=rmtReadAsync(ROBODESK_PIN_IR_RX,rx_,&rxCount_);return reading_;}
  static bool validName(const char*name){if(!name||!*name||strlen(name)>15)return false;for(const char*p=name;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'||*p==' '))return false;return true;}
  int find(const char*name)const{if(!name||!*name){if(lastProfile_<ProfileCapacity&&profiles_[lastProfile_].magic==ProfileMagic&&profiles_[lastProfile_].count>2)return lastProfile_;for(unsigned i=0;i<ProfileCapacity;++i)if(profiles_[i].magic==ProfileMagic&&profiles_[i].count>2)return int(i);return -1;}for(unsigned i=0;i<ProfileCapacity;++i)if(profiles_[i].magic==ProfileMagic&&profiles_[i].count>2&&!strcmp(profiles_[i].name,name))return int(i);return -1;}
  bool saveProfile(unsigned i,const Profile&profile){if(i>=ProfileCapacity)return false;Preferences p;if(!p.begin("robodesk_ir",false))return false;const bool ok=p.putBytes(i==0?"p0":i==1?"p1":i==2?"p2":"p3",&profile,sizeof(Profile))==sizeof(Profile)&&p.putUChar("last",uint8_t(i))==1;p.end();return ok;}
public:
  bool begin(){
    ready_=rmtInit(ROBODESK_PIN_IR_RX,RMT_RX_MODE,RMT_MEM_NUM_BLOCKS_2,1000000)&&rmtInit(ROBODESK_PIN_IR_TX,RMT_TX_MODE,RMT_MEM_NUM_BLOCKS_2,1000000)&&rmtSetRxMinThreshold(ROBODESK_PIN_IR_RX,50)&&rmtSetRxMaxThreshold(ROBODESK_PIN_IR_RX,12000)&&rmtSetCarrier(ROBODESK_PIN_IR_TX,true,true,38000,0.33f);
    if(!ready_)return false;Preferences p;if(p.begin("robodesk_ir",false)){const char*keys[]={"p0","p1","p2","p3"};for(unsigned i=0;i<ProfileCapacity;++i)if(p.getBytesLength(keys[i])==sizeof(Profile)){p.getBytes(keys[i],&profiles_[i],sizeof(Profile));if(profiles_[i].magic!=ProfileMagic||profiles_[i].count<=2||profiles_[i].count>=64||!validName(profiles_[i].name))profiles_[i]=Profile();}lastProfile_=p.getUChar("last",0);if(lastProfile_>=ProfileCapacity||profiles_[lastProfile_].magic!=ProfileMagic)lastProfile_=0;
      if(find(nullptr)<0){size_t n=p.getBytesLength("learned");if(n>2*sizeof(rmt_data_t)&&n<64*sizeof(rmt_data_t)&&n%sizeof(rmt_data_t)==0){Profile&legacy=profiles_[0];legacy.magic=ProfileMagic;strcpy(legacy.name,"last");legacy.count=uint8_t(n/sizeof(rmt_data_t));p.getBytes("learned",legacy.data,n);lastProfile_=0;const char*key="p0";p.putBytes(key,&legacy,sizeof(Profile));p.putUChar("last",0);}}p.end();}ready_=arm();return ready_;
  }
  bool learn(uint32_t now,const char*name="last"){if(!ready_||transmitting_)return false;if(!name||!*name)name="last";if(!validName(name))return false;int slot=find(name);if(slot<0){for(unsigned i=0;i<ProfileCapacity;++i)if(profiles_[i].magic!=ProfileMagic||profiles_[i].count<=2){slot=int(i);break;}}if(slot<0)return false;learnProfile_=uint8_t(slot);strncpy(learnName_,name,sizeof(learnName_)-1);learnName_[sizeof(learnName_)-1]=0;learnUntil_=now+10000;return true;}
  bool sendNec(const char*hex){if(!ready_||transmitting_||!hex||!*hex||strlen(hex)>8)return false;char*end=nullptr;uint32_t code=uint32_t(strtoul(hex,&end,16));if(!end||*end)return false;
    tx_[0].duration0=9000;tx_[0].level0=1;tx_[0].duration1=4500;tx_[0].level1=0;
    for(unsigned i=0;i<32;++i){tx_[i+1].duration0=560;tx_[i+1].level0=1;tx_[i+1].duration1=(code&(1u<<i))?1690:560;tx_[i+1].level1=0;}
    tx_[33].duration0=560;tx_[33].level0=1;tx_[33].duration1=12000;tx_[33].level1=0;transmitting_=rmtWriteAsync(ROBODESK_PIN_IR_TX,tx_,34);return transmitting_;
  }
  bool replay(const char*name=nullptr){if(!ready_||transmitting_)return false;const int i=find(name);if(i<0)return false;const Profile&profile=profiles_[i];memcpy(tx_,profile.data,size_t(profile.count)*sizeof(rmt_data_t));transmitting_=rmtWriteAsync(ROBODESK_PIN_IR_TX,tx_,profile.count);return transmitting_;}
  void service(uint32_t now){if(!ready_)return;if(transmitting_&&rmtTransmitCompleted(ROBODESK_PIN_IR_TX))transmitting_=false;
    if(reading_&&rmtReceiveCompleted(ROBODESK_PIN_IR_RX)){reading_=false;if(learnUntil_&&int32_t(learnUntil_-now)>0&&!transmitting_&&rxCount_>2&&rxCount_<64){uint32_t total=0;for(size_t i=0;i<rxCount_;++i)total+=rx_[i].duration0+rx_[i].duration1;
      if(total>=1000&&total<=150000){Profile updated{};updated.magic=ProfileMagic;strncpy(updated.name,learnName_,sizeof(updated.name)-1);updated.count=uint8_t(rxCount_);for(size_t i=0;i<rxCount_;++i){updated.data[i]=rx_[i];updated.data[i].level0=!rx_[i].level0;updated.data[i].level1=!rx_[i].level1;}if(saveProfile(learnProfile_,updated)){profiles_[learnProfile_]=updated;lastProfile_=learnProfile_;}learnUntil_=0;}}
      if(!arm())ready_=false;}
    if(learnUntil_&&int32_t(now-learnUntil_)>=0)learnUntil_=0;
  }
  bool ready()const{return ready_;}bool learned()const{return find(nullptr)>=0;}bool learning()const{return learnUntil_!=0;}unsigned profileCount()const{unsigned n=0;for(const auto&p:profiles_)n+=p.magic==ProfileMagic;return n;}const char*profileName(unsigned i)const{return i<ProfileCapacity&&profiles_[i].magic==ProfileMagic?profiles_[i].name:"";}
};
