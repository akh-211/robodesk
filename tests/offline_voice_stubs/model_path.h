#pragma once
#include <cstring>
struct srmodel_list_t {};
inline srmodel_list_t offlineVoiceTestModels;
inline srmodel_list_t* esp_srmodel_init(const char*){return &offlineVoiceTestModels;}
inline char* esp_srmodel_filter(srmodel_list_t*,const char* prefix,const char* language){
  static char wake[]="wn9_hiesp",english[]="mn7_en";
  if(std::strcmp(prefix,"wn")==0&&std::strcmp(language,"hiesp")==0)return wake;
  if(std::strcmp(prefix,"mn")==0&&std::strcmp(language,"en")==0)return english;
  return nullptr;
}
inline void esp_srmodel_deinit(srmodel_list_t*){}
