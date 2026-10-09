#pragma once
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include "FirmwareOtaKey.h"
#include "RoboBoardProfile.h"

// The receiver independently authenticates the role/version/size/hash before
// erase, and the actual bytes before selecting the inactive slot.
class RoboSignedImage {
  uint32_t version_=0;size_t expected_=0,written_=0;uint8_t expectedHash_[32]{};mbedtls_sha256_context hash_;bool active_=false;
public:
  const char* error="none";uint32_t lastProgress=0;
  static bool version(uint32_t*out){Preferences p;if(!p.begin("robodesk_ota",true))return false;*out=p.getUInt("version",ROBODESK_OTA_INITIAL_VERSION);p.end();return true;}
  static bool verify(const char*target,uint32_t version,size_t size,const char*sha,const char*signature){
    if(!sha||strlen(sha)!=64||!signature)return false;char prefix[16];snprintf(prefix,sizeof(prefix),"%u:",unsigned(version));if(strncmp(signature,prefix,strlen(prefix)))return false;
    const char*hex=signature+strlen(prefix);size_t n=strlen(hex);if(n<128||n>160||(n&1))return false;uint8_t sig[80];for(size_t i=0;i<n/2;++i){if(!isxdigit(hex[i*2])||!isxdigit(hex[i*2+1]))return false;char pair[]={hex[i*2],hex[i*2+1],0};sig[i]=uint8_t(strtoul(pair,nullptr,16));}
    for(size_t i=0;i<64;++i)if(!isxdigit(sha[i]))return false;
    char text[192];int len=snprintf(text,sizeof(text),"RoboDeskSonicCharacter|%s|%u|%u|%s",target,unsigned(version),unsigned(size),sha);if(len<=0||size_t(len)>=sizeof(text))return false;
    uint8_t digest[32];mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);bool ok=mbedtls_sha256_starts(&hash,0)==0&&mbedtls_sha256_update(&hash,reinterpret_cast<const uint8_t*>(text),len)==0&&mbedtls_sha256_finish(&hash,digest)==0;mbedtls_sha256_free(&hash);if(!ok)return false;
    mbedtls_pk_context key;mbedtls_pk_init(&key);ok=mbedtls_pk_parse_public_key(&key,reinterpret_cast<const uint8_t*>(ROBODESK_OTA_PUBLIC_KEY_PEM),sizeof(ROBODESK_OTA_PUBLIC_KEY_PEM))==0&&mbedtls_pk_verify(&key,MBEDTLS_MD_SHA256,digest,0,sig,n/2)==0;mbedtls_pk_free(&key);return ok;
  }
  bool begin(uint32_t v,size_t n,const char*sha,const char*sig){
#if !defined(CONFIG_APP_ROLLBACK_ENABLE)
    error="rollback_disabled";return false;
#else
    uint32_t current=0;const esp_partition_t*target=esp_ota_get_next_update_partition(nullptr);
    if(active_||!version(&current)||v<=current||v<=6||!target||!n||n>ROBODESK_APP_PARTITION_SIZE||n>target->size){error="target_or_version";return false;}
    if(!verify(ROBODESK_OTA_SIGNING_TARGET,v,n,sha,sig)){error="signature";return false;}
    for(unsigned i=0;i<32;++i){char pair[]={sha[i*2],sha[i*2+1],0};expectedHash_[i]=uint8_t(strtoul(pair,nullptr,16));}
    mbedtls_sha256_init(&hash_);if(mbedtls_sha256_starts(&hash_,0)!=0||!Update.begin(n,U_FLASH)){mbedtls_sha256_free(&hash_);error="slot_begin";return false;}
    version_=v;expected_=n;written_=0;active_=true;lastProgress=millis();error="none";return true;
#endif
  }
  bool write(size_t offset,const uint8_t*p,size_t n){if(!active_||offset!=written_||!p||n>expected_-written_){error="offset_or_size";return false;}if(Update.write(const_cast<uint8_t*>(p),n)!=n||mbedtls_sha256_update(&hash_,p,n)!=0){abort();error="write";return false;}written_+=n;lastProgress=millis();return true;}
  bool finish(){if(!active_||written_!=expected_){abort();error="size";return false;}uint8_t digest[32];bool ok=mbedtls_sha256_finish(&hash_,digest)==0&&!memcmp(digest,expectedHash_,32);mbedtls_sha256_free(&hash_);active_=false;if(!ok){Update.abort();error="hash";return false;}
    const esp_partition_t*target=esp_ota_get_next_update_partition(nullptr);Preferences p;
    if(!target||!p.begin("robodesk_ota",false)){Update.abort();error="version_stage";return false;}
    ok=p.putUInt("pending",version_)==4&&p.putUInt("pend_off",target->address)==4;
    if(!ok||!Update.end(true)||!Update.isFinished()){p.remove("pending");p.remove("pend_off");p.end();Update.abort();error="commit";return false;}p.end();return true;
  }
  void abort(){if(active_){mbedtls_sha256_free(&hash_);active_=false;}Update.abort();}
  bool active()const{return active_;}size_t written()const{return written_;}
};
