#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>
#include "WakeWordBuildConfig.h"

// Arduino ESP_SR is only declared when the board target and model-storage
// configuration support it. Keep all ESP_SR symbols behind this availability gate
// so the normal Voice AI build continues to compile without the special partition.
#if ROBODESK_WAKEWORD_ENGINE_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3) && defined(CONFIG_MODEL_IN_FLASH)
#include <ESP_SR.h>
#include <esp_partition.h>
#include <model_path.h>
#define ROBODESK_WAKEWORD_ENGINE_AVAILABLE 1
#else
#define ROBODESK_WAKEWORD_ENGINE_AVAILABLE 0
#endif

class WakeWordController {
 public:
  enum State : uint8_t { Dormant = 0, Armed = 1, Triggered = 2, Commanding = 3, Fault = 4 };

  WakeWordController() = default;

  bool compiled() const { return ROBODESK_WAKEWORD_ENGINE_ENABLE != 0; }
  bool available() const { return ROBODESK_WAKEWORD_ENGINE_AVAILABLE != 0; }
  bool running() const { return state_ == Armed; }
  bool commanding() const { return state_ == Commanding; }
  State state() const { return state_; }
  uint32_t detections() const { return detections_; }
  uint32_t beginFailures() const { return beginFailures_; }
  uint32_t modeSwitchFailures() const { return modeSwitchFailures_; }
  uint32_t commandDetections() const { return commandDetections_; }
  const char* label() const { return ROBODESK_WAKEWORD_LABEL; }

  const char* availabilityReason() const {
    if(state_==Fault&&beginFailures_)return "model_or_engine_load_failed";
    if(state_==Fault&&modeSwitchFailures_)return "i2s_mode_switch_failed";
#if !ROBODESK_WAKEWORD_ENGINE_ENABLE
    return "build_flag_off";
#elif !defined(CONFIG_IDF_TARGET_ESP32S3)
    return "target_not_esp32s3";
#elif !defined(CONFIG_MODEL_IN_FLASH)
    return "model_in_flash_disabled";
#else
    return "ready";
#endif
  }

  // ESP_SR owns the microphone reads while armed. INMP441 remains physically
  // configured as 32-bit stereo I2S, while configureRX performs the 32->16
  // transform expected by ESP-SR (16 kHz signed 16-bit audio).
  bool arm(I2SClass& mic) {
#if ROBODESK_WAKEWORD_ENGINE_AVAILABLE
    if (state_ == Armed) return true;
    if (state_ == Commanding || state_ == Triggered) {
      if (!ESP_SR.end()) { ++modeSwitchFailures_; state_ = Fault; return false; }
      state_ = Dormant;
    }
    const esp_partition_t* partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"model");
    uint32_t header=0;
    if(!partition||esp_partition_read(partition,0,&header,sizeof(header))!=ESP_OK||header==0xffffffffu||header==0){++beginFailures_;state_=Fault;return false;}
    srmodel_list_t*models=esp_srmodel_init("model");
    bool valid=models&&esp_srmodel_filter(models,"wn","hiesp");
#ifndef CONFIG_SR_MN_EN_NONE
    valid=valid&&esp_srmodel_filter(models,"mn","en");
#endif
    if(models)esp_srmodel_deinit(models);
    if(!valid){++beginFailures_;state_=Fault;return false;}
    if (!mic.configureRX(16000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO,
                         I2S_RX_TRANSFORM_32_TO_16, I2S_STD_SLOT_LEFT)) {
      ++modeSwitchFailures_;
      state_ = Fault;
      return false;
    }
    instance_ = this;
    wakeLatched_ = false;
    ESP_SR.onEvent(&WakeWordController::onSrEventStatic);
    // We only need wake-word detection. Command list is intentionally empty.
    if (!ESP_SR.begin(mic, nullptr, 0, SR_CHANNELS_STEREO, SR_MODE_WAKEWORD, "MN")) {
      ++beginFailures_;
      state_ = Fault;
      mic.configureRX(16000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO,
                      I2S_RX_TRANSFORM_NONE, I2S_STD_SLOT_LEFT);
      return false;
    }
    state_ = Armed;
    return true;
#else
    (void)mic;
    state_ = Dormant;
    return false;
#endif
  }

  // Run the fixed command vocabulary while the mic is owned by ESP-SR. This
  // is entered only inside the app's bounded conversation window.
#if ROBODESK_WAKEWORD_ENGINE_AVAILABLE && !defined(CONFIG_SR_MN_EN_NONE)
  bool armCommands(I2SClass& mic, const sr_cmd_t* commands, size_t commandCount) {
    if (state_ == Commanding) return true;
    if (!commands || !commandCount || state_ == Armed || state_ == Triggered) return false;
    const esp_partition_t* partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"model");
    uint32_t header=0;
    if(!partition||esp_partition_read(partition,0,&header,sizeof(header))!=ESP_OK||header==0xffffffffu||header==0){++beginFailures_;state_=Fault;return false;}
    srmodel_list_t* models=esp_srmodel_init("model");
    const bool valid=models&&esp_srmodel_filter(models,"mn","en");
    if(models)esp_srmodel_deinit(models);
    if(!valid){++beginFailures_;state_=Fault;return false;}
    if (!mic.configureRX(16000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO,
                         I2S_RX_TRANSFORM_32_TO_16, I2S_STD_SLOT_LEFT)) {
      ++modeSwitchFailures_; state_=Fault; return false;
    }
    instance_=this;
    wakeLatched_=false;
    commandLatched_=false;
    ESP_SR.onEvent(&WakeWordController::onSrEventStatic);
    state_=Commanding;
    if (!ESP_SR.begin(mic, commands, commandCount, SR_CHANNELS_STEREO, SR_MODE_COMMAND, "MN")) {
      ++beginFailures_;
      mic.configureRX(16000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO,
                      I2S_RX_TRANSFORM_NONE, I2S_STD_SLOT_LEFT);
      state_=Fault;
      return false;
    }
    return true;
  }
#else
  bool armCommands(I2SClass& mic, const void* commands, size_t commandCount) {
    (void)mic; (void)commands; (void)commandCount;
    return false;
  }
#endif

  // Stop ESP-SR before returning microphone ownership to the regular Gemini/VAD
  // path. Manual capture uses the original 32-bit stereo samples.
  bool disarmToManual(I2SClass& mic) {
#if ROBODESK_WAKEWORD_ENGINE_AVAILABLE
    bool ok = true;
    if (state_ == Armed || state_ == Triggered || state_ == Commanding || state_ == Fault) {
      ok = ESP_SR.end();
      if (!ok) ++modeSwitchFailures_;
    }
    const bool rx = mic.configureRX(16000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO,
                                    I2S_RX_TRANSFORM_NONE, I2S_STD_SLOT_LEFT);
    if (!rx) ++modeSwitchFailures_;
    state_ = Dormant;
    wakeLatched_ = false;
    return ok && rx;
#else
    (void)mic;
    state_ = Dormant;
    return true;
#endif
  }

  bool consumeDetection() {
    if (!wakeLatched_) return false;
    wakeLatched_ = false;
    state_ = Triggered;
    return true;
  }

  bool consumeCommand(int& commandId) {
    if (!commandLatched_) return false;
    commandId=lastCommandId_;
    commandLatched_=false;
    return true;
  }

 private:
  volatile bool wakeLatched_ = false;
  volatile bool commandLatched_ = false;
  volatile int lastCommandId_ = -1;
  volatile State state_ = Dormant;
  uint32_t detections_ = 0;
  uint32_t commandDetections_ = 0;
  uint32_t beginFailures_ = 0;
  uint32_t modeSwitchFailures_ = 0;

#if ROBODESK_WAKEWORD_ENGINE_AVAILABLE
  static WakeWordController* instance_;

  static void onSrEventStatic(sr_event_t event, int commandId, int phraseId) {
    (void)phraseId;
    if (!instance_) return;
    if(instance_->state_==Commanding){
      if(event==SR_EVENT_COMMAND){instance_->lastCommandId_=commandId;instance_->commandLatched_=true;++instance_->commandDetections_;}
      // ESP-SR's Arduino example explicitly rearms command mode after both a
      // detection and timeout so a bounded session can accept follow-up words.
      if(event==SR_EVENT_COMMAND||event==SR_EVENT_TIMEOUT)ESP_SR.setMode(SR_MODE_COMMAND);
      return;
    }
    // With our stereo bus / "MN" input, CHANNEL_VERIFIED is the authoritative
    // event. Latch WAKEWORD as a fallback because some ESP-SR configurations
    // emit only the first event. The bool prevents double-triggering.
    if (event == SR_EVENT_WAKEWORD || event == SR_EVENT_WAKEWORD_CHANNEL) {
      if (!instance_->wakeLatched_) {
        instance_->wakeLatched_ = true;
        ++instance_->detections_;
      }
    }
  }
#endif
};

#if ROBODESK_WAKEWORD_ENGINE_AVAILABLE
WakeWordController* WakeWordController::instance_ = nullptr;
#endif
