#pragma once
#include <cerrno>
constexpr int MSG_PEEK=1,MSG_DONTWAIT=2;
inline int recv(int,void*,size_t,int){errno=EAGAIN;return -1;}
