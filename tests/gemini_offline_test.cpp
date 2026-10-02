// Deterministic host tests of the actual client, with mocked TLS/FreeRTOS only.
#include "GeminiLiveDirect.h"
#include <cassert>
#include <iostream>
static GeminiLiveDirectClient::Config config(){return {"test-key","test-model","test-voice","id-ID","test-prompt",false,"test-ca","","[]"};}
struct CountingSink : GeminiStreamSink {
  unsigned setups=0;
  void onGeminiSetupComplete()override{++setups;}
  void onGeminiAudio(const uint8_t*,size_t)override{}
  void onGeminiInputTranscript(const char*)override{}
  void onGeminiOutputTranscript(const char*)override{}
  void onGeminiTurnComplete()override{}
  void onGeminiWaitingForInput()override{}
  void onGeminiGenerationComplete()override{}
  void onGeminiInterrupted()override{}
  void onGeminiGoAway()override{}
  void onGeminiSessionHandle(const char*)override{}
  void onGeminiToolCall(const char*,const char*,const char*)override{}
  void onGeminiProtocolError(const char*)override{}
};
static void runWorker(){auto fn=mockTask;mockTask=nullptr;assert(fn);fn(mockTaskArg);}
static void resetMocks(){mockNow=100;mockConnectCalls=mockWriteCalls=0;mockConnectOK=mockTaskOK=true;mockWriteStalled=false;mockWriteLimit=0;mockTx.clear();mockRx.clear();mockTask=nullptr;}
static void open(GeminiLiveDirectClient& c){assert(c.connectAsync(config()));runWorker();assert(c.connected());c.markSetupComplete();mockTx.clear();}
static std::string unmaskFirstText(){
  assert(mockTx.size()>=6);size_t at=2,n=mockTx[1]&127;
  if(n==126){n=(size_t(mockTx[2])<<8)|mockTx[3];at=4;}
  assert(mockTx.size()>=at+4+n);std::string text;
  for(size_t i=0;i<n;++i)text+=char(mockTx[at+4+i]^mockTx[at+(i&3)]);
  return text;
}
int main(){
  resetMocks();{
    GeminiLiveDirectClient c;assert(c.connectAsync(config()));
    assert(c.connecting()&&!c.connected()&&!c.ready()&&!c.reconnectDue(mockNow));
    assert(mockConnectCalls==0);assert(!c.connectAsync(config()));
    uint8_t pcm[640]={};assert(!c.sendAudio(pcm,sizeof(pcm)));assert(!c.sendAudioStreamEnd());
    c.service(mockNow,8192);assert(mockWriteCalls==0);
    c.disconnect(mockNow);runWorker();assert(!c.connecting()&&!c.connected());
  }
  resetMocks();{
    GeminiLiveDirectClient c;mockConnectOK=false;assert(c.connectAsync(config()));runWorker();
    uint32_t completed=mockNow;assert(!c.reconnectDue(completed+2499));assert(c.reconnectDue(completed+2500));
    mockNow=completed+2500;assert(c.connectAsync(config()));runWorker();completed=mockNow;
    assert(!c.reconnectDue(completed+4999));assert(c.reconnectDue(completed+5000));
  }
  resetMocks();{
    GeminiLiveDirectClient c;mockTaskOK=false;assert(!c.connectAsync(config()));assert(!c.connecting());
    assert(!c.reconnectDue(mockNow+29999));assert(c.reconnectDue(mockNow+30000));
  }
  resetMocks();{
    GeminiLiveDirectClient c;char model[]="snapshot-model";auto cfg=config();cfg.model=model;
    assert(c.connectAsync(cfg));std::memset(model,'x',sizeof(model)-1);runWorker();assert(c.connected());
    assert(unmaskFirstText().find("models/snapshot-model")!=std::string::npos);
  }
  resetMocks();{
    GeminiLiveDirectClient c;open(c);uint8_t pcm[1280]={};
    assert(c.sendAudio(pcm,sizeof(pcm)));assert(unmaskFirstText().find("audio/pcm;rate=16000")!=std::string::npos);
    assert(!c.sendAudio(pcm,1281));
    mockWriteStalled=true;uint32_t before=mockNow;
    assert(!c.sendAudio(pcm,sizeof(pcm)));assert(mockNow-before==12);assert(!c.connected());
  }
  resetMocks();{
    GeminiLiveDirectClient c;open(c);mockWriteLimit=7;uint8_t pcm[640]={};
    assert(c.sendAudio(pcm,sizeof(pcm)));assert(c.connected());assert(mockWriteCalls>10);
  }
  resetMocks();{
    GeminiLiveDirectClient c;open(c);mockNow+=15000;c.service(mockNow,2048);
    assert(c.connected());assert((mockTx[0]&15)==9);
    mockNow+=10000;c.service(mockNow,2048);assert(!c.connected());
  }
  resetMocks();{
    GeminiLiveDirectClient c;open(c);mockNow+=15000;c.service(mockNow,2048);
    mockRx={0x8a,2,'r','d'};c.service(mockNow,2048);
    mockNow+=10000;c.service(mockNow,2048);assert(c.connected());
  }
  resetMocks();{
    GeminiLiveDirectClient c;open(c);mockNow+=15000;c.service(mockNow,2048);
    mockNow+=20000;c.service(mockNow,0);c.service(mockNow,2048);assert(c.connected());
  }
  resetMocks();{
    GeminiLiveDirectClient c;assert(c.connectAsync(config()));runWorker();mockNow+=10001;
    c.service(mockNow,0);assert(!c.connected()); // Setup deadline still runs.
  }
  resetMocks();{
    GeminiLiveDirectClient c;assert(c.connectAsync(config()));runWorker();
    mockRx={0x89,2,'r','d'};mockWriteStalled=true;const uint32_t before=mockNow;
    c.service(mockNow,2048);assert(!c.connected());assert(mockNow-before==12);
  }
  resetMocks();{
    CountingSink first,second;GeminiLiveDirectClient c(&first);open(c);
    const std::string setup="{\"setupComplete\":{}}";
    const auto receiveSetup=[&]{mockRx={0x81,uint8_t(setup.size())};mockRx.insert(mockRx.end(),setup.begin(),setup.end());c.service(mockNow,2048);};
    receiveSetup();assert(first.setups==1&&second.setups==0);
    c.setSink(&second);receiveSetup();assert(first.setups==1&&second.setups==1);
  }
  resetMocks();{
    GeminiLiveDirectClient c;open(c);assert(c.sendToolResponse("test-id","test-tool","{}"));
    const auto text=unmaskFirstText();assert(text.find("test-id")!=std::string::npos&&text.find("test-tool")!=std::string::npos);
  }
  std::cout<<"PASS: 13 async/offline/audio/parser client regression scenarios\n";
}
