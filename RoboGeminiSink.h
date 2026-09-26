#pragma once
#include "GeminiStreamParser.h"

class RoboGeminiSink : public GeminiStreamSink {
public:
  void onGeminiSetupComplete() override;
  void onGeminiAudio(const uint8_t* data,size_t len) override;
  void onGeminiInputTranscript(const char* text) override;
  void onGeminiOutputTranscript(const char* text) override;
  void onGeminiTurnComplete() override;
  void onGeminiWaitingForInput() override;
  void onGeminiGenerationComplete() override;
  void onGeminiInterrupted() override;
  void onGeminiGoAway() override;
  void onGeminiSessionHandle(const char* handle) override;
  void onGeminiToolCall(const char* id,const char* name,const char* argsJson) override;
  void onGeminiProtocolError(const char* text) override;
};
