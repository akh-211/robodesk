#include "TimeCompat.h"
#include <Arduino.h>
#include "MicCapturedFrame.h"
#include <Wire.h>
#include <WiFi.h>
#include "RuntimeSettings.h"
#include "SettingsDashboard.h"
#include "WakeWordController.h"
#include "GeminiLiveDirect.h"
#include "RoboGeminiSink.h"
#include "RoboBrain.h"
#include "GoogleGtsRootR1.h"
#include <ESP_I2S.h>
#include "NativeSpeakerI2S.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <LivingEyes.h>
#include <livingeyes/CorePerformances.h>
#include <livingeyes/ExtendedPerformances.h>
#include <livingeyes/PerceptionBridge.h>
#include <livingeyes/MotionSensorAdapter.h>
#include <livingeyes/SensorAdapters.h>
#include <livingeyes/VoiceAI.h>
#include <livingeyes/VoiceAudioBuffer.h>
#include <livingeyes/VoiceCharacterBridge.h>
#include <livingeyes/SonicCharacter.h>
#include <livingeyes/SonicSynth.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_ota_ops.h>
#include <Preferences.h>

#include <freertos/queue.h>

#if __has_include("secrets.h")
#include "secrets.h"
#define LE_VOICE_SECRETS_PRESENT 1
#else
#define LE_VOICE_SECRETS_PRESENT 0
#define WIFI_SSID "CHANGE_ME"
#define WIFI_PASSWORD "CHANGE_ME"
#define GEMINI_API_KEY "CHANGE_ME"
#define GEMINI_MODEL "gemini-3.8-live"
#define GEMINI_VOICE "Iapetus"
#define GEMINI_TLS_INSECURE 0
#define DASHBOARD_ADMIN_PIN "robodesk"
#define SETUP_AP_PASSWORD "robodesk123"
#endif

#ifndef DASHBOARD_ADMIN_PIN
#define DASHBOARD_ADMIN_PIN "robodesk"
#endif
#ifndef SETUP_AP_PASSWORD
#define SETUP_AP_PASSWORD "robodesk123"
#endif
#ifndef GEMINI_LANGUAGE_CODE
#define GEMINI_LANGUAGE_CODE "id-ID"
#endif

// LivingEyes / RoboDesk v0.15.0-dev Sonic Character + Eye Acting + Living Character reference firmware.
// Hardware: ESP32-S3 N16R8 + SSD1306 + MPU6050-compatible + TTP223 +
// AM312 + AHT20 + BMP280 + INMP441 + MAX98357A + 8ohm speaker.
// Standalone direct mode: ESP32-S3 connects to Gemini Live over WSS.
// The API key is stored in secrets.h on-device; do not publish firmware binaries containing it.

// ---------------- PIN MAP ----------------
constexpr int PIN_SDA = 8, PIN_SCL = 9, PIN_MPU_INT = 10;
constexpr int PIN_TOUCH = 7, PIN_PIR = 15;
constexpr int PIN_MIC_WS = 4, PIN_MIC_BCLK = 5, PIN_MIC_DATA = 6;
constexpr int PIN_SPK_DIN = 11, PIN_SPK_BCLK = 12, PIN_SPK_LRC = 13;
constexpr uint8_t OLED_ADDR = 0x3C, MPU_ADDR = 0x68, BMP_ADDR = 0x77;

// ---------------- AUDIO / VAD ----------------
constexpr uint32_t MIC_RATE = 16000, SPEAKER_RATE = 24000;
constexpr size_t GEMINI_SPEECH_PRIME_BYTES = 5760; // 120 ms PCM16 @ 24 kHz, plus 120 ms native DMA cushion
constexpr size_t SPEAKER_SERVICE_MONO_SAMPLES = 480; // 20 ms per write; enough DMA slack for the single-loop scheduler
constexpr float MIC_DIGITAL_GAIN = 3.2f;             // INMP441 -> PCM16 gain; still protected by hard limiter
constexpr float VAD_END_MIN_MULTIPLIER = 1.9f;       // end sooner when post-speech board/room noise stays elevated
constexpr uint16_t NOISE_WINDOW_FRAMES = 25;          // 500 ms at 20 ms/frame; use a minimum window so speech/echo cannot ratchet the floor
constexpr uint32_t SONIC_POST_GEMINI_QUIET_MS = 1000; // leave a clean follow-up listening window after Gemini speech
constexpr size_t MIC_RAW_WORDS = MIC_FRAME_SAMPLES * 2; // stereo slots from INMP441
constexpr size_t MIC_PCM_BYTES = MIC_FRAME_SAMPLES * sizeof(int16_t);
constexpr size_t MIC_TX_BATCH_BYTES = MIC_PCM_BYTES * 2; // 40 ms: Google Live best-practice range, halves WS overhead
constexpr uint8_t MIC_QUEUE_CAP = 8;                     // 160 ms capture elasticity, bounded
constexpr uint8_t MIC_SERVICE_MAX_FRAMES = 6;            // bound work per Arduino loop pass
#ifndef ROBODESK_MIC_SLOT
#define ROBODESK_MIC_SLOT -1 // -1: auto, 0: INMP441 L/R=GND, 1: L/R=3.3V
#endif
static_assert(ROBODESK_MIC_SLOT>=-1&&ROBODESK_MIC_SLOT<=1,"Invalid INMP441 slot");
constexpr uint32_t TOUCH_DEBOUNCE_MS = 60;
constexpr uint32_t IMU_PERIOD_MS = 10, ENV_PERIOD_MS = 2000, FRAME_PERIOD_US = 33333;
constexpr uint16_t VAD_START_FRAMES = 3;              // 60 ms
constexpr uint32_t VAD_MAX_UTTERANCE_MS = 8000;
constexpr float VAD_START_MULTIPLIER = 3.5f;
constexpr float VAD_END_MULTIPLIER = 1.9f;
constexpr float VAD_ABS_START = 420.0f;
constexpr float VAD_ABS_END = 360.0f;
#ifndef VOICE_ENABLE_EXPERIMENTAL_BARGE_IN
#define VOICE_ENABLE_EXPERIMENTAL_BARGE_IN 0
#endif
#ifndef VOICE_SPEAKER_GAIN
#define VOICE_SPEAKER_GAIN 0.45f  // conservative default for an 8 ohm / 1 W speaker
#endif
#ifndef VOICE_POST_SPEAK_GUARD_MS
#define VOICE_POST_SPEAK_GUARD_MS 300u  // short half-duplex echo-tail guard after speaker playback
#endif

// ---------------- DISPLAY / CHARACTER ----------------
Adafruit_SSD1306 oled(128, 64, &Wire, -1, 400000, 400000);
void flushDisplay(Adafruit_SSD1306& d) { d.display(); }
uint32_t clockUs() { return uint32_t(micros()); }
livingeyes::GfxSurface<Adafruit_SSD1306> surface(oled, SSD1306_WHITE, SSD1306_BLACK, flushDisplay, clockUs);
livingeyes::Character robot(surface);
livingeyes::PerformanceGraph coreBank, extendedBank;
livingeyes::ChoreographyEngine choreography;
livingeyes::RelationshipMemory relationships;
livingeyes::LifeDirector life;
livingeyes::PerceptionBridge perception(robot);

livingeyes::MotionAdapterConfig makeMotionConfig() {
  livingeyes::MotionAdapterConfig c;
  c.alpha=.18f; c.tiltThreshold=.38f; c.shakeGyro=2.8f; c.pickupDeltaG=.34f;
  c.fallG=.22f; c.fallRearmG=.65f; c.fallRearmMs=350; c.stillGyro=.12f;
  c.tiltHoldMs=1200; c.shakeCooldownMs=900; c.pickupHoldMs=180; c.stillHoldMs=900;
  return c;
}
livingeyes::MotionAdapterConfig motionCfg = makeMotionConfig();
livingeyes::MotionSensorAdapter motionAdapter(perception, motionCfg);
livingeyes::TouchSensorAdapter touchAdapter(perception);

livingeyes::VoiceAiSession voice;
livingeyes::VoiceCharacterBridge voiceFace(&robot);
livingeyes::VoiceByteRing<32768> speakerRing;  // ~683 ms of 24k PCM16 mono; TCP backpressure prevents overflow
livingeyes::VoiceByteRing<6400> preRoll;       // ~200 ms of 16k PCM16 mono
livingeyes::SonicDirector sonicDirector;
livingeyes::ProceduralSonicSynth sonicSynth;
bool sonicPlaybackActive=false,sonicMouthActive=false;
livingeyes::SonicCue sonicActiveCue=livingeyes::SonicCue::Curious;
livingeyes::SonicPriority sonicActivePriority=livingeyes::SonicPriority::Ambient;
uint32_t sonicStarted=0,sonicFinished=0,sonicPreemptedBySpeech=0,sonicSafetyPreemptions=0,sonicMutedSpeechBytes=0;
uint32_t sonicTouchAt=0,sonicMotionAt=0,sonicIdleAt=0,sonicFallAt=0;
bool sonicTouchPrev=false,sonicPickedPrev=false,sonicPirPrev=false;
uint16_t localMinuteOfDay=0;

RuntimeSettings runtimeSettings;
RuntimeSettingsStore settingsStore;
SettingsDashboard settingsDashboard;
WakeWordController wakeWord;
uint32_t setupApAt=0;
RoboBrain brain;
char runtimeSystemPrompt[4600]={0};
uint16_t brainDayIndex=0;
bool clockSynced=false;
uint32_t lastClockSyncAt=0;
uint32_t proactiveVoiceAt=0;
bool pirPrevious=false;

// ---------------- HARDWARE ----------------
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
bool ahtOK=false, bmpOK=false;
I2SClass MicI2S(I2S_NUM_0);
RoboDeskNativeSpeakerI2S SpeakerI2S;
bool micOK=false, speakerOK=false;
int32_t micRaw[MIC_RAW_WORDS];
StaticQueue_t micQueueStruct;
uint8_t micQueueStorage[MIC_QUEUE_CAP * sizeof(MicCapturedFrame)];
QueueHandle_t micQueue=nullptr;
TaskHandle_t micTaskHandle=nullptr;
volatile bool micTaskPaused=false;
volatile bool micTaskBusy=false;
std::atomic<bool> micFrameResetRequested{false};
volatile uint32_t micCapturedFrames=0,micCaptureDrops=0,micCaptureGapMaxUs=0;
volatile float micLastRms=0.0f;
volatile uint8_t micQueueHighWater=0;
uint8_t micTxBatch[MIC_TX_BATCH_BYTES];
size_t micTxBatchLen=0;
uint32_t micTxFailures=0;
uint32_t micTxMaxUs=0;
float micDc = 0.0f;
float micSlotEnergyL=0.0f,micSlotEnergyR=0.0f;
int8_t micSelectedSlot=ROBODESK_MIC_SLOT;
uint16_t micSlotProbeFrames=0,micSlotSwitchVotes=0;

volatile uint32_t mpuIRQ = 0;
void IRAM_ATTR onMPU(){ ++mpuIRQ; }
float lastG=1.0f, lastGyro=0.0f, envTemp=0, envHumidity=0, envPressure=0;
bool touchRaw=false, touchStable=false, pirState=false;
uint32_t touchRawChangedAt=0;

// ---------------- DIRECT GEMINI LIVE NETWORK ----------------
bool geminiReady=false;
volatile bool firmwareUpdateActive=false;
bool otaPendingVerify=false;
bool otaSelfTestFailed=false;
uint32_t otaSelfTestAt=0;
uint32_t otaMicFrameBaseline=0,otaSpeakerFrameBaseline=0;
volatile bool pendingReady=false;
bool serialAudioTestActive=false;
uint32_t wifiRetryAt=0;
bool wifiWasConnected=false;
uint32_t statsAt=0;
volatile uint32_t lastAudioRxAt=0;
uint32_t utteranceStartedAt=0;
uint16_t vadVoiceFrames=0, vadSilenceFrames=0;
bool utteranceActive=false;
volatile uint32_t captureGuardUntil=0;
float noiseFloor=120.0f;
float noiseWindow[NOISE_WINDOW_FRAMES]={0};
uint16_t noiseWindowFrames=0;
bool noiseCalibrated=false;
uint32_t sonicConversationQuietUntil=0;
uint32_t micFramesSent=0, micClips=0, serverVadFinals=0, localVadEnds=0;
volatile uint32_t speakerFramesPlayed=0, speakerUnderruns=0, speakerStarves=0;
volatile uint32_t speakerWriteGapMaxUs=0,speakerWriteTimeMaxUs=0,lastSpeakerWriteEndUs=0;
volatile uint16_t speakerLevelQ15=0;
volatile bool speakerTaskBusy=false;
volatile uint32_t speakerAudibleUntil=0;
volatile uint32_t speakerGeneration=0;
volatile bool speakerDrainSignal=false;
volatile bool speakerGeminiActive=false;
TaskHandle_t speakerTaskHandle=nullptr;
StaticSemaphore_t speakerRingMutexStorage;
SemaphoreHandle_t speakerRingMutex=nullptr;
uint32_t utterancePcmFrames=0;
uint32_t geminiAudioOverflow=0;
volatile bool geminiSpeechPriming=false;
uint32_t geminiSpeechPrimeAt=0,geminiSpeechPrimeStarts=0,geminiSpeechPrimeForced=0;
char geminiSessionHandle[1024]={0};

// HOTFIX16: Gemini tool calls are deferred until after GeminiStreamParser::endMessage()
// has returned. This prevents parser + brain + tool-response scratch from nesting on
// the Arduino loop task stack. Fixed capacity preserves bounded realtime behavior.
struct PendingGeminiToolCall {
  char id[96];
  char name[96];
  char args[900];
};
static const uint8_t GEMINI_TOOL_QUEUE_CAP=4;
PendingGeminiToolCall geminiToolQueue[GEMINI_TOOL_QUEUE_CAP];
uint8_t geminiToolHead=0,geminiToolTail=0,geminiToolCount=0;
char geminiToolResult[1500];


// HOTFIX20: speakerRing is shared between the Arduino loop (Gemini/Sonic producer)
// and a dedicated high-priority I2S feeder task. Keep the fixed-capacity ring, but
// serialize access with a static mutex so the Character core remains bounded.
struct SpeakerRingLock {
  SpeakerRingLock(){ if(speakerRingMutex) xSemaphoreTake(speakerRingMutex,portMAX_DELAY); }
  ~SpeakerRingLock(){ if(speakerRingMutex) xSemaphoreGive(speakerRingMutex); }
};
size_t speakerSize(){ SpeakerRingLock l; return speakerRing.size(); }
size_t speakerFree(){ SpeakerRingLock l; return speakerRing.free(); }
size_t speakerWrite(const uint8_t*data,size_t len){ SpeakerRingLock l; return speakerRing.write(data,len); }
size_t speakerRead(uint8_t*data,size_t len,uint32_t&generation){ SpeakerRingLock l;generation=speakerGeneration;const size_t available=speakerRing.size();return speakerRing.read(data,(available<len?available:len)&~size_t(1)); }
void speakerClear(){ SpeakerRingLock l; speakerRing.clear();++speakerGeneration; }
bool speakerDrained(uint32_t now){ SpeakerRingLock l;return speakerRing.size()==0&&!speakerTaskBusy&&int32_t(now-speakerAudibleUntil)>=0; }
uint32_t speakerDropped(){ SpeakerRingLock l; return speakerRing.dropped(); }

float noisePercentile30(){
  float a[NOISE_WINDOW_FRAMES];
  for(uint16_t i=0;i<NOISE_WINDOW_FRAMES;i++)a[i]=noiseWindow[i];
  for(uint16_t i=1;i<NOISE_WINDOW_FRAMES;i++){
    float v=a[i];int j=int(i)-1;
    while(j>=0&&a[j]>v){a[j+1]=a[j];--j;}a[j+1]=v;
  }
  return a[(NOISE_WINDOW_FRAMES*3)/10];
}
void updateNoiseEstimator(float rms){
  noiseWindow[noiseWindowFrames++]=rms;
  if(noiseWindowFrames<NOISE_WINDOW_FRAMES)return;
  const float candidate=noisePercentile30();
  if(!noiseCalibrated){noiseFloor=fmaxf(100.0f,candidate);noiseCalibrated=true;}
  else {
    const float delta=candidate-noiseFloor;
    const float step=fmaxf(-120.0f,fminf(delta,120.0f));
    noiseFloor+=step*.18f;
  }
  noiseFloor=fmaxf(100.0f,fminf(noiseFloor,1400.0f));
  noiseWindowFrames=0;
}

void speakerTaskMain(void*){
  static uint8_t monoBytes[SPEAKER_SERVICE_MONO_SAMPLES*2];
  static int16_t stereo[SPEAKER_SERVICE_MONO_SAMPLES*2];
  size_t pendingBytes=0,writeOffset=0;
  uint32_t blockGeneration=0;
  bool emptyEpisode=false;
  for(;;){
    if(!speakerOK || geminiSpeechPriming){
      speakerTaskBusy=false;vTaskDelay(pdMS_TO_TICKS(1));continue;
    }
    // Mark busy before removing PCM, so the loop cannot declare it drained.
    speakerTaskBusy=true;
    if(pendingBytes&&blockGeneration!=speakerGeneration){pendingBytes=writeOffset=0;}
    size_t n=pendingBytes?0:speakerRead(monoBytes,sizeof(monoBytes),blockGeneration);
    if(!n&&!pendingBytes){
      speakerTaskBusy=false;
      if(!emptyEpisode && speakerGeminiActive && !pendingReady && int32_t(millis()-speakerAudibleUntil)>=0){++speakerStarves;emptyEpisode=true;}
      if(!speakerGeminiActive)lastSpeakerWriteEndUs=0;
      vTaskDelay(pdMS_TO_TICKS(1));continue;
    }
    emptyEpisode=false;speakerTaskBusy=true;
    size_t samples=n/2;double sumSq=0;
    for(size_t i=0;i<samples;i++){
      int16_t raw=int16_t(uint16_t(monoBytes[i*2])|(uint16_t(monoBytes[i*2+1])<<8));
      float scaled=float(raw)*runtimeSettings.speakerGain();
      if(scaled>32767.f)scaled=32767.f;if(scaled<-32768.f)scaled=-32768.f;
      int16_t v=int16_t(scaled);stereo[i*2]=v;stereo[i*2+1]=v;sumSq+=double(v)*double(v);
    }
    if(n){
      pendingBytes=samples*2*sizeof(int16_t);writeOffset=0;
      float rms=samples?float(sqrt(sumSq/samples)):0;float level=fminf(1.f,rms/6500.f);
      speakerLevelQ15=uint16_t(level*32767.f);
    }
    const size_t expected=pendingBytes-writeOffset;
    if(blockGeneration!=speakerGeneration){pendingBytes=writeOffset=0;speakerTaskBusy=false;continue;}
    const uint32_t writeStartUs=micros();
    const uint32_t prevEnd=lastSpeakerWriteEndUs;
    if(prevEnd){const uint32_t gap=uint32_t(writeStartUs-prevEnd);if(gap>speakerWriteGapMaxUs)speakerWriteGapMaxUs=gap;}
    size_t written=SpeakerI2S.write(reinterpret_cast<uint8_t*>(stereo)+writeOffset,expected);
    const uint32_t writeEndUs=micros();lastSpeakerWriteEndUs=writeEndUs;
    const uint32_t writeUs=uint32_t(writeEndUs-writeStartUs);if(writeUs>speakerWriteTimeMaxUs)speakerWriteTimeMaxUs=writeUs;
    if(written!=expected)++speakerUnderruns;
    if(written){
      speakerAudibleUntil=millis()+SpeakerI2S.dmaDurationMs(SPEAKER_RATE);
      captureGuardUntil=speakerAudibleUntil+uint32_t(runtimeSettings.postSpeakGuardMs);
      speakerFramesPlayed+=uint32_t(written/(2*sizeof(int16_t)));
      writeOffset+=written;
    }
    if(writeOffset<pendingBytes){vTaskDelay(pdMS_TO_TICKS(1));continue;}
    pendingBytes=writeOffset=0;
    speakerTaskBusy=false;
    if(speakerSize()==0)speakerDrainSignal=true;
  }
}

bool wakeWindowOpen=false;
uint32_t wakeWindowUntil=0;
uint32_t wakeLastTriggerAt=0;

void rebuildSystemPrompt(){
  brain.setMemoryEnabled(runtimeSettings.memoryEnabled!=0);
  brain.buildSystemPrompt(runtimeSystemPrompt,sizeof(runtimeSystemPrompt),runtimeSettings.robotName,runtimeSettings.speechStyle,brainDayIndex);
}

void applyFaceSettings(){
  livingeyes::FaceLifeProfile fp;
  fp.enabled=runtimeSettings.faceLifeEnabled!=0;
  fp.pupils=runtimeSettings.facePupils!=0;
  fp.highlights=runtimeSettings.facePupils!=0;
  fp.brows=runtimeSettings.faceBrows!=0;
  fp.lashes=runtimeSettings.faceLashes!=0;
  fp.autoBrows=runtimeSettings.faceAutoBrows!=0;
  fp.autoLashes=runtimeSettings.faceAutoLashes!=0;
  fp.pupilLessActing=runtimeSettings.facePupilLessActing!=0;
  fp.microMotion=float(runtimeSettings.faceMicroX100)/100.f;
  fp.silhouetteGain=float(runtimeSettings.faceSilhouetteX100)/100.f;
  fp.gazeReach=float(runtimeSettings.faceGazeReachX100)/100.f;
  robot.setFaceLifeProfile(fp);
  if(runtimeSettings.mouthMode==1)robot.setMouthPolicy(livingeyes::MouthPolicy::Hidden);
  else if(runtimeSettings.mouthMode==2)robot.setMouthPolicy(livingeyes::MouthPolicy::Always);
  else robot.setMouthPolicy(livingeyes::MouthPolicy::Automatic);
}

void applySonicSettings(){
  livingeyes::SonicProfile p;
  p.enabled=runtimeSettings.masterSound!=0;p.characterSfx=runtimeSettings.characterSfx!=0;p.wakeSfx=runtimeSettings.wakeSfx!=0;p.touchSfx=runtimeSettings.touchSfx!=0;p.motionSfx=runtimeSettings.motionSfx!=0;p.notificationSfx=runtimeSettings.notificationSfx!=0;p.quietHours=runtimeSettings.quietHoursEnabled!=0;
  p.frequency=runtimeSettings.sonicFrequency==0?livingeyes::SonicFrequency::Low:(runtimeSettings.sonicFrequency==2?livingeyes::SonicFrequency::Expressive:livingeyes::SonicFrequency::Normal);
  p.intensity=float(runtimeSettings.sfxIntensityX100)/100.f;p.quietGain=float(runtimeSettings.quietGainX100)/100.f;p.quietStartMin=runtimeSettings.quietStartMin;p.quietEndMin=runtimeSettings.quietEndMin;p.clampToBounds();sonicDirector.setProfile(p);
}

bool sonicQuietNow(){return sonicDirector.quietAt(localMinuteOfDay,clockSynced);}

bool sonicCueAllowed(livingeyes::SonicCue cue){
  if(!runtimeSettings.masterSound||!runtimeSettings.characterSfx)return false;
  if(cue==livingeyes::SonicCue::WakeAck&&!runtimeSettings.wakeSfx)return false;
  if(cue==livingeyes::SonicCue::Affection&&!runtimeSettings.touchSfx)return false;
  if((cue==livingeyes::SonicCue::Pickup||cue==livingeyes::SonicCue::PutDown||cue==livingeyes::SonicCue::Shake||cue==livingeyes::SonicCue::Surprise)&&!runtimeSettings.motionSfx)return false;
  if((cue==livingeyes::SonicCue::Notification||cue==livingeyes::SonicCue::Success||cue==livingeyes::SonicCue::Failure||cue==livingeyes::SonicCue::Error)&&!runtimeSettings.notificationSfx)return false;
  return true;
}

bool requestSonic(livingeyes::SonicCue cue,livingeyes::SonicPriority priority,float strength,uint32_t now,bool force=false){
  if(!sonicCueAllowed(cue))return false;
  // After a Gemini reply, keep ordinary character/ambient chirps out of the mic
  // during the most likely user follow-up window. Safety and wake cues remain allowed.
  if(int32_t(sonicConversationQuietUntil-now)>0 && (priority==livingeyes::SonicPriority::Character || priority==livingeyes::SonicPriority::Ambient)) return false;
  return sonicDirector.request(cue,priority,strength,now,force);
}

livingeyes::SonicCue sonicCueFromName(const char*name,bool*ok=nullptr){
  if(ok)*ok=true;
  if(!name){if(ok)*ok=false;return livingeyes::SonicCue::Curious;}
  for(unsigned i=0;i<unsigned(livingeyes::SonicCue::Count);i++){livingeyes::SonicCue c=livingeyes::SonicCue(i);if(strcmp(name,livingeyes::sonicCueName(c))==0)return c;}
  if(!strcmp(name,"love"))return livingeyes::SonicCue::Affection;
  if(!strcmp(name,"sleep"))return livingeyes::SonicCue::Sleepy;
  if(!strcmp(name,"wake"))return livingeyes::SonicCue::WakeAck;
  if(ok)*ok=false;
  return livingeyes::SonicCue::Curious;
}

bool dashboardSoundTest(void*,const char*name){bool ok=false;livingeyes::SonicCue cue=sonicCueFromName(name,&ok);if(!ok)return false;return requestSonic(cue,cue==livingeyes::SonicCue::WakeAck?livingeyes::SonicPriority::Wake:livingeyes::SonicPriority::Character,1.f,millis(),true);}

RoboGeminiSink geminiSink;
GeminiLiveDirectClient gemini(&geminiSink);

void applyVoiceState(livingeyes::VoiceAiState s){ voiceFace.apply(s); }
bool captureGuardActive(uint32_t now){ return int32_t(captureGuardUntil-now)>0; }
void armCaptureGuard(uint32_t now){ captureGuardUntil=now+uint32_t(runtimeSettings.postSpeakGuardMs); }
void pauseMicCaptureTask(){
  micFrameResetRequested.store(true);
  micTaskPaused=true;const uint32_t start=millis();while(micTaskBusy&&uint32_t(millis()-start)<80u)delay(1);if(micQueue)xQueueReset(micQueue);micTxBatchLen=0;
}
void resumeMicCaptureTask(){ if(micQueue)xQueueReset(micQueue);micTxBatchLen=0;micFrameResetRequested.store(true);micTaskPaused=false; }

const char* inputModeName(){
  if(runtimeSettings.inputMode==RuntimeSettings::TouchToTalk)return "touch";
  if(runtimeSettings.inputMode==RuntimeSettings::WakeWord)return "wake";
  return "always";
}

bool wakeWindowActive(uint32_t now){
  return runtimeSettings.inputMode==RuntimeSettings::WakeWord && wakeWindowOpen && int32_t(wakeWindowUntil-now)>0;
}

void extendWakeWindow(uint32_t now){
  if(runtimeSettings.inputMode!=RuntimeSettings::WakeWord)return;
  wakeWindowOpen=true;
  wakeWindowUntil=now+uint32_t(runtimeSettings.wakeFollowupMs);
}

void setReadyWhenAudioDrained(){
  if(speakerDrained(millis())){
    const uint32_t now=millis();armCaptureGuard(now);sonicConversationQuietUntil=now+SONIC_POST_GEMINI_QUIET_MS;extendWakeWindow(now);speakerGeminiActive=false;voice.turnComplete(now);applyVoiceState(voice.state());pendingReady=false;serialAudioTestActive=false;
  } else pendingReady=true;
}

void startGeminiSpeechPlayback(uint32_t now,bool forced){
  if(!geminiSpeechPriming)return;
  const size_t queued=speakerSize();
  const uint32_t waited=uint32_t(now-geminiSpeechPrimeAt);
  geminiSpeechPriming=false;speakerGeminiActive=true;++geminiSpeechPrimeStarts;if(forced)++geminiSpeechPrimeForced;
  if(voice.state()!=livingeyes::VoiceAiState::Speaking){voice.speaking(now);applyVoiceState(voice.state());}
  Serial.printf("LEV,AUDIO,SPEECH_PRIMED,bytes=%u,target=%u,waitMs=%lu,forced=%u\n",unsigned(queued),unsigned(GEMINI_SPEECH_PRIME_BYTES),(unsigned long)waited,unsigned(forced));
}

void RoboGeminiSink::onGeminiSetupComplete(){
  uint32_t now=millis();gemini.markSetupComplete();geminiReady=true;serialAudioTestActive=false;voice.ready(now);applyVoiceState(voice.state());
  Serial.printf("LEV,GEMINI,READY,model=%s,voice=%s\n",runtimeSettings.geminiModel,runtimeSettings.geminiVoice);
}
void RoboGeminiSink::onGeminiAudio(const uint8_t* data,size_t len){
  uint32_t now=millis();
  if(!runtimeSettings.masterSound||!runtimeSettings.speechEnabled){sonicMutedSpeechBytes+=uint32_t(len);voice.noteRxAudio(len);lastAudioRxAt=now;return;}
  if(sonicPlaybackActive||sonicSynth.active()){sonicSynth.reset();speakerClear();if(sonicMouthActive){robot.speaking(false);sonicMouthActive=false;}sonicPlaybackActive=false;++sonicPreemptedBySpeech;}
  if(!geminiSpeechPriming&&voice.state()!=livingeyes::VoiceAiState::Speaking){geminiSpeechPriming=true;geminiSpeechPrimeAt=now;}
  size_t accepted=speakerWrite(data,len);if(accepted<len)geminiAudioOverflow+=uint32_t(len-accepted);
  voice.noteRxAudio(accepted);lastAudioRxAt=now;
  if(geminiSpeechPriming&&speakerSize()>=GEMINI_SPEECH_PRIME_BYTES)startGeminiSpeechPlayback(now,false);
}
void RoboGeminiSink::onGeminiInputTranscript(const char* text){
  const uint32_t now=millis();
  if(utteranceActive){
    utteranceActive=false;vadSilenceFrames=vadVoiceFrames=0;preRoll.clear();micTxBatchLen=0;++serverVadFinals;
    if(voice.state()==livingeyes::VoiceAiState::Listening){voice.thinking(now);applyVoiceState(voice.state());}
  }
  Serial.printf("LEV,GEMINI,INPUT,%s\n",text?text:"");
}
void RoboGeminiSink::onGeminiOutputTranscript(const char* text){Serial.printf("LEV,GEMINI,OUTPUT,%s\n",text?text:"");}
void RoboGeminiSink::onGeminiTurnComplete(){brain.onConversationComplete(millis(),brainDayIndex,true);setReadyWhenAudioDrained();}
void RoboGeminiSink::onGeminiWaitingForInput(){
  const uint32_t now=millis();
  Serial.println("LEV,GEMINI,WAITING_FOR_INPUT");
  if(!utteranceActive)setReadyWhenAudioDrained();
}
void RoboGeminiSink::onGeminiGenerationComplete(){Serial.println("LEV,GEMINI,GENERATION_COMPLETE");}
void RoboGeminiSink::onGeminiInterrupted(){uint32_t now=millis();brain.onInterrupted();speakerClear();geminiSpeechPriming=false;speakerGeminiActive=false;pendingReady=false;serialAudioTestActive=false;voice.interrupted(now);applyVoiceState(voice.state());Serial.println("LEV,GEMINI,INTERRUPTED");}
void RoboGeminiSink::onGeminiGoAway(){Serial.println("LEV,GEMINI,GO_AWAY");speakerGeminiActive=false;gemini.disconnect(millis());geminiReady=false;voice.disconnected(millis());applyVoiceState(voice.state());}
void RoboGeminiSink::onGeminiSessionHandle(const char* handle){
  if(!handle||!handle[0])return;
  strncpy(geminiSessionHandle,handle,sizeof(geminiSessionHandle)-1);
  geminiSessionHandle[sizeof(geminiSessionHandle)-1]=0;
  Serial.printf("LEV,GEMINI,RESUME_HANDLE,len=%u\n",unsigned(strlen(geminiSessionHandle)));
}
void RoboGeminiSink::onGeminiToolCall(const char* id,const char* name,const char* argsJson){
  if(!id||!name||!argsJson)return;
  if(geminiToolCount>=GEMINI_TOOL_QUEUE_CAP){Serial.printf("LEV,BRAIN,TOOL_QUEUE_FULL,name=%s\n",name);return;}
  PendingGeminiToolCall& t=geminiToolQueue[geminiToolTail];
  strncpy(t.id,id,sizeof(t.id)-1);t.id[sizeof(t.id)-1]=0;
  strncpy(t.name,name,sizeof(t.name)-1);t.name[sizeof(t.name)-1]=0;
  strncpy(t.args,argsJson,sizeof(t.args)-1);t.args[sizeof(t.args)-1]=0;
  geminiToolTail=uint8_t((geminiToolTail+1u)%GEMINI_TOOL_QUEUE_CAP);++geminiToolCount;
  Serial.printf("LEV,BRAIN,TOOL_QUEUED,name=%s,q=%u\n",t.name,unsigned(geminiToolCount));
}

void serviceGeminiTools(){
  if(!geminiToolCount||!gemini.connected())return;
  PendingGeminiToolCall& t=geminiToolQueue[geminiToolHead];
  bool handled=brain.executeTool(t.name,t.args,brainDayIndex,geminiToolResult,sizeof(geminiToolResult));
  if(handled)rebuildSystemPrompt();
  Serial.printf("LEV,BRAIN,TOOL,name=%s,handled=%u,q=%u\n",t.name,unsigned(handled),unsigned(geminiToolCount));
  if(!gemini.sendToolResponse(t.id,t.name,geminiToolResult))Serial.println("LEV,BRAIN,TOOL_RESPONSE_FAIL");
  geminiToolHead=uint8_t((geminiToolHead+1u)%GEMINI_TOOL_QUEUE_CAP);--geminiToolCount;
}
void RoboGeminiSink::onGeminiProtocolError(const char* text){gemini.markProtocolError();voice.fault(millis());applyVoiceState(voice.state());Serial.printf("LEV,GEMINI,ERROR,%s\n",text?text:"");}

void beginWiFi(){
  WiFi.mode(WIFI_STA);WiFi.setSleep(false);wifiRetryAt=millis();setupApAt=wifiRetryAt+15000;
  if(runtimeSettings.wifiConfigured()){
    WiFi.begin(runtimeSettings.wifiSsid,runtimeSettings.wifiPassword);Serial.println("LEV,WIFI,CONNECTING");
  } else {
    Serial.println("LEV,WIFI,UNCONFIGURED");settingsDashboard.startSetupAp();
  }
}

void resetGeminiOffline(uint32_t now){
  if(gemini.connecting()||gemini.connected())gemini.disconnect(now);
  geminiReady=false;pendingReady=false;
  utteranceActive=false;vadVoiceFrames=vadSilenceFrames=0;
  micTxBatchLen=0;preRoll.clear();
  geminiToolHead=geminiToolTail=geminiToolCount=0;
  speakerClear();speakerGeminiActive=false;geminiSpeechPriming=false;
  voice.disconnected(now);applyVoiceState(voice.state());
}

bool dashboardFirmwareUpdateControl(void*,bool starting){
  if(starting){
    const auto state=voice.state();
    if(firmwareUpdateActive||utteranceActive||speakerSize()!=0||speakerTaskBusy||serialAudioTestActive||wakeWord.running()||
       state==livingeyes::VoiceAiState::Speaking||state==livingeyes::VoiceAiState::Thinking)return false;
    firmwareUpdateActive=true;
    resetGeminiOffline(millis());
    pauseMicCaptureTask();
    const uint32_t deadline=millis()+13000u;
    while(gemini.connecting()&&int32_t(millis()-deadline)<0)vTaskDelay(pdMS_TO_TICKS(25));
    if(gemini.connecting()){
      Serial.println("LEV,OTA,QUIESCE,CONNECT_TIMEOUT");
      firmwareUpdateActive=false;
      resumeMicCaptureTask();
      return false;
    }
    Serial.println("LEV,OTA,QUIESCE,NETWORK_IDLE");
    return true;
  }
  pauseMicCaptureTask();
  firmwareUpdateActive=false;
  resumeMicCaptureTask();
  return true;
}

bool reconcileOtaVersionForRunningImage(const esp_partition_t* running){
  if(!running)return false;
  Preferences prefs;
  if(!prefs.begin("robodesk_ota",false))return false;
  const uint32_t staged=prefs.getUInt("pending",0);
  const uint32_t target=prefs.getUInt("pend_off",0xffffffffUL);
  uint32_t current=prefs.getUInt("version",0);
  bool ok=true;
  if(staged){
    if(running->address==target){
      if(staged>current&&prefs.putUInt("version",staged)!=sizeof(uint32_t))ok=false;
      else if(staged>current)current=staged;
    }
    if(ok){prefs.remove("pending");prefs.remove("pend_off");}
  }
  if(ok&&current==0&&prefs.putUInt("version",ROBODESK_OTA_INITIAL_VERSION)!=sizeof(uint32_t))ok=false;
  prefs.end();
  return ok;
}

bool otaVersionStagedFor(const esp_partition_t* running){
  if(!running)return false;
  Preferences prefs;
  if(!prefs.begin("robodesk_ota",true))return false;
  const uint32_t staged=prefs.getUInt("pending",0);
  const uint32_t target=prefs.getUInt("pend_off",0xffffffffUL);
  prefs.end();
  return staged>0&&running->address==target;
}

void beginOtaBootSelfTest(){
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
  const esp_partition_t* running=esp_ota_get_running_partition();
  esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
  otaPendingVerify=running&&esp_ota_get_state_partition(running,&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY;
  if(!otaPendingVerify&&running&&!reconcileOtaVersionForRunningImage(running))Serial.println("LEV,OTA,VERSION,RECONCILE_PENDING");
  otaSelfTestAt=millis();
  otaMicFrameBaseline=micCapturedFrames;
  otaSpeakerFrameBaseline=speakerFramesPlayed;
  if(otaPendingVerify)Serial.println("LEV,OTA,SELFTEST,START,network=not_required");
#endif
}

void serviceOtaBootSelfTest(uint32_t now){
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
  if(!otaPendingVerify||otaSelfTestFailed||uint32_t(now-otaSelfTestAt)<8000u)return;
  const bool micExercised=runtimeSettings.inputMode==RuntimeSettings::WakeWord&&wakeWord.running()
      ? wakeWord.available()
      : uint32_t(micCapturedFrames-otaMicFrameBaseline)>=10u&&micSelectedSlot>=0;
  // Do not require audible input or output during boot: offline/quiet rooms are valid.
  // Driver initialization, running audio tasks, and active mic frame capture are the local checks.
  const bool passed=micOK&&speakerOK&&micTaskHandle&&speakerTaskHandle&&micExercised;
  if(passed){
    if(!otaVersionStagedFor(esp_ota_get_running_partition())){
      Serial.println("LEV,OTA,SELFTEST,FAIL,version_stage=0");
      otaSelfTestFailed=true;
      if(esp_ota_mark_app_invalid_rollback_and_reboot()!=ESP_OK)ESP.restart();
      return;
    }
    const esp_err_t result=esp_ota_mark_app_valid_cancel_rollback();
    if(result==ESP_OK){
      otaPendingVerify=false;
      if(!reconcileOtaVersionForRunningImage(esp_ota_get_running_partition()))Serial.println("LEV,OTA,VERSION,RECONCILE_PENDING");
      Serial.println("LEV,OTA,SELFTEST,PASS,rollback=cancelled");return;
    }
    Serial.printf("LEV,OTA,SELFTEST,CONFIRM_FAIL,err=%d\n",int(result));
  }else{
    Serial.printf("LEV,OTA,SELFTEST,FAIL,mic=%u,speaker=%u,micTask=%u,speakerTask=%u,micFrames=%lu,micRms=%.1f,speakerFrames=%lu\n",unsigned(micOK),unsigned(speakerOK),unsigned(micTaskHandle!=nullptr),unsigned(speakerTaskHandle!=nullptr),static_cast<unsigned long>(micCapturedFrames-otaMicFrameBaseline),double(micLastRms),static_cast<unsigned long>(speakerFramesPlayed-otaSpeakerFrameBaseline));
  }
  otaSelfTestFailed=true;
  if(esp_ota_mark_app_invalid_rollback_and_reboot()!=ESP_OK)ESP.restart();
#else
  (void)now;
#endif
}

void fatalStartup(const char* reason){
  Serial.printf("LEV,FATAL,%s\n",reason?reason:"UNKNOWN");
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
  if(otaPendingVerify){
    const esp_err_t result=esp_ota_mark_app_invalid_rollback_and_reboot();
    Serial.printf("LEV,OTA,SELFTEST,FATAL_ROLLBACK,err=%d\n",int(result));
    ESP.restart();
  }
#endif
  while(true)delay(1000);
}

void serviceWiFi(uint32_t now){
  const bool connected=WiFi.status()==WL_CONNECTED;
  if(connected&&!wifiWasConnected){Serial.printf("LEV,WIFI,CONNECTED,IP=%s,RSSI=%d\n",WiFi.localIP().toString().c_str(),WiFi.RSSI());configTime(long(runtimeSettings.timezoneOffsetMin)*60L,0,"pool.ntp.org","time.google.com");clockSynced=false;}
  else if(!connected&&wifiWasConnected){Serial.println("LEV,WIFI,LOST");resetGeminiOffline(now);}
  wifiWasConnected=connected;
  if(!connected&&runtimeSettings.wifiConfigured()&&now-wifiRetryAt>=5000){wifiRetryAt=now;WiFi.begin(runtimeSettings.wifiSsid,runtimeSettings.wifiPassword);Serial.println("LEV,WIFI,RETRY");}
  if(!connected&&!settingsDashboard.apStarted()&&int32_t(now-setupApAt)>=0)settingsDashboard.startSetupAp();
}

void serviceClock(uint32_t now){
  if(WiFi.status()!=WL_CONNECTED||now-lastClockSyncAt<1000)return;
  lastClockSyncAt=now;
  time_t t=time(nullptr);if(t<1700000000)return;
  RoboDeskTm tmv;if(!localtime_r(&t,&tmv))return;
  robot.setClockContext(uint16_t(tmv.tm_hour*60+tmv.tm_min),uint8_t(tmv.tm_wday),true);
  int64_t localSec=int64_t(t)+int64_t(runtimeSettings.timezoneOffsetMin)*60;
  if(localSec<0)localSec=0;
  localMinuteOfDay=uint16_t((uint64_t(localSec)%86400u)/60u);
  brainDayIndex=uint16_t(uint64_t(localSec)/86400u);
  if(!clockSynced){clockSynced=true;rebuildSystemPrompt();Serial.printf("LEV,CLOCK,SYNC,day=%u,time=%02d:%02d\n",unsigned(brainDayIndex),tmv.tm_hour,tmv.tm_min);}
}

void serviceBrain(uint32_t now){
  bool rising=pirState&&!pirPrevious;pirPrevious=pirState;brain.update(now,pirState,touchStable,motionAdapter.pickedUp(),brainDayIndex,runtimeSettings.proactiveVisual!=0);
  if(runtimeSettings.proactiveVoice&&!sonicQuietNow()&&rising&&geminiReady&&!utteranceActive&&!pendingReady&&speakerSize()==0&&voice.state()==livingeyes::VoiceAiState::Ready&&uint32_t(now-proactiveVoiceAt)>=300000){
    proactiveVoiceAt=now;char msg[280];snprintf(msg,sizeof(msg),"[A person has just appeared near you. If socially appropriate, give one short natural greeting as %s. Do not mention this metadata.]",runtimeSettings.robotName);if(gemini.sendClientTextTurn(msg)){voice.thinking(now);applyVoiceState(voice.state());Serial.println("LEV,BRAIN,PROACTIVE_GREETING");}
  }
  const bool safe=!utteranceActive&&speakerSize()==0&&voice.state()!=livingeyes::VoiceAiState::Speaking&&voice.state()!=livingeyes::VoiceAiState::Thinking;
  if(safe&&brain.persistenceDue(now)){if(brain.save(now))Serial.printf("LEV,BRAIN,SAVE,mem=%u,mood=%s\n",brain.memory().count(),brain.mind().moodName());else Serial.println("LEV,BRAIN,SAVE_FAIL");}
}

void serviceGemini(uint32_t now){
  if(WiFi.status()!=WL_CONNECTED||!runtimeSettings.geminiConfigured()){
    if(gemini.connecting()||gemini.connected()||geminiReady)resetGeminiOffline(now);
    return;
  }
  if(!gemini.connected()&&gemini.reconnectDue(now)){
    voice.connecting(now);applyVoiceState(voice.state());
    GeminiLiveDirectClient::Config c;
    c.apiKey=runtimeSettings.geminiApiKey;c.model=runtimeSettings.geminiModel;c.voice=runtimeSettings.geminiVoice;c.languageCode=GEMINI_LANGUAGE_CODE;c.systemPrompt=runtimeSystemPrompt;
    c.insecureTls=bool(GEMINI_TLS_INSECURE);c.rootCaPem=GOOGLE_GTS_ROOT_R1;c.resumeHandle=geminiSessionHandle;c.toolsJson=brain.toolsJson();
    Serial.println("LEV,GEMINI,CONNECTING");
    if(!gemini.connectAsync(c)){voice.disconnected(now);applyVoiceState(voice.state());Serial.println("LEV,GEMINI,CONNECT_TASK_FAIL");}
  }
  if(gemini.connecting())return; // Worker has sole ownership of TLS until finished.
  if(!gemini.connected()&&voice.state()==livingeyes::VoiceAiState::Connecting){
    resetGeminiOffline(millis());Serial.println("LEV,GEMINI,OFFLINE");return;
  }
  // Gemini can generate audio faster than real time. Stop consuming TCP while
  // the speaker queue is high; TCP flow control becomes our lossless pacing layer.
  size_t budget=0;
  if(gemini.connected()){
    const size_t free=speakerFree();
    if(sonicPlaybackActive&&sonicActivePriority==livingeyes::SonicPriority::Safety) budget=0;
    else if(geminiSpeechPriming){
      // While priming, finish the next JSON/audio envelope quickly. The parser
      // streams decoded PCM into speakerRing, so leave ample ring headroom.
      if(free>=16384) budget=8192;
      else if(free>=8192) budget=4096;
      else if(free>=4096) budget=1024;
    } else if(voice.state()==livingeyes::VoiceAiState::Speaking){
      // PCM needs ~64 kB/s on the wire after base64. A 2 kB budget per
      // rendered loop could fall below that rate; refill faster when low.
      if(free>=16384) budget=8192;
      else if(free>=8192) budget=4096;
      else if(free>=4096) budget=2048;
      else if(free>=2048) budget=1024;
    } else {
      if(free>=4096) budget=2048; else if(free>=2048) budget=512;
    }
  }
  gemini.service(now,budget);
  if(!gemini.connected()&&geminiReady){resetGeminiOffline(millis());Serial.println("LEV,GEMINI,DISCONNECTED");}
}

void serviceWakeWord(uint32_t now){
  if(runtimeSettings.inputMode!=RuntimeSettings::WakeWord)return;
  if(!wakeWord.available())return;

  if(wakeWord.running()){
    if(!wakeWord.consumeDetection())return;
    if(!wakeWord.disarmToManual(MicI2S)){
      Serial.println("LEV,WAKE,DISARM_FAIL");
      return;
    }
    resumeMicCaptureTask();
    wakeLastTriggerAt=now;
    extendWakeWindow(now);
    preRoll.clear();
    vadVoiceFrames=vadSilenceFrames=0;
    noiseFloor=120.0f;noiseCalibrated=false;noiseWindowFrames=0;
    // Small switch-over guard keeps the final WakeNet DMA tail out of Gemini VAD.
    captureGuardUntil=now+80u;
    brain.onWake(now);
    requestSonic(livingeyes::SonicCue::WakeAck,livingeyes::SonicPriority::Wake,1.f,now,true);
    if(geminiReady){voice.ready(now);applyVoiceState(voice.state());}
    Serial.printf("LEV,WAKE,DETECTED,label=%s,count=%lu,followupMs=%u\n",wakeWord.label(),(unsigned long)wakeWord.detections(),unsigned(runtimeSettings.wakeFollowupMs));
    return;
  }

  if(!wakeWindowOpen)return;
  if(int32_t(now-wakeWindowUntil)<0)return;
  if(utteranceActive||pendingReady||speakerSize()!=0)return;
  if(voice.state()==livingeyes::VoiceAiState::Speaking||voice.state()==livingeyes::VoiceAiState::Thinking)return;

  wakeWindowOpen=false;
  preRoll.clear();
  vadVoiceFrames=vadSilenceFrames=0;
  pauseMicCaptureTask();
  if(wakeWord.arm(MicI2S)){
    Serial.printf("LEV,WAKE,ARMED,label=%s\n",wakeWord.label());
  }else{
    resumeMicCaptureTask();
    Serial.printf("LEV,WAKE,ARM_FAIL,reason=%s\n",wakeWord.availabilityReason());
  }
}

void sendContext(){
  char body[900],voiceDirection[360],msg[1320];brain.buildTurnContext(body,sizeof(body),motionAdapter.pickedUp(),touchStable,pirState,envTemp,envHumidity,envPressure,lastG,lastGyro,uint8_t(robot.activity()));
  livingeyes::EmotionalSpeechProfile::build(voiceDirection,sizeof(voiceDirection),brain.mind().state(),brain.mind().moodName(),sonicQuietNow());
  snprintf(msg,sizeof(msg),"%s [%s]",body,voiceDirection);gemini.sendClientTextTurn(msg,false);
}

// ---------------- MPU HELPERS ----------------
bool mpuWrite(uint8_t reg,uint8_t value){Wire.beginTransmission(MPU_ADDR);Wire.write(reg);Wire.write(value);return Wire.endTransmission()==0;}
bool mpuRead(uint8_t reg,uint8_t* data,uint8_t count){Wire.beginTransmission(MPU_ADDR);Wire.write(reg);if(Wire.endTransmission(false)!=0)return false;if(Wire.requestFrom(MPU_ADDR,count)!=count)return false;for(uint8_t i=0;i<count;i++)data[i]=Wire.read();return true;}
int16_t be16(uint8_t hi,uint8_t lo){return int16_t((uint16_t(hi)<<8)|uint16_t(lo));}

void beginMpu(){
  uint8_t who=0;mpuRead(0x75,&who,1);Serial.printf("LEV,MPU,WHO=0x%02X\n",who);
  mpuWrite(0x6B,0x01);delay(30);mpuWrite(0x1A,0x03);mpuWrite(0x19,9);mpuWrite(0x1B,0x00);mpuWrite(0x1C,0x00);mpuWrite(0x38,0x01);
  pinMode(PIN_MPU_INT,INPUT);attachInterrupt(digitalPinToInterrupt(PIN_MPU_INT),onMPU,RISING);
}

void serviceSensors(uint32_t now){
  static uint32_t lastImu=0,lastEnv=0;
  bool raw=digitalRead(PIN_TOUCH)==HIGH;
  if(raw!=touchRaw){touchRaw=raw;touchRawChangedAt=now;}
  if(raw!=touchStable && now-touchRawChangedAt>=TOUCH_DEBOUNCE_MS){touchStable=raw;}
  touchAdapter.update(touchStable,1.0f,now,livingeyes::TouchZone::Head);
  pirState=digitalRead(PIN_PIR)==HIGH;

  if(now-lastImu>=IMU_PERIOD_MS){
    lastImu=now;uint8_t r[14];
    if(mpuRead(0x3B,r,sizeof(r))){
      float ax=be16(r[0],r[1])/16384.f, ay=be16(r[2],r[3])/16384.f, az=be16(r[4],r[5])/16384.f;
      constexpr float D2R=.017453292519943295f;
      float gx=be16(r[8],r[9])/131.f*D2R,gy=be16(r[10],r[11])/131.f*D2R,gz=be16(r[12],r[13])/131.f*D2R;
      lastG=sqrtf(ax*ax+ay*ay+az*az);lastGyro=sqrtf(gx*gx+gy*gy+gz*gz);
      livingeyes::ImuSample s;s.ax=ax;s.ay=ay;s.az=az;s.gx=gx;s.gy=gy;s.gz=gz;s.nowMs=now;motionAdapter.update(s);
    }
  }
  if(now-lastEnv>=ENV_PERIOD_MS&&!utteranceActive&&voice.state()!=livingeyes::VoiceAiState::Speaking&&voice.state()!=livingeyes::VoiceAiState::Thinking){
    lastEnv=now;
    if(ahtOK){sensors_event_t h,t;aht.getEvent(&h,&t);envTemp=t.temperature;envHumidity=h.relative_humidity;}
    if(bmpOK)envPressure=bmp.readPressure()/100.f;
  }
}

bool captureMicFrame(MicCapturedFrame& frame){
  // Preserve a partial I2S read; padding it would insert artificial silence.
  static MicRawFrameAssembler assembly;
  if(micFrameResetRequested.exchange(false))assembly.reset();
  const bool complete=assembly.read(reinterpret_cast<uint8_t*>(micRaw),sizeof(micRaw),
    [](uint8_t* out,size_t n){return MicI2S.readBytes(reinterpret_cast<char*>(out),n);},
    [](){return millis();});
  if(micFrameResetRequested.exchange(false)){assembly.reset();return false;}
  if(!complete)return false;
  const size_t frames=MIC_FRAME_SAMPLES;
  frame.capturedAt=millis();frame.clipped=0;frame.rms=0;
  if(!frames)return false;

  // Probe both stereo slots once; INMP441 L/R=GND should be left, but verify on-board.
  double eL=0.0,eR=0.0;
  for(size_t i=0;i<frames;i++){
    const float l=float(micRaw[i*2]>>16), r=float(micRaw[i*2+1]>>16);
    eL+=double(l)*double(l);eR+=double(r)*double(r);
  }
  const float rmsL=float(sqrt(eL/frames)),rmsR=float(sqrt(eR/frames));
  micSlotEnergyL=micSlotEnergyL*.90f+rmsL*.10f;micSlotEnergyR=micSlotEnergyR*.90f+rmsR*.10f;
  if(micSlotProbeFrames<20)++micSlotProbeFrames;
  if(micSelectedSlot<0&&micSlotProbeFrames>=8){
    micSelectedSlot=(micSlotEnergyR>micSlotEnergyL)?1:0;micDc=0.0f;
    Serial.printf("LEV,AUDIO,MIC_SLOT,left=%.1f,right=%.1f,selected=%c\n",micSlotEnergyL,micSlotEnergyR,micSelectedSlot?'R':'L');
  } else if(ROBODESK_MIC_SLOT<0&&micSelectedSlot>=0){
    const float selected=micSelectedSlot?micSlotEnergyR:micSlotEnergyL,other=micSelectedSlot?micSlotEnergyL:micSlotEnergyR;
    if(other>fmaxf(80.0f,selected*4.0f)){if(++micSlotSwitchVotes>=25){micSelectedSlot=micSelectedSlot?0:1;micSlotSwitchVotes=0;micDc=0.0f;Serial.printf("LEV,AUDIO,MIC_SLOT_SWITCH,left=%.1f,right=%.1f,selected=%c\n",micSlotEnergyL,micSlotEnergyR,micSelectedSlot?'R':'L');}} else micSlotSwitchVotes=0;
  }
  const size_t slot=(micSelectedSlot==1)?1u:0u;
  double sumSq=0;bool clipped=false;
  for(size_t i=0;i<MIC_FRAME_SAMPLES;i++){
    int16_t out=0;
    if(i<frames){
      const float s24=float(micRaw[i*2+slot]>>8);
      micDc=micDc*.995f+s24*.005f;
      float centered=(s24-micDc)*MIC_DIGITAL_GAIN/256.f;
      if(centered>32767)centered=32767;if(centered<-32768)centered=-32768;
      out=int16_t(centered);if(abs(int(out))>32300)clipped=true;
    }
    frame.pcm[i]=out;sumSq+=double(out)*double(out);
  }
  frame.clipped=clipped?1:0;frame.rms=float(sqrt(sumSq/MIC_FRAME_SAMPLES));micLastRms=frame.rms;return true;
}

void micCaptureTaskMain(void*){
  uint32_t lastFrameUs=0;
  for(;;){
    if(!micOK||micTaskPaused||(runtimeSettings.inputMode==RuntimeSettings::WakeWord&&wakeWord.running())){micTaskBusy=false;vTaskDelay(pdMS_TO_TICKS(2));continue;}
    micTaskBusy=true;static MicCapturedFrame frame,stale;const uint32_t before=micros();
    const bool captured=captureMicFrame(frame);const uint32_t after=micros();
    if(lastFrameUs){const uint32_t gap=uint32_t(after-lastFrameUs);if(gap>micCaptureGapMaxUs)micCaptureGapMaxUs=gap;}lastFrameUs=after;
    if(!captured){micTaskBusy=false;vTaskDelay(pdMS_TO_TICKS(1));continue;}
    ++micCapturedFrames;
    if(xQueueSend(micQueue,&frame,0)!=pdPASS){xQueueReceive(micQueue,&stale,0);xQueueSend(micQueue,&frame,0);++micCaptureDrops;}
    const UBaseType_t q=uxQueueMessagesWaiting(micQueue);if(q>micQueueHighWater)micQueueHighWater=uint8_t(q>255?255:q);
    (void)before;
  }
}

bool flushMicTxBatch(){
  if(!micTxBatchLen)return true;
  const uint32_t before=micros();const bool sent=gemini.sendAudio(micTxBatch,micTxBatchLen);
  const uint32_t elapsed=uint32_t(micros()-before);if(elapsed>micTxMaxUs)micTxMaxUs=elapsed;
  if(!sent){++micTxFailures;resetGeminiOffline(millis());return false;}
  voice.noteTxAudio(micTxBatchLen);micFramesSent+=uint32_t((micTxBatchLen+MIC_PCM_BYTES-1)/MIC_PCM_BYTES);micTxBatchLen=0;return true;
}

bool appendMicTx(const uint8_t*data,size_t len){
  while(len){
    const size_t room=MIC_TX_BATCH_BYTES-micTxBatchLen,k=len<room?len:room;
    memcpy(micTxBatch+micTxBatchLen,data,k);micTxBatchLen+=k;data+=k;len-=k;
    if(micTxBatchLen==MIC_TX_BATCH_BYTES&&!flushMicTxBatch())return false;
  }
  return true;
}

void sendPreRoll(){
  uint8_t tmp[MIC_PCM_BYTES];
  while(preRoll.size()){
    size_t n=preRoll.read(tmp,sizeof(tmp));if(!n||!appendMicTx(tmp,n))break;
  }
}

void startUtterance(uint32_t now,bool alreadyStreaming){
  if(!gemini.connected()||!geminiReady||utteranceActive)return;
  utteranceActive=true;utteranceStartedAt=now;utterancePcmFrames=0;vadSilenceFrames=0;vadVoiceFrames=0;extendWakeWindow(now);
  brain.onConversationStart(now,brainDayIndex);sendContext();
  if(!gemini.connected()){resetGeminiOffline(millis());return;}
  voice.listening(now);applyVoiceState(voice.state());
  // AlwaysListening already streams continuously to server VAD. Touch/wake modes
  // begin at local detection and therefore prepend the bounded 200 ms pre-roll.
  if(!alreadyStreaming)sendPreRoll();
  Serial.printf("LEV,VAD,START,rmsFloor=%.1f,hybrid=1\n",noiseFloor);
}

void endUtterance(uint32_t now){
  if(!utteranceActive)return;
  utteranceActive=false;vadSilenceFrames=vadVoiceFrames=0;preRoll.clear();if(!flushMicTxBatch())return;
  if(runtimeSettings.inputMode!=RuntimeSettings::AlwaysListening&&!gemini.sendAudioStreamEnd()){
    ++micTxFailures;resetGeminiOffline(millis());
    Serial.println("LEV,VAD,END_SEND_FAIL");return;
  }
  ++localVadEnds;voice.thinking(now);applyVoiceState(voice.state());
  Serial.printf("LEV,VAD,END,durMs=%lu,pcmFrames=%lu,floor=%.1f,hybrid=1\n",(unsigned long)uint32_t(now-utteranceStartedAt),(unsigned long)utterancePcmFrames,noiseFloor);
}

void processMicFrame(const MicCapturedFrame& frame){
  const uint32_t now=millis();const float rms=frame.rms;
  if(uint32_t(now-frame.capturedAt)>120u){preRoll.clear();micTxBatchLen=0;return;}
  if(captureGuardActive(now)){
    preRoll.clear();vadVoiceFrames=vadSilenceFrames=0;noiseWindowFrames=0;micTxBatchLen=0;return;
  }
  if(!utteranceActive&&speakerSize()==0&&!speakerGeminiActive)updateNoiseEstimator(rms);

  const bool alwaysStreaming=runtimeSettings.inputMode==RuntimeSettings::AlwaysListening;
  const auto state=voice.state();
  const bool streamState=voice.captureAllowed()||(alwaysStreaming&&(state==livingeyes::VoiceAiState::Thinking||state==livingeyes::VoiceAiState::Interrupted));
  bool capture=geminiReady&&gemini.connected()&&streamState&&!serialAudioTestActive;
  if(runtimeSettings.inputMode==RuntimeSettings::WakeWord&&!wakeWindowActive(now)&&!utteranceActive)capture=false;
  if(runtimeSettings.inputMode==RuntimeSettings::TouchToTalk&&!touchStable&&!utteranceActive)capture=false;
#if VOICE_ENABLE_EXPERIMENTAL_BARGE_IN
  if(voice.state()==livingeyes::VoiceAiState::Speaking&&rms>fmaxf(5000.f,noiseFloor*8.f)){speakerClear();pendingReady=false;startUtterance(now,false);return;}
#endif
  if(!capture){preRoll.clear();micTxBatchLen=0;return;}
  if(frame.clipped)++micClips;
  if(alwaysStreaming){
    // Continuous 40 ms stream gives Gemini server-side VAD the same clean timing
    // pattern as the laptop backend. Silence is not locally gated out.
    if(!appendMicTx(reinterpret_cast<const uint8_t*>(frame.pcm),MIC_PCM_BYTES))return;
  }

  if(!utteranceActive){
    if(!alwaysStreaming){if(preRoll.free()<MIC_PCM_BYTES)preRoll.discard(MIC_PCM_BYTES);preRoll.write(reinterpret_cast<const uint8_t*>(frame.pcm),MIC_PCM_BYTES);}
    if(!noiseCalibrated){vadVoiceFrames=0;return;}
    const float configured=runtimeSettings.vadStartMultiplier();
    const float start=fmaxf(VAD_ABS_START,noiseFloor*fminf(configured,2.6f));
    if(rms>=start){if(++vadVoiceFrames>=VAD_START_FRAMES)startUtterance(now,alwaysStreaming);}else vadVoiceFrames=0;
    return;
  }

  if(!alwaysStreaming&&!appendMicTx(reinterpret_cast<const uint8_t*>(frame.pcm),MIC_PCM_BYTES))return;
  ++utterancePcmFrames;
  const float effectiveEndMultiplier=fmaxf(1.45f,fminf(runtimeSettings.vadEndMultiplier(),2.2f));
  const float stop=fmaxf(VAD_ABS_END,noiseFloor*effectiveEndMultiplier);
  if(rms<stop)++vadSilenceFrames;else vadSilenceFrames=0;
  if((runtimeSettings.inputMode==RuntimeSettings::TouchToTalk&&!touchStable)||vadSilenceFrames>=runtimeSettings.vadEndFrames()||uint32_t(now-utteranceStartedAt)>=VAD_MAX_UTTERANCE_MS)endUtterance(now);
}

void serviceMicrophone(uint32_t){
  if(!micQueue)return;
  static MicCapturedFrame frame;uint8_t serviced=0;
  while(serviced<MIC_SERVICE_MAX_FRAMES&&xQueueReceive(micQueue,&frame,0)==pdPASS){processMicFrame(frame);++serviced;}
}

void serviceSonicEvents(uint32_t now){
  if(touchStable&&!sonicTouchPrev&&uint32_t(now-sonicTouchAt)>=500){sonicTouchAt=now;requestSonic(livingeyes::SonicCue::Affection,livingeyes::SonicPriority::Character,.9f,now);}
  sonicTouchPrev=touchStable;
  const bool picked=motionAdapter.pickedUp();if(picked!=sonicPickedPrev&&uint32_t(now-sonicMotionAt)>=450){sonicMotionAt=now;requestSonic(picked?livingeyes::SonicCue::Pickup:livingeyes::SonicCue::PutDown,livingeyes::SonicPriority::Character,.85f,now,true);}sonicPickedPrev=picked;
  if(lastG<motionCfg.fallG&&uint32_t(now-sonicFallAt)>=2200){sonicFallAt=now;requestSonic(livingeyes::SonicCue::Surprise,livingeyes::SonicPriority::Safety,1.f,now,true);}
  else if(lastGyro>motionCfg.shakeGyro&&uint32_t(now-sonicMotionAt)>=1100){sonicMotionAt=now;requestSonic(livingeyes::SonicCue::Shake,livingeyes::SonicPriority::Character,.9f,now);}
  if(pirState&&!sonicPirPrev){const auto&ms=brain.mind().state();requestSonic(ms.trust>.62f?livingeyes::SonicCue::Greeting:livingeyes::SonicCue::Curious,livingeyes::SonicPriority::Character,.62f,now);}sonicPirPrev=pirState;
  if(uint32_t(now-sonicIdleAt)>=15000){sonicIdleAt=now;const auto&m=brain.mind().state();if(!sonicQuietNow()&&!pirState&&!touchStable){if(m.energy<.30f)requestSonic(livingeyes::SonicCue::Sleepy,livingeyes::SonicPriority::Ambient,.42f,now);else if(m.boredom>.62f)requestSonic(livingeyes::SonicCue::Thinking,livingeyes::SonicPriority::Ambient,.38f,now);else if(m.curiosity>.74f)requestSonic(livingeyes::SonicCue::Curious,livingeyes::SonicPriority::Ambient,.34f,now);}}
}

void finishSonicPlayback(){if(sonicMouthActive){robot.speaking(false);sonicMouthActive=false;}if(sonicPlaybackActive){sonicPlaybackActive=false;++sonicFinished;Serial.printf("LEV,SONIC,DONE,cue=%s\n",livingeyes::sonicCueName(sonicActiveCue));}}

void serviceSonic(uint32_t now){
  if(!runtimeSettings.masterSound||!runtimeSettings.characterSfx){sonicSynth.reset();if(sonicPlaybackActive&&speakerSize()==0)finishSonicPlayback();return;}
  if(sonicPlaybackActive||sonicSynth.active()){
    if(sonicActivePriority!=livingeyes::SonicPriority::Safety&&(voice.state()==livingeyes::VoiceAiState::Speaking||voice.state()==livingeyes::VoiceAiState::Thinking)){sonicSynth.reset();speakerClear();++sonicPreemptedBySpeech;finishSonicPlayback();return;}
    while(sonicSynth.active()&&speakerFree()>=512){int16_t pcm[256];size_t n=sonicSynth.render(pcm,256);if(!n)break;size_t accepted=speakerWrite(reinterpret_cast<const uint8_t*>(pcm),n*sizeof(int16_t));if(accepted!=n*sizeof(int16_t))break;}
    if(!sonicSynth.active()&&speakerSize()==0)finishSonicPlayback();
    return;
  }
  livingeyes::SonicRequest req;if(!sonicDirector.peek(req))return;
  if(int32_t(sonicConversationQuietUntil-now)>0 && (req.priority==livingeyes::SonicPriority::Character || req.priority==livingeyes::SonicPriority::Ambient)){sonicDirector.pop(req);return;}
  const bool speechBusy=utteranceActive||pendingReady||voice.state()==livingeyes::VoiceAiState::Speaking||voice.state()==livingeyes::VoiceAiState::Thinking;
  if(speechBusy&&req.priority!=livingeyes::SonicPriority::Safety)return;
  if(speakerSize()!=0&&req.priority!=livingeyes::SonicPriority::Safety)return;
  sonicDirector.pop(req);
  if(req.priority==livingeyes::SonicPriority::Safety&&speakerSize()!=0){speakerClear();sonicSynth.reset();++sonicSafetyPreemptions;}
  livingeyes::SonicRecipe recipe=sonicDirector.recipe(req,brain.mind().state(),robot.personalityProfile(),now^uint32_t(req.cue));
  if(sonicQuietNow())recipe.gain*=float(runtimeSettings.quietGainX100)/100.f;
  if(recipe.gain<.015f)return;
  if(!sonicSynth.start(recipe,SPEAKER_RATE))return;
  sonicPlaybackActive=true;sonicActiveCue=req.cue;sonicActivePriority=req.priority;++sonicStarted;
  if(recipe.mouth){sonicMouthActive=true;robot.speaking(true);robot.showMouthFor(uint32_t(recipe.durationMs)+recipe.gapMs+recipe.secondDurationMs+120u);}
  Serial.printf("LEV,SONIC,START,cue=%s,priority=%u,gain=%.2f,quiet=%u,mouth=%u\n",livingeyes::sonicCueName(req.cue),unsigned(req.priority),recipe.gain,unsigned(sonicQuietNow()),unsigned(recipe.mouth));
}

void serviceSpeaker(uint32_t now){
  if(!speakerOK)return;
  if(geminiSpeechPriming){
    const size_t queued=speakerSize();
    const bool force=pendingReady&&queued>=2;
    if(queued>=GEMINI_SPEECH_PRIME_BYTES||force)startGeminiSpeechPlayback(now,force&&queued<GEMINI_SPEECH_PRIME_BYTES);
  }
  const float level=float(speakerLevelQ15)/32767.f;
  voiceFace.playbackLevel(level);
  if(sonicMouthActive)robot.audioLevel(level);
  if(speakerDrainSignal||pendingReady){
    speakerDrainSignal=false;
    if(speakerDrained(now)){
      speakerLevelQ15=0;
      if(sonicPlaybackActive&&!sonicSynth.active())finishSonicPlayback();
      if(pendingReady)setReadyWhenAudioDrained();
    }
  }
}

// Bounded diagnostic command: validates Gemini-to-I2S playback independently
// of speech recognition, without changing saved settings.
void serviceSerialAudioTest(uint32_t now){
  static char command[24];static uint8_t length=0;static bool overflow=false;
  for(uint8_t i=0;i<32&&Serial.available();++i){
    const char ch=char(Serial.read());if(ch=='\r')continue;
    if(ch!='\n'){if(length+1<sizeof(command))command[length++]=ch;else overflow=true;continue;}
    command[length]=0;
    if(!overflow&&!strcmp(command,"audio_test")){
      if(geminiReady&&gemini.connected()&&speakerDrained(now)){
        utteranceActive=false;vadVoiceFrames=vadSilenceFrames=0;micTxBatchLen=0;preRoll.clear();
        const bool sent=gemini.sendClientTextTurn("Uji speaker. Ucapkan tepat satu kalimat ini dengan kecepatan normal: Halo, saya RoboDesk. Satu, dua, tiga, empat, lima.");
        if(sent){serialAudioTestActive=true;voice.ready(now);voice.thinking(now);applyVoiceState(voice.state());}
        Serial.printf("LEV,AUDIO,TEST,sent=%u\n",unsigned(sent));
      }else Serial.println("LEV,AUDIO,TEST,not_ready");
    }
    length=0;overflow=false;
  }
}

void setup(){
  Serial.begin(115200);delay(800);Serial.println("\n=== RoboDesk v0.15.1 realtime-audio fix4 + Sonic Character + Eye Acting + Living Character + Direct Gemini ===");
  beginOtaBootSelfTest();
  RuntimeSettings defaults;
  RuntimeSettings::copy(defaults.wifiSsid,sizeof(defaults.wifiSsid),WIFI_SSID);
  RuntimeSettings::copy(defaults.wifiPassword,sizeof(defaults.wifiPassword),WIFI_PASSWORD);
  RuntimeSettings::copy(defaults.geminiApiKey,sizeof(defaults.geminiApiKey),GEMINI_API_KEY);
  RuntimeSettings::copy(defaults.geminiModel,sizeof(defaults.geminiModel),GEMINI_MODEL);
  RuntimeSettings::copy(defaults.geminiVoice,sizeof(defaults.geminiVoice),GEMINI_VOICE);
  RuntimeSettings::copy(defaults.adminPin,sizeof(defaults.adminPin),DASHBOARD_ADMIN_PIN);
  Serial.println("LEV,BOOT,SETTINGS_LOAD_BEGIN");
  const bool loaded=settingsStore.load(runtimeSettings,defaults);
  if(strstr(runtimeSettings.speechStyle,"slightly slower-than-normal")||strstr(runtimeSettings.speechStyle,"slower-than-normal")){
    RuntimeSettings::copy(runtimeSettings.speechStyle,sizeof(runtimeSettings.speechStyle),defaults.speechStyle);
    if(loaded)settingsStore.save(runtimeSettings);
    Serial.println("LEV,CFG,SPEECH_STYLE_MIGRATED,pace=normal");
  }
  Serial.println("LEV,BOOT,SETTINGS_LOAD_DONE");
  applySonicSettings();
  Serial.println("LEV,BOOT,SONIC_SETTINGS_DONE");
  settingsDashboard.begin(&runtimeSettings,&settingsStore,&brain,SETUP_AP_PASSWORD,dashboardSoundTest,nullptr,dashboardFirmwareUpdateControl,nullptr);
  Serial.printf("LEV,CFG,source=%s,mode=%s,gain=%.2f,guard=%u,vadStart=%.2f,vadEnd=%.2f,endMs=%u\n",
    loaded?"NVS":"defaults",inputModeName(),runtimeSettings.speakerGain(),unsigned(runtimeSettings.postSpeakGuardMs),runtimeSettings.vadStartMultiplier(),runtimeSettings.vadEndMultiplier(),unsigned(runtimeSettings.vadEndMs));
  if(!runtimeSettings.wifiConfigured()||!runtimeSettings.geminiConfigured())Serial.println("LEV,CFG,SETUP_REQUIRED,use http://192.168.4.1/ after RoboDesk setup AP appears");
  Wire.begin(PIN_SDA,PIN_SCL);Wire.setClock(400000);
  if(!oled.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR,true,false))fatalStartup("OLED");
  bool c=livingeyes::core_performances::installCorePerformances(coreBank),e=livingeyes::extended_performances::installExtendedPerformances(extendedBank);
  if(!c||!e)fatalStartup("PERFORMANCE_BANK");
  robot.attach(&coreBank);robot.addPerformanceBank(&extendedBank);robot.attach(&choreography);robot.attach(&relationships);robot.attach(&life);
  robot.useCoreBehaviorPerformanceTags();robot.useCoreInteractionPerformanceTags();robot.useExtendedInteractionPerformanceTags();robot.begin(millis());applyFaceSettings();robot.enableAutonomousBehavior(true);
  Serial.printf("LEV,FACE,life=%u,pupil=%u,brow=%u,lash=%u,autoBrow=%u,autoLash=%u,mouth=%u,micro=%u,pupilLess=%u,silhouette=%u,gazeReach=%u\n",unsigned(runtimeSettings.faceLifeEnabled),unsigned(runtimeSettings.facePupils),unsigned(runtimeSettings.faceBrows),unsigned(runtimeSettings.faceLashes),unsigned(runtimeSettings.faceAutoBrows),unsigned(runtimeSettings.faceAutoLashes),unsigned(runtimeSettings.mouthMode),unsigned(runtimeSettings.faceMicroX100),unsigned(runtimeSettings.facePupilLessActing),unsigned(runtimeSettings.faceSilhouetteX100),unsigned(runtimeSettings.faceGazeReachX100));
  Serial.printf("LEV,SONIC,CFG,master=%u,speech=%u,sfx=%u,intensity=%u,freq=%u,wake=%u,touch=%u,motion=%u,notif=%u,quiet=%u,start=%u,end=%u,quietGain=%u\n",unsigned(runtimeSettings.masterSound),unsigned(runtimeSettings.speechEnabled),unsigned(runtimeSettings.characterSfx),unsigned(runtimeSettings.sfxIntensityX100),unsigned(runtimeSettings.sonicFrequency),unsigned(runtimeSettings.wakeSfx),unsigned(runtimeSettings.touchSfx),unsigned(runtimeSettings.motionSfx),unsigned(runtimeSettings.notificationSfx),unsigned(runtimeSettings.quietHoursEnabled),unsigned(runtimeSettings.quietStartMin),unsigned(runtimeSettings.quietEndMin),unsigned(runtimeSettings.quietGainX100));
  brain.begin(&robot,&relationships,millis());brain.setMemoryEnabled(runtimeSettings.memoryEnabled!=0);rebuildSystemPrompt();
  Serial.printf("LEV,BRAIN,LOAD,restored=%u,mem=%u,rel=%u,social=%u,evoDays=%u\n",unsigned(brain.restored()),brain.memory().count(),brain.relationshipCount(),brain.longSocialCount(),unsigned(brain.evolutionDays()));
  voice.reset(millis());applyVoiceState(voice.state());
  beginMpu();pinMode(PIN_TOUCH,INPUT);pinMode(PIN_PIR,INPUT);touchRaw=touchStable=digitalRead(PIN_TOUCH)==HIGH;touchRawChangedAt=millis();pirState=digitalRead(PIN_PIR)==HIGH;
  ahtOK=aht.begin(&Wire);bmpOK=bmp.begin(BMP_ADDR);Serial.printf("LEV,SENSORS,AHT=%u,BMP=%u\n",ahtOK,bmpOK);

  MicI2S.setPins(PIN_MIC_BCLK,PIN_MIC_WS,-1,PIN_MIC_DATA);
  micOK=MicI2S.begin(I2S_MODE_STD,MIC_RATE,I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO);
  speakerOK=SpeakerI2S.begin(I2S_NUM_1,SPEAKER_RATE,PIN_SPK_BCLK,PIN_SPK_LRC,PIN_SPK_DIN);
  speakerRingMutex=xSemaphoreCreateMutexStatic(&speakerRingMutexStorage);
  micQueue=xQueueCreateStatic(MIC_QUEUE_CAP,sizeof(MicCapturedFrame),micQueueStorage,&micQueueStruct);
  BaseType_t speakerTaskRc=pdFAIL,micTaskRc=pdFAIL;
  if(speakerOK&&speakerRingMutex)speakerTaskRc=xTaskCreatePinnedToCore(speakerTaskMain,"rdSpeaker",3072,nullptr,5,&speakerTaskHandle,1);
  if(micOK&&micQueue)micTaskRc=xTaskCreatePinnedToCore(micCaptureTaskMain,"rdMic",4096,nullptr,4,&micTaskHandle,1);
  if(speakerTaskRc!=pdPASS){speakerTaskHandle=nullptr;speakerOK=false;Serial.println("LEV,AUDIO,SPK_TASK_FAIL");}
  else Serial.printf("LEV,AUDIO,SPK_TASK_OK,core=1,prio=5,dmaDesc=%u,dmaFrames=%u\n",unsigned(RoboDeskNativeSpeakerI2S::dmaDescriptors()),unsigned(RoboDeskNativeSpeakerI2S::dmaFrames()));
  if(micTaskRc!=pdPASS){micTaskHandle=nullptr;micOK=false;Serial.println("LEV,AUDIO,MIC_TASK_FAIL");}
  else Serial.printf("LEV,AUDIO,MIC_TASK_OK,core=1,prio=4,q=%u,frameMs=20\n",unsigned(MIC_QUEUE_CAP));
  Serial.printf("LEV,AUDIO,MIC=%u,SPK=%u,micPort=%d,spkPort=%d,speakerGain=%.2f\n",micOK,speakerOK,int(MicI2S.getPort()),SpeakerI2S.port(),runtimeSettings.speakerGain());
  Serial.printf("LEV,AUDIO,MIC_FORMAT,rate=16000,pcmBits=16,channels=1,slotOverride=%d\n",ROBODESK_MIC_SLOT);
  if(!micOK||!speakerOK){voice.fault(millis());applyVoiceState(voice.state());}
  Serial.printf("LEV,WAKE,PREPARED,compiled=%u,available=%u,label=%s,reason=%s\n",unsigned(wakeWord.compiled()),unsigned(wakeWord.available()),wakeWord.label(),wakeWord.availabilityReason());
  if(runtimeSettings.inputMode==RuntimeSettings::WakeWord){
    pauseMicCaptureTask();
    if(micOK && ESP.getPsramSize()>0 && wakeWord.available() && wakeWord.arm(MicI2S)){
      Serial.printf("LEV,WAKE,ARMED,label=%s\n",wakeWord.label());
    }else{
      Serial.printf("LEV,WAKE,FALLBACK_ALWAYS,reason=%s,psram=%u\n",ESP.getPsramSize()?wakeWord.availabilityReason():"psram_unavailable",unsigned(ESP.getPsramSize()));
      runtimeSettings.inputMode=RuntimeSettings::AlwaysListening;resumeMicCaptureTask();
    }
  }
  beginWiFi();
  settingsDashboard.startHttp();
  statsAt=millis();
}

void loop(){
  uint32_t now=millis();
  const bool dashboardSafe = firmwareUpdateActive || (!utteranceActive && speakerSize()==0 && voice.state()!=livingeyes::VoiceAiState::Speaking && voice.state()!=livingeyes::VoiceAiState::Thinking);
  settingsDashboard.service(now,dashboardSafe);
  if(firmwareUpdateActive){serviceOtaBootSelfTest(now);delay(1);return;}
  serviceOtaBootSelfTest(now);
  serviceWiFi(now);
  serviceGemini(now);
  const bool responseStalled=(voice.state()==livingeyes::VoiceAiState::Thinking&&uint32_t(millis()-voice.changedAt())>=15000u)||(voice.state()==livingeyes::VoiceAiState::Speaking&&uint32_t(millis()-lastAudioRxAt)>=15000u);
  if(geminiReady&&responseStalled&&speakerDrained(millis())){
    Serial.println("LEV,AUDIO,RESPONSE_TIMEOUT,disconnect=1");resetGeminiOffline(millis());
  }
  serviceSerialAudioTest(millis());
  serviceGeminiTools();
  serviceSensors(now);
  serviceClock(now);
  serviceBrain(now);
  serviceWakeWord(now);
  serviceSonicEvents(now);
  serviceSonic(now);
  serviceSpeaker(now); // prioritize playback; mic DMA can be drained immediately after
  if(micOK)serviceMicrophone(now); // drains bounded capture queue; MicI2S itself is owned by rdMic task
  robot.update(now);
  static uint32_t nextFrameUs=0;uint32_t us=micros();
  const bool audioNeedsService=(micQueue&&uxQueueMessagesWaiting(micQueue)>=4)||(speakerGeminiActive&&speakerSize()<GEMINI_SPEECH_PRIME_BYTES);
  if(!audioNeedsService&&int32_t(us-nextFrameUs)>=0){robot.renderAdaptive();nextFrameUs=us+FRAME_PERIOD_US;}

  if(now-statsAt>=5000){
    statsAt=now;auto d=robot.diagnostics();const auto& vm=voice.metrics();
    Serial.printf("LEV,AUDIO,FLOW,captured=%lu,sent=%lu,txMaxUs=%lu\n",(unsigned long)micCapturedFrames,(unsigned long)micFramesSent,(unsigned long)micTxMaxUs);
    Serial.printf("LEV,STAT,state=%s,mode=%s,wifi=%d,wss=%u,ready=%u,q=%u,drop=%lu,exp=%lu,rej=%lu,spkBuf=%u,spkDrop=%lu,directOverflow=%lu,tx=%lu,rx=%lu,utter=%lu,replies=%lu,micFrames=%lu,spkFrames=%lu,clip=%lu,underrun=%lu,starve=%lu,spkGapMaxUs=%lu,spkWriteMaxUs=%lu,prime=%u,primeStarts=%lu,primeForced=%lu,spkTask=%u,micTask=%u,micQ=%u,micQHi=%u,micDrop=%lu,micGapMaxUs=%lu,txFail=%lu,svVad=%lu,locVad=%lu,noiseCal=%u,guard=%u,noise=%.1f,g=%.2f,gyro=%.2f,touch=%u,pir=%u,mouth=%u,eyeSpace=%.2f,eyeScale=%.2f,pupilScale=%.2f,heap=%u\n",
      livingeyes::voiceAiStateName(voice.state()),inputModeName(),WiFi.RSSI(),gemini.connected(),geminiReady,d.queuedJobs,(unsigned long)d.character.dropped,(unsigned long)d.character.expired,(unsigned long)d.character.rejected,
      unsigned(speakerSize()),(unsigned long)speakerDropped(),(unsigned long)geminiAudioOverflow,(unsigned long)vm.txAudioBytes,(unsigned long)vm.rxAudioBytes,(unsigned long)vm.utterances,(unsigned long)vm.replies,
      (unsigned long)micFramesSent,(unsigned long)speakerFramesPlayed,(unsigned long)micClips,(unsigned long)speakerUnderruns,(unsigned long)speakerStarves,(unsigned long)speakerWriteGapMaxUs,(unsigned long)speakerWriteTimeMaxUs,unsigned(geminiSpeechPriming),(unsigned long)geminiSpeechPrimeStarts,(unsigned long)geminiSpeechPrimeForced,unsigned(speakerTaskBusy),unsigned(micTaskBusy),unsigned(micQueue?uxQueueMessagesWaiting(micQueue):0),unsigned(micQueueHighWater),(unsigned long)micCaptureDrops,(unsigned long)micCaptureGapMaxUs,(unsigned long)micTxFailures,(unsigned long)serverVadFinals,(unsigned long)localVadEnds,unsigned(noiseCalibrated),unsigned(captureGuardActive(now)),noiseFloor,lastG,lastGyro,touchStable,pirState,unsigned(d.mouthVisible),d.faceSpacing,d.faceEyeScale,d.facePupilScale,unsigned(ESP.getFreeHeap()));
    Serial.printf("LEV,WAKESTAT,compiled=%u,available=%u,armed=%u,window=%u,triggers=%lu,beginFail=%lu,switchFail=%lu,lastTrigger=%lu\n",
      unsigned(wakeWord.compiled()),unsigned(wakeWord.available()),unsigned(wakeWord.running()),unsigned(wakeWindowActive(now)),
      (unsigned long)wakeWord.detections(),(unsigned long)wakeWord.beginFailures(),(unsigned long)wakeWord.modeSwitchFailures(),(unsigned long)wakeLastTriggerAt);
    Serial.printf("LEV,SONICSTAT,active=%u,cue=%s,q=%u,started=%lu,finished=%lu,accepted=%lu,rejected=%lu,suppressed=%lu,speechPreempt=%lu,safetyPreempt=%lu,mutedSpeech=%lu,quiet=%u\n",unsigned(sonicPlaybackActive),livingeyes::sonicCueName(sonicActiveCue),unsigned(sonicDirector.queued()),(unsigned long)sonicStarted,(unsigned long)sonicFinished,(unsigned long)sonicDirector.accepted(),(unsigned long)sonicDirector.rejected(),(unsigned long)sonicDirector.suppressed(),(unsigned long)sonicPreemptedBySpeech,(unsigned long)sonicSafetyPreemptions,(unsigned long)sonicMutedSpeechBytes,unsigned(sonicQuietNow()));
    const auto& bs=brain.mind().state();Serial.printf("LEV,BRAINSTAT,mem=%u,rel=%u,socialMem=%u,evoDays=%u,mood=%s,energy=%.2f,curiosity=%.2f,affection=%.2f,boredom=%.2f,social=%.2f,trust=%.2f,remember=%lu,recall=%lu,forget=%lu,day=%u\n",brain.memory().count(),brain.relationshipCount(),brain.longSocialCount(),unsigned(brain.evolutionDays()),brain.mind().moodName(),bs.energy,bs.curiosity,bs.affection,bs.boredom,bs.socialNeed,bs.trust,(unsigned long)brain.remembers(),(unsigned long)brain.recalls(),(unsigned long)brain.forgets(),unsigned(brainDayIndex));
  }
  yield();
}
