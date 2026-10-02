#include "../TlsMemory.h"
#include <cassert>
#include <cstdlib>
#include <iostream>

static bool psramAvailable=true, internalAvailable=true;
static unsigned calls=0;
static uint32_t lastCaps=0;
static void* (*installedCalloc)(size_t,size_t)=nullptr;
static void (*installedFree)(void*)=nullptr;
void* heap_caps_calloc(size_t count,size_t size,uint32_t caps) {
  ++calls; lastCaps=caps;
  if(caps&MALLOC_CAP_SPIRAM ? !psramAvailable : !internalAvailable)return nullptr;
  return std::calloc(count,size);
}
void heap_caps_free(void* memory){std::free(memory);}
int mbedtls_platform_set_calloc_free(void* (*allocate)(size_t,size_t),void (*release)(void*)) {
  installedCalloc=allocate;installedFree=release;return 0;
}
int main(){
  assert(roboInstallTlsAllocator()==0);
  assert(installedCalloc==roboTlsCalloc && installedFree==roboTlsFree);
  auto* memory=static_cast<unsigned char*>(installedCalloc(16,4));
  assert(memory && calls==1 && (lastCaps&MALLOC_CAP_SPIRAM));
  for(unsigned i=0;i<64;++i)assert(memory[i]==0);
  installedFree(memory);
  psramAvailable=false;
  memory=static_cast<unsigned char*>(installedCalloc(16,4));
  assert(memory && calls==3 && (lastCaps&MALLOC_CAP_INTERNAL));
  installedFree(memory);
  internalAvailable=false;
  assert(!installedCalloc(16,4) && calls==5);
  assert(!installedCalloc(SIZE_MAX,2) && calls==5);
  installedFree(nullptr);
  std::cout<<"PASS: TLS allocator PSRAM, fallback, zeroing, exhaustion and overflow\n";
}
