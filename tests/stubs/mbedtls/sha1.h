#pragma once
#include <cstring>
inline int mbedtls_sha1(const unsigned char*,size_t,unsigned char* out){std::memset(out,0,20);return 0;}
