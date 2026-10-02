#include "GeminiStreamParser.h"
#include <cassert>
#include <string>
#include <iostream>
class Sink:public GeminiStreamSink {
 public:
  std::string input,output,handle,goAway,cancelled;uint32_t tokens=0;bool truncated=false,complete=false,interrupted=false;unsigned tools=0,errors=0;
  void onGeminiSetupComplete()override{}void onGeminiAudio(const uint8_t*,size_t)override{}
  void onGeminiInputTranscript(const char*s)override{input+=s;}void onGeminiOutputTranscript(const char*s)override{output+=s;}
  void onGeminiTurnComplete()override{complete=true;}void onGeminiWaitingForInput()override{}void onGeminiGenerationComplete()override{}void onGeminiInterrupted()override{interrupted=true;}void onGeminiGoAway()override{}
  void onGeminiSessionHandle(const char*s)override{handle=s;}void onGeminiToolCall(const char*,const char*,const char*)override{++tools;}void onGeminiProtocolError(const char*)override{++errors;}
  void onGeminiToolCancelled(const char*s)override{cancelled+=s;}void onGeminiUsage(uint32_t n)override{tokens=n;}void onGeminiTranscriptTruncated(bool)override{truncated=true;}void onGeminiGoAwayTime(const char*s)override{goAway=s;}
};
static void message(GeminiStreamParser&p,const std::string&s){p.beginMessage();for(size_t offset=0;offset<s.size();offset+=7){size_t n=s.size()-offset;if(n>7)n=7;p.feed(reinterpret_cast<const uint8_t*>(s.data()+offset),n);}p.endMessage();}
int main(){Sink sink;GeminiStreamParser parser(&sink);
  message(parser,R"({"serverContent":{"inputTranscription":{"text":"Saya suka "}}})");message(parser,R"({"serverContent":{"inputTranscription":{"text":"kopi"},"outputTranscription":{"text":"baik"},"turnComplete":true}})");assert(sink.input=="Saya suka kopi"&&sink.output=="baik"&&sink.complete);
  sink.input.clear();message(parser,R"({"serverContent":{"inputTranscription":{},"outputTranscription":{"text":"jawaban AI"}}})");assert(sink.input.empty());
  message(parser,R"({"sessionResumptionUpdate":{"resumable":true,"newHandle":"abc"}})");assert(sink.handle=="abc");message(parser,R"({"sessionResumptionUpdate":{"resumable":false,"newHandle":"ignored"}})");assert(sink.handle.empty());
  message(parser,R"({"goAway":{"timeLeft":"30s"}})");assert(sink.goAway=="30s");message(parser,R"({"toolCallCancellation":{"ids":["one","two"]}})");assert(sink.cancelled=="onetwo");message(parser,R"({"usageMetadata":{"totalTokenCount":321}})");assert(sink.tokens==321);
  std::string longText(1100,'x');message(parser,"{\"serverContent\":{\"inputTranscription\":{\"text\":\""+longText+"\"}}}");assert(sink.truncated);
  unsigned errors=sink.errors;message(parser,"{\"unknown\":\""+std::string(9000,'x')+"\",\"toolCall\":{\"functionCalls\":[{\"id\":\"a\",\"name\":\"remember_fact\",\"args\":{}}]}}");assert(sink.errors==errors+1&&sink.tools==0);
  std::cout<<"PASS: fragmented transcripts, speaker provenance, resume invalidation, GoAway, cancellation, usage and oversized metadata\n";
}
