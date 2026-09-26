#pragma once
#include "Arduino.h"
#include <memory>
constexpr int MBEDTLS_ERR_SSL_WANT_READ=-2, MBEDTLS_ERR_SSL_WANT_WRITE=-3;
constexpr int MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY=-4;
struct sslclient_context {int socket=-1; int ssl_ctx=0;};
inline int mbedtls_ssl_write(int*,const uint8_t* p,size_t n){
  ++mockWriteCalls;
  if(mockWriteStalled)return MBEDTLS_ERR_SSL_WANT_WRITE;
  size_t k=mockWriteLimit?std::min(n,mockWriteLimit):n;
  mockTx.insert(mockTx.end(),p,p+k);return int(k);
}
inline int mbedtls_ssl_read(int*,uint8_t* p,size_t n){
  if(mockRx.empty())return MBEDTLS_ERR_SSL_WANT_READ;
  size_t k=std::min(n,mockRx.size());std::copy_n(mockRx.begin(),k,p);
  mockRx.erase(mockRx.begin(),mockRx.begin()+k);return int(k);
}
inline size_t mbedtls_ssl_get_bytes_avail(const int*){return mockRx.size();}
class WiFiClientSecure {
  std::string headers;size_t readAt=0;
protected:
  std::shared_ptr<sslclient_context> sslclient=std::make_shared<sslclient_context>();
public:
  void stop(){sslclient->socket=-1;}
  void setTimeout(uint32_t){}
  void setHandshakeTimeout(uint32_t){}
  void setInsecure(){}
  void setCACert(const char*){}
  bool connect(const char*,uint16_t,int32_t){
    ++mockConnectCalls;delay(500);
    if(!mockConnectOK)return false;
    sslclient->socket=1;readAt=0;
    headers="HTTP/1.1 101 Switching Protocols\r\nSec-WebSocket-Accept: AAAAAAAAAAAAAAAAAAAAAAAAAAA=\r\n\r\n";
    return true;
  }
  size_t write(const uint8_t*,size_t n){return n;}
  int available(){return int(headers.size()-readAt);}
  int read(){return readAt<headers.size()?uint8_t(headers[readAt++]):-1;}
};
