#pragma once
#include "PhoneAncsProtocol.h"
#include "PhoneNotificationBridge.h"
#include "PhoneAncsEventSupport.h"
#include <atomic>
#include <stdlib.h>
#include <type_traits>
#if defined(CONFIG_NIMBLE_ENABLED)
#if defined(ROBODESK_NIMBLE_EXTERNAL)
#include <nimble/nimble/host/include/host/ble_gap.h>
#include <nimble/nimble/host/include/host/ble_gatt.h>
#include <nimble/nimble/host/include/host/ble_uuid.h>
#else
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>
#endif
#include <freertos/queue.h>
#include <freertos/task.h>

// GATT client on the existing peripheral connection. BLEClient::connect cannot
// attach to that connection in Arduino ESP32 3.3.11, so use async NimBLE GATT.
class PhoneAncsClient {
 public:
  bool begin(){if(listenerRegistered_)return true;events_=xQueueCreate(8,sizeof(Event));if(!events_)return false;eventGate_.open();listenerRegistered_=ble_gap_event_listener_register(&listener_,gap,this)==0;if(!listenerRegistered_)end();return listenerRegistered_;}
  void end(){pauseEventAdmission();if(listenerRegistered_){ble_gap_event_listener_unregister(&listener_);listenerRegistered_=false;}discardQueuedEvents();if(events_){vQueueDelete(events_);events_=nullptr;}reset();conn_=0xffff;}
  void hostStopped(bool stopped){if(stopped)operationTokens_.hostStopped();}
  bool ready()const{return ready_;}
  const char* status()const{return ready_?"ready":conn_==0xffff?"disconnected":failed_?"unavailable":"discovering";}
  void service(int trustedConnection,bool enabled,PhoneNotificationBridge*bridge,const char*allowlist,uint32_t now){
    ble_gap_conn_desc desc{};
    const bool secure=trustedConnection>=0&&ble_gap_conn_find(uint16_t(trustedConnection),&desc)==0&&desc.sec_state.encrypted&&desc.sec_state.bonded&&desc.sec_state.authenticated&&desc.sec_state.key_size>=16;
    uint16_t conn=enabled&&secure?uint16_t(trustedConnection):0xffff;
    if(conn!=conn_){if(conn_!=0xffff&&bridge)bridge->clear();reset();conn_=conn;if(conn_!=0xffff)session_.begin(conn_);retryAt_=now;}
    if(conn_==0xffff)return;
    if(overflow_.exchange(false)){fail(now);}
    Event e;while(events_&&xQueueReceive(events_,&e,0)==pdTRUE){EventDataRelease release{e};
      if(e.conn!=conn_||!session_.accepts(e.generation,e.conn))continue;
      if(e.kind==Notify){
        if(e.a==changedHandle_){if(bridge)bridge->clear();reset();if(conn_!=0xffff)session_.begin(conn_);retryAt_=now;continue;}
        if(!ready_)continue;
        if(e.a==sourceHandle_&&e.size==8){uint32_t uid=uint32_t(e.payload.data[4])|(uint32_t(e.payload.data[5])<<8)|(uint32_t(e.payload.data[6])<<16)|(uint32_t(e.payload.data[7])<<24);char key[33];snprintf(key,sizeof(key),"ios-%08lx",(unsigned long)uid);
          if(e.payload.data[0]==2){if(bridge)bridge->remove(key);for(auto&p:pending_)if(p.used&&p.uid==uid)p.used=false;if(reading_&&uid==uid_){reading_=false;parser_.begin(PhoneAncsProtocol::Kind::Identifier,0);}continue;}
          if(e.payload.data[0]>1)continue;bool found=false;for(auto&p:pending_)if(p.used&&p.uid==uid)found=true;if(!found){for(auto&p:pending_)if(!p.used){p.uid=uid;p.used=true;found=true;break;}}
        }else if(e.a==dataHandle_&&reading_){if(!parser_.feed(e.payload.data,e.size)){reading_=false;quietUntil_=now+1500;}else if(parser_.complete())finishResponse(bridge,allowlist,now);}
        continue;
      }
      if(e.kind==Service&&phase_==Services){if(e.status){fail(now);continue;}if(e.a){if(e.tag==1){ancsStart_=e.a;ancsEnd_=e.b;}else if(e.tag==2){gattStart_=e.a;gattEnd_=e.b;}}else{
        if(!ancsStart_){fail(now);continue;}phase_=AncsChars;charCount_=0;startChars(ancsStart_,ancsEnd_,now);}}
      else if(e.kind==Characteristic&&(phase_==AncsChars||phase_==GattChars)){if(e.status){fail(now);continue;}if(e.a){if(charCount_>=12){fail(now);continue;}chars_[charCount_++]={e.a,e.b,e.tag};}else{
        for(unsigned i=0;i<charCount_;++i){auto&c=chars_[i];uint16_t end=phase_==AncsChars?ancsEnd_:gattEnd_;for(unsigned j=0;j<charCount_;++j)if(chars_[j].def>c.value&&chars_[j].def<=end)end=chars_[j].def-1;
          if(c.tag==1){sourceHandle_=c.value;sourceEnd_=end;}if(c.tag==2){dataHandle_=c.value;dataEnd_=end;}if(c.tag==3)controlHandle_=c.value;if(c.tag==4){changedHandle_=c.value;changedEnd_=end;}}
        if(phase_==AncsChars&&gattStart_){phase_=GattChars;charCount_=0;startChars(gattStart_,gattEnd_,now);}else{if(!sourceHandle_||!dataHandle_||!controlHandle_){fail(now);continue;}phase_=DataDesc;startDesc(dataHandle_,dataEnd_,now);}}}
      else if(e.kind==Descriptor&&(phase_==DataDesc||phase_==SourceDesc||phase_==ChangedDesc)){if(e.status){fail(now);continue;}if(e.a){if(e.tag==1)cccd_=e.a;}else{
        if(!cccd_){fail(now);continue;}phase_=phase_==DataDesc?DataSub:phase_==SourceDesc?SourceSub:ChangedSub;uint8_t value[2]={uint8_t(phase_==ChangedSub?2:1),0};auto*operation=acquireOperation();if(!operation){fail(now);continue;}if(ble_gattc_write_flat(conn_,cccd_,value,2,written,operation)){operationTokens_.release(operation);fail(now);}else deadline_=now+7000;}}
      else if(e.kind==Written){if(e.status){if(phase_==Ready){reading_=false;quietUntil_=now+1500;}else fail(now);continue;}
        if(phase_==DataSub){phase_=SourceDesc;startDesc(sourceHandle_,sourceEnd_,now);}else if(phase_==SourceSub&&changedHandle_){phase_=ChangedDesc;startDesc(changedHandle_,changedEnd_,now);}else if(phase_==SourceSub||phase_==ChangedSub){phase_=Ready;ready_=true;}}
    }
    if(phase_!=Ready&&phase_!=Idle&&int32_t(now-deadline_)>=0)fail(now);
    if(phase_==Idle&&int32_t(now-retryAt_)>=0){resetDiscovery();phase_=Services;deadline_=now+7000;auto*operation=acquireOperation();if(!operation||ble_gattc_disc_all_svcs(conn_,serviceFound,operation)){if(operation)operationTokens_.release(operation);fail(now);}}
    if(reading_&&uint32_t(now-requestAt_)>=7000){reading_=false;parser_=PhoneAncsProtocol{};quietUntil_=now+1500;}
    if(ready_&&!reading_&&int32_t(now-quietUntil_)>=0){for(auto&p:pending_)if(p.used){uid_=p.uid;p.used=false;request(PhoneAncsProtocol::Kind::Identifier,now);break;}}
  }
 private:
  enum :uint8_t {Notify,Service,Characteristic,Descriptor,Written};
  enum Phase:uint8_t {Idle,Services,AncsChars,GattChars,DataDesc,DataSub,SourceDesc,SourceSub,ChangedDesc,ChangedSub,Ready};
  static constexpr uint16_t MaxNotifyPayload=384;
  struct Event{uint16_t conn=0,a=0,b=0,size=0;uint32_t generation=0;uint8_t kind=0,tag=0;int status=0;PhoneAncsEventPayload payload;};
  static_assert(std::is_trivially_copyable<Event>::value,"ANCS event queue copies its records by value");
  using OperationToken=PhoneAncsOperationTokenPool::Token;
  struct EventDataRelease{Event& event;~EventDataRelease(){event.payload.release();}};
  struct Chr{uint16_t def=0,value=0;uint8_t tag=0;};struct Pending{uint32_t uid=0;bool used=false;};
  QueueHandle_t events_=nullptr;bool listenerRegistered_=false;ble_gap_event_listener listener_{};std::atomic<bool>overflow_{false};PhoneAncsEventGate eventGate_;PhoneAncsSessionGuard session_;PhoneAncsOperationTokenPool operationTokens_;
  uint16_t conn_=0xffff,ancsStart_=0,ancsEnd_=0,gattStart_=0,gattEnd_=0,sourceHandle_=0,sourceEnd_=0,dataHandle_=0,dataEnd_=0,controlHandle_=0,changedHandle_=0,changedEnd_=0,cccd_=0;
  Chr chars_[12]{};unsigned charCount_=0;Pending pending_[6]{};Phase phase_=Idle;bool ready_=false,failed_=false,reading_=false;uint32_t retryAt_=0,deadline_=0,uid_=0,requestAt_=0,quietUntil_=0;
  PhoneAncsProtocol parser_;PhoneAncsProtocol::Kind requestKind_=PhoneAncsProtocol::Kind::Identifier;
  void pauseEventAdmission(){eventGate_.closeAndWait([](){vTaskDelay(1);});}
  void discardQueuedEvents(){Event e;while(events_&&xQueueReceive(events_,&e,0)==pdTRUE)e.payload.release();}
  void enqueue(Event e){if(!eventGate_.enter()){e.payload.release();return;}if(!session_.accepts(e.generation,e.conn)){eventGate_.leave();e.payload.release();return;}bool queued=events_&&xQueueSend(events_,&e,0)==pdTRUE;if(!queued){e.payload.release();overflow_=true;}eventGate_.leave();}
  void markOverflow(uint32_t generation,uint16_t connection){if(!eventGate_.enter())return;if(session_.accepts(generation,connection))overflow_=true;eventGate_.leave();}
  OperationToken*acquireOperation(){uint32_t generation=0;uint16_t connection=PhoneAncsSessionGuard::InvalidConnection;if(!session_.snapshot(generation,connection)||connection!=conn_)return nullptr;return operationTokens_.acquire(this,generation,connection);}
  bool operationCurrent(const OperationToken*operation,uint16_t connection)const{return operationTokens_.active(operation)&&operation->owner==this&&operation->connection==connection&&session_.accepts(operation->generation,connection);}
  static int normalized(int s){return s==BLE_HS_EDONE?0:s;}
  static uint8_t uuidTag(const ble_uuid_t*u){char s[BLE_UUID_STR_LEN];ble_uuid_to_str(u,s);if(!strcmp(s,"7905f431-b5ce-4e99-a40f-4b1e122d00d0"))return 1;if(ble_uuid_u16(u)==0x1801)return 2;return 0;}
  static int gap(ble_gap_event*event,void*arg){auto*self=(PhoneAncsClient*)arg;if(event->type!=BLE_GAP_EVENT_NOTIFY_RX)return 0;Event e;if(!self->session_.snapshot(e.generation,e.conn)||e.conn!=event->notify_rx.conn_handle)return 0;e.kind=Notify;e.a=event->notify_rx.attr_handle;e.size=OS_MBUF_PKTLEN(event->notify_rx.om);if(e.size>MaxNotifyPayload){self->markOverflow(e.generation,e.conn);return 0;}if(!e.payload.allocate(e.size)){self->markOverflow(e.generation,e.conn);return 0;}if(os_mbuf_copydata(event->notify_rx.om,0,e.size,e.payload.data)==0)self->enqueue(e);else{e.payload.release();self->markOverflow(e.generation,e.conn);}return 0;}
  static int serviceFound(uint16_t conn,const ble_gatt_error*err,const ble_gatt_svc*s,void*arg){auto*operation=(OperationToken*)arg;auto*self=operation?static_cast<PhoneAncsClient*>(operation->owner):nullptr;const bool terminal=err->status!=0;if(self&&self->operationCurrent(operation,conn)){Event e;e.kind=Service;e.conn=conn;e.generation=operation->generation;e.status=normalized(err->status);if(s){e.a=s->start_handle;e.b=s->end_handle;e.tag=uuidTag(&s->uuid.u);if(e.tag)self->enqueue(e);}else self->enqueue(e);}if(terminal&&self)self->operationTokens_.release(operation);return 0;}
  static int chrFound(uint16_t conn,const ble_gatt_error*err,const ble_gatt_chr*c,void*arg){auto*operation=(OperationToken*)arg;auto*self=operation?static_cast<PhoneAncsClient*>(operation->owner):nullptr;const bool terminal=err->status!=0;if(self&&self->operationCurrent(operation,conn)){Event e;e.kind=Characteristic;e.conn=conn;e.generation=operation->generation;e.status=normalized(err->status);if(c){e.a=c->def_handle;e.b=c->val_handle;char s[BLE_UUID_STR_LEN];ble_uuid_to_str(&c->uuid.u,s);if(!strcmp(s,"9fbf120d-6301-42d9-8c58-25e699a21dbd"))e.tag=1;else if(!strcmp(s,"22eac6e9-24d6-4bb5-be44-b36ace7c7bfb"))e.tag=2;else if(!strcmp(s,"69d1d8f3-45e1-49a8-9821-9bbdfdaad9d9"))e.tag=3;else if(ble_uuid_u16(&c->uuid.u)==0x2a05)e.tag=4;}self->enqueue(e);}if(terminal&&self)self->operationTokens_.release(operation);return 0;}
  static int descFound(uint16_t conn,const ble_gatt_error*err,uint16_t,const ble_gatt_dsc*d,void*arg){auto*operation=(OperationToken*)arg;auto*self=operation?static_cast<PhoneAncsClient*>(operation->owner):nullptr;const bool terminal=err->status!=0;if(self&&self->operationCurrent(operation,conn)){Event e;e.kind=Descriptor;e.conn=conn;e.generation=operation->generation;e.status=normalized(err->status);if(d){e.a=d->handle;e.tag=ble_uuid_u16(&d->uuid.u)==0x2902?1:0;}self->enqueue(e);}if(terminal&&self)self->operationTokens_.release(operation);return 0;}
  static int written(uint16_t conn,const ble_gatt_error*err,ble_gatt_attr*,void*arg){auto*operation=(OperationToken*)arg;auto*self=operation?static_cast<PhoneAncsClient*>(operation->owner):nullptr;if(self&&self->operationCurrent(operation,conn)){Event e;e.kind=Written;e.conn=conn;e.generation=operation->generation;e.status=err->status;self->enqueue(e);}if(self)self->operationTokens_.release(operation);return 0;}
  void startChars(uint16_t start,uint16_t end,uint32_t now){deadline_=now+7000;auto*operation=acquireOperation();if(!operation||ble_gattc_disc_all_chrs(conn_,start,end,chrFound,operation)){if(operation)operationTokens_.release(operation);fail(now);}}
  void startDesc(uint16_t start,uint16_t end,uint32_t now){cccd_=0;deadline_=now+7000;if(start>=end){fail(now);return;}auto*operation=acquireOperation();if(!operation||ble_gattc_disc_all_dscs(conn_,start,end,descFound,operation)){if(operation)operationTokens_.release(operation);fail(now);}}
  void resetDiscovery(){phase_=Idle;ready_=reading_=false;ancsStart_=ancsEnd_=gattStart_=gattEnd_=sourceHandle_=dataHandle_=controlHandle_=changedHandle_=0;charCount_=0;}
  void reset(){const bool resumeAdmission=events_&&listenerRegistered_;pauseEventAdmission();session_.invalidate();overflow_=false;parser_=PhoneAncsProtocol{};resetDiscovery();failed_=false;for(auto&p:pending_)p.used=false;discardQueuedEvents();if(resumeAdmission)eventGate_.open();}
  void fail(uint32_t now){const bool resumeAdmission=events_&&listenerRegistered_;pauseEventAdmission();session_.invalidate();overflow_=false;parser_=PhoneAncsProtocol{};resetDiscovery();failed_=true;retryAt_=now+10000;for(auto&p:pending_)p.used=false;discardQueuedEvents();if(conn_!=0xffff)session_.begin(conn_);if(resumeAdmission)eventGate_.open();}
  void request(PhoneAncsProtocol::Kind kind,uint32_t now){requestKind_=kind;parser_.begin(kind,uid_,parser_.appId);uint8_t cmd[72]{};size_t n=0;
    if(kind==PhoneAncsProtocol::Kind::AppName){cmd[n++]=1;size_t k=strlen(parser_.appId);memcpy(cmd+n,parser_.appId,k);n+=k;cmd[n++]=0;cmd[n++]=0;}
    else{cmd[n++]=0;for(unsigned i=0;i<4;++i)cmd[n++]=uint8_t(uid_>>(8*i));if(kind==PhoneAncsProtocol::Kind::Identifier)cmd[n++]=0;else{cmd[n++]=1;cmd[n++]=79;cmd[n++]=0;cmd[n++]=3;cmd[n++]=180;cmd[n++]=0;}}
    auto*operation=acquireOperation();if(!operation){reading_=false;quietUntil_=now+1500;return;}reading_=true;requestAt_=now;if(ble_gattc_write_flat(conn_,controlHandle_,cmd,n,written,operation)){operationTokens_.release(operation);reading_=false;quietUntil_=now+1500;}}
  void finishResponse(PhoneNotificationBridge*bridge,const char*allowlist,uint32_t now){reading_=false;
    if(requestKind_==PhoneAncsProtocol::Kind::Identifier){if(PhoneNotificationBridge::allowed_(parser_.appId,allowlist))request(PhoneAncsProtocol::Kind::Content,now);return;}
    if(requestKind_==PhoneAncsProtocol::Kind::Content){request(PhoneAncsProtocol::Kind::AppName,now);return;}
    char key[33];snprintf(key,sizeof(key),"ios-%08lx",(unsigned long)uid_);if(bridge)bridge->accept(parser_.appId,parser_.appName[0]?parser_.appName:parser_.appId,parser_.title,parser_.body,allowlist,now,key);parser_=PhoneAncsProtocol{};
  }
};
#else
class PhoneAncsClient {public:void end(){}void hostStopped(bool){}bool begin(){return false;}bool ready()const{return false;}const char*status()const{return "unsupported";}void service(int,bool,PhoneNotificationBridge*,const char*,uint32_t){}};
#endif
