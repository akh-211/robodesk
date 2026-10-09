#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <new>
#include <vector>
struct MockQueue { size_t itemSize=0,capacity=0;std::deque<std::vector<uint8_t>> items; };
using QueueHandle_t=MockQueue*;
constexpr int pdTRUE=1,pdFALSE=0;
inline bool mockQueueCreateOK=true;
inline void (*mockBeforeQueueSend)()=nullptr;
inline QueueHandle_t xQueueCreate(unsigned n,unsigned itemSize){if(!mockQueueCreateOK)return nullptr;return new(std::nothrow)MockQueue{itemSize,n,{}};}
inline int xQueueSend(QueueHandle_t q,const void*item,uint32_t){if(mockBeforeQueueSend){auto f=mockBeforeQueueSend;mockBeforeQueueSend=nullptr;f();}if(!q||q->items.size()>=q->capacity)return pdFALSE;std::vector<uint8_t>b(q->itemSize);memcpy(b.data(),item,q->itemSize);q->items.push_back(std::move(b));return pdTRUE;}
inline int xQueueReceive(QueueHandle_t q,void*out,uint32_t){if(!q||q->items.empty())return pdFALSE;memcpy(out,q->items.front().data(),q->itemSize);q->items.pop_front();return pdTRUE;}
inline int xQueueReset(QueueHandle_t q){if(!q)return pdFALSE;q->items.clear();return pdTRUE;}
inline unsigned uxQueueMessagesWaiting(QueueHandle_t q){return q?unsigned(q->items.size()):0;}
inline void vQueueDelete(QueueHandle_t q){delete q;}
