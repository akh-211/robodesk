#pragma once
#include <stddef.h>
#include <stdint.h>
constexpr uint32_t MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_INTERNAL=4;
void* heap_caps_calloc(size_t count,size_t size,uint32_t caps);
void heap_caps_free(void* memory);
