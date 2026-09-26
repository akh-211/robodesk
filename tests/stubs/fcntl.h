#pragma once
constexpr int F_GETFL=1,F_SETFL=2,O_NONBLOCK=4;
inline int fcntl(int,int,int){return 0;}
