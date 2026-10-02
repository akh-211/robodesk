#pragma once
#include <cstddef>
#include <cstdint>
constexpr int ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_DATA_SPIFFS=2,ESP_OK=0;
struct esp_partition_t {};
inline esp_partition_t offlineVoiceTestPartition;
inline const esp_partition_t* esp_partition_find_first(int,int,const char*){return &offlineVoiceTestPartition;}
inline int esp_partition_read(const esp_partition_t*,size_t,void* data,size_t size){if(size>=sizeof(uint32_t))*static_cast<uint32_t*>(data)=0x12345678u;return ESP_OK;}
