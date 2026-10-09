#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_system.h>

#include "NativeSpeakerI2S.h"
#include "RuntimeSettings.h"

namespace {
constexpr int kSdaPin = 8;
constexpr int kSclPin = 9;
constexpr int kTouchHeadPin = 7;
constexpr int kTouchSidePin = 16;
constexpr int kPirPin = 15;
constexpr int kMicWsPin = 4;
constexpr int kMicBclkPin = 5;
constexpr int kMicDataPin = 6;
constexpr int kSpeakerDinPin = 11;
constexpr int kSpeakerBclkPin = 12;
constexpr int kSpeakerLrcPin = 13;

enum class Test : uint8_t { Idle, Led, Inputs, I2c, Mic, Speaker, Wifi };
Test activeTest = Test::Idle;
I2SClass mic(I2S_NUM_0);
RoboDeskNativeSpeakerI2S speaker;
RuntimeSettings diagnosticSettings;
RuntimeSettingsStore diagnosticSettingsStore;
int16_t silence[960] = {};
uint32_t lastHeartbeat = 0;
uint32_t lastSample = 0;
uint32_t lastAudioWrite = 0;
uint32_t lastLedToggle = 0;
bool ledOn = false;
char command[32];
uint8_t commandLength = 0;

const char* testName(Test test) {
  switch (test) {
    case Test::Led: return "led";
    case Test::Inputs: return "inputs";
    case Test::I2c: return "i2c";
    case Test::Mic: return "mic";
    case Test::Speaker: return "speaker_silence";
    case Test::Wifi: return "wifi_sta";
    default: return "idle";
  }
}

void stopTest() {
  if (activeTest == Test::Led) {
    digitalWrite(LED_BUILTIN, LOW);
    ledOn = false;
  }
  if (activeTest == Test::Speaker) speaker.end();
  if (activeTest == Test::Mic) mic.end();
  if (activeTest == Test::Wifi) {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
  }
  Serial.printf("DIAG,STOP,test=%s\n", testName(activeTest));
  activeTest = Test::Idle;
}

void scanI2c(uint32_t now) {
  unsigned found = 0;
  Serial.print("DIAG,I2C,SCAN");
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf(",0x%02X", address);
      ++found;
    }
  }
  Serial.printf(",count=%u,at=%lu\n", found, static_cast<unsigned long>(now));
}

void startTest(const char* name) {
  stopTest();
  const uint32_t now = millis();
  if (!strcmp(name, "inputs")) {
    pinMode(kTouchHeadPin, INPUT);
    pinMode(kTouchSidePin, INPUT);
    pinMode(kPirPin, INPUT);
    activeTest = Test::Inputs;
  } else if (!strcmp(name, "led")) {
    digitalWrite(LED_BUILTIN, LOW);
    ledOn = false;
    lastLedToggle = now - 500;
    activeTest = Test::Led;
  } else if (!strcmp(name, "i2c")) {
    Wire.begin(kSdaPin, kSclPin);
    Wire.setClock(100000);
    activeTest = Test::I2c;
    scanI2c(now);
    lastSample = now;
  } else if (!strcmp(name, "mic")) {
    mic.setPins(kMicBclkPin, kMicWsPin, -1, kMicDataPin);
    if (!mic.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_32BIT,
                   I2S_SLOT_MODE_STEREO)) {
      Serial.println("DIAG,START,test=mic,result=failed");
      return;
    }
    activeTest = Test::Mic;
  } else if (!strcmp(name, "speaker")) {
    if (!speaker.begin(I2S_NUM_1, 24000, kSpeakerBclkPin,
                       kSpeakerLrcPin, kSpeakerDinPin)) {
      Serial.println("DIAG,START,test=speaker_silence,result=failed");
      return;
    }
    activeTest = Test::Speaker;
    lastAudioWrite = now - 20;
  } else if (!strcmp(name, "wifi")) {
    diagnosticSettings.clear();
    diagnosticSettingsStore.load(diagnosticSettings, diagnosticSettings);
    const int8_t index = diagnosticSettings.findWifiNetworkIndex(0);
    if (index < 0) {
      Serial.println("DIAG,START,test=wifi_sta,result=unconfigured");
      return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.setAutoReconnect(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.begin(diagnosticSettings.wifiSsidAt(uint8_t(index)),
               diagnosticSettings.wifiPasswordAt(uint8_t(index)));
    activeTest = Test::Wifi;
  } else {
    Serial.println("DIAG,ERROR,commands=pins|test led|test inputs|test i2c|test mic|test speaker|test wifi|stop");
    return;
  }
  Serial.printf("DIAG,START,test=%s,result=ok\n", testName(activeTest));
}

void processCommand() {
  command[commandLength] = '\0';
  if (!strcmp(command, "pins")) {
    Serial.printf("DIAG,PINS,i2c=%d/%d,touch=%d/%d,pir=%d,mic=%d/%d/%d,speaker=%d/%d/%d\n",
                  kSdaPin, kSclPin, kTouchHeadPin, kTouchSidePin, kPirPin,
                  kMicBclkPin, kMicWsPin, kMicDataPin, kSpeakerBclkPin,
                  kSpeakerLrcPin, kSpeakerDinPin);
  } else if (!strcmp(command, "stop")) {
    stopTest();
  } else if (!strncmp(command, "test ", 5)) {
    startTest(command + 5);
  } else {
    Serial.println("DIAG,HELP,commands=pins|test led|test inputs|test i2c|test mic|test speaker|test wifi|stop");
  }
  commandLength = 0;
}

void serviceSerial() {
  while (Serial.available()) {
    const char ch = char(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      processCommand();
    } else if (commandLength + 1 < sizeof(command)) {
      command[commandLength++] = ch;
    } else {
      commandLength = 0;
      Serial.println("DIAG,ERROR,command_too_long");
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.printf("DIAG,BOOT,reset_reason=%d,heap=%u,psram=%u\n",
                static_cast<int>(esp_reset_reason()), ESP.getFreeHeap(),
                ESP.getPsramSize());
  Serial.println("DIAG,HELP,commands=pins|test led|test inputs|test i2c|test mic|test speaker|test wifi|stop");
}

void loop() {
  const uint32_t now = millis();
  serviceSerial();
  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    Serial.printf("DIAG,HEARTBEAT,ms=%lu,heap=%u,min_heap=%u,psram=%u,test=%s\n",
                  static_cast<unsigned long>(now), ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), ESP.getPsramSize(), testName(activeTest));
  }
  if (activeTest == Test::Led && now - lastLedToggle >= 500) {
    lastLedToggle = now;
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? HIGH : LOW);
    Serial.printf("DIAG,LED,state=%s\n", ledOn ? "on" : "off");
  } else if (activeTest == Test::Inputs && now - lastSample >= 250) {
    lastSample = now;
    Serial.printf("DIAG,INPUTS,head=%d,side=%d,pir=%d,at=%lu\n",
                  digitalRead(kTouchHeadPin), digitalRead(kTouchSidePin),
                  digitalRead(kPirPin), static_cast<unsigned long>(now));
  } else if (activeTest == Test::I2c && now - lastSample >= 10000) {
    lastSample = now;
    scanI2c(now);
  } else if (activeTest == Test::Speaker && now - lastAudioWrite >= 20) {
    lastAudioWrite = now;
    const size_t written = speaker.write(silence, sizeof(silence), 20);
    if (written != sizeof(silence)) Serial.printf("DIAG,SPEAKER,short_write=%u\n", unsigned(written));
  } else if (activeTest == Test::Wifi &&
             now - lastSample >= 2000) {
    lastSample = now;
    Serial.printf("DIAG,WIFI,status=%u,rssi=%d\n",
                  unsigned(WiFi.status() == WL_CONNECTED),
                  WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  }
  delay(1);
}
