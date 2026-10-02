#pragma once
#include <string.h>

inline bool roboSameOriginHttp(const char* host,const char* origin){
  static const char prefix[]="http://";
  if(!host||!*host||!origin||strncmp(origin,prefix,sizeof(prefix)-1)!=0)return false;
  return strcmp(origin+sizeof(prefix)-1,host)==0;
}
