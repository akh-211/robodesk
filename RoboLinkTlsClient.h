#pragma once
#include <NetworkClient.h>
#include <esp_random.h>
#include <sys/time.h>
#include <new>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <esp_crt_bundle.h>
#include "RoboDualRuntime.h"
#include "RoboTunnelProtocol.h"

// S3 has no radio or socket. TLS ends here, in PSRAM-first mbedTLS memory; the C3
// only relays ciphertext to an allowlisted host over the UART tunnel (RawTcp).
class RoboLinkTlsClient:public NetworkClient {
  struct Session {
    mbedtls_ssl_context ssl;mbedtls_ssl_config conf;mbedtls_entropy_context entropy;mbedtls_ctr_drbg_context drbg;mbedtls_x509_crt ca;
    Session(){mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&conf);mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&drbg);mbedtls_x509_crt_init(&ca);}
    ~Session(){mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&conf);mbedtls_ctr_drbg_free(&drbg);mbedtls_entropy_free(&entropy);mbedtls_x509_crt_free(&ca);}
  };
  uint32_t generation_=0;Session*session_=nullptr;bool established_=false,insecure_=false,bundle_=false;const char*ca_=nullptr;uint32_t handshakeMs_=8000;
  uint8_t rx_[256];size_t rxLen_=0,rxPos_=0;
  bool tunnelUp()const{return generation_&&RoboDual.tcpGeneration==generation_&&RoboDual.tcpConnected&&RoboDual.link.connected();}
  static int bioSend(void*ctx,const unsigned char*b,size_t n){
    auto*self=static_cast<RoboLinkTlsClient*>(ctx);if(!self->tunnelUp())return MBEDTLS_ERR_SSL_CONN_EOF;
    const size_t k=RoboDual.writeTcp(b,n,20);return k?int(k):MBEDTLS_ERR_SSL_WANT_WRITE;
  }
  static int bioRecv(void*ctx,unsigned char*b,size_t n){
    auto*self=static_cast<RoboLinkTlsClient*>(ctx);if(!self->tunnelUp())return MBEDTLS_ERR_SSL_CONN_EOF;
    const size_t k=RoboDual.readTcp(b,n);return k?int(k):MBEDTLS_ERR_SSL_WANT_READ;
  }
  void freeSession(){delete session_;session_=nullptr;established_=false;rxLen_=rxPos_=0;}
  bool startTls(const char*host){
    session_=new(std::nothrow) Session;if(!session_)return false;Session&s=*session_;
    static const char personalization[]="robodesk-s3-tls";
    if(mbedtls_ctr_drbg_seed(&s.drbg,mbedtls_entropy_func,&s.entropy,reinterpret_cast<const unsigned char*>(personalization),sizeof(personalization)-1)!=0)return false;
    if(mbedtls_ssl_config_defaults(&s.conf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)!=0)return false;
    if(insecure_){mbedtls_ssl_conf_authmode(&s.conf,MBEDTLS_SSL_VERIFY_NONE);}
    else{
      // Certificate validity needs wall time; the C3 supplies NTP over the link.
      const time_t now=RoboDual.epoch();if(!now)return false;
      timeval tv{};tv.tv_sec=now;settimeofday(&tv,nullptr);
      if(bundle_){if(esp_crt_bundle_attach(&s.conf)!=0)return false;}
      else{
        if(!ca_||!*ca_||mbedtls_x509_crt_parse(&s.ca,reinterpret_cast<const unsigned char*>(ca_),strlen(ca_)+1)!=0)return false;
        mbedtls_ssl_conf_ca_chain(&s.conf,&s.ca,nullptr);
      }
      mbedtls_ssl_conf_authmode(&s.conf,MBEDTLS_SSL_VERIFY_REQUIRED);
    }
    mbedtls_ssl_conf_rng(&s.conf,mbedtls_ctr_drbg_random,&s.drbg);
    if(mbedtls_ssl_setup(&s.ssl,&s.conf)!=0||mbedtls_ssl_set_hostname(&s.ssl,host)!=0)return false;
    mbedtls_ssl_set_bio(&s.ssl,this,bioSend,bioRecv,nullptr);
    const uint32_t start=millis();int r;
    while((r=mbedtls_ssl_handshake(&s.ssl))!=0){
      if((r!=MBEDTLS_ERR_SSL_WANT_READ&&r!=MBEDTLS_ERR_SSL_WANT_WRITE)||!tunnelUp()||uint32_t(millis()-start)>=handshakeMs_)return false;
      delay(1);
    }
    established_=true;return true;
  }
  // -1 closed/error, 0 nothing yet, >0 bytes. Plaintext already buffered is returned first.
  int pull(uint8_t*p,size_t n){
    if(rxPos_<rxLen_){const size_t k=rxLen_-rxPos_<n?rxLen_-rxPos_:n;memcpy(p,rx_+rxPos_,k);rxPos_+=k;return int(k);}
    if(!established_||!tunnelUp())return -1;
    const int r=mbedtls_ssl_read(&session_->ssl,p,n);
    if(r>0)return r;
    if(r==MBEDTLS_ERR_SSL_WANT_READ||r==MBEDTLS_ERR_SSL_WANT_WRITE)return 0;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
    if(r==MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)return 0;
#endif
    established_=false;return -1;
  }
  bool pushAll(const uint8_t*p,size_t n,uint32_t budgetMs){
    size_t sent=0;const uint32_t start=millis();
    while(sent<n){
      if(!established_||!tunnelUp()||uint32_t(millis()-start)>=budgetMs)return false;
      const int r=mbedtls_ssl_write(&session_->ssl,p+sent,n-sent);
      if(r>0){sent+=size_t(r);continue;}
      if(r!=MBEDTLS_ERR_SSL_WANT_READ&&r!=MBEDTLS_ERR_SSL_WANT_WRITE){established_=false;return false;}
      delay(1);
    }
    return true;
  }
public:
  ~RoboLinkTlsClient(){stop();}
  void setCACert(const char*pem){ca_=pem;insecure_=false;bundle_=false;}
  void useCertBundle(){bundle_=true;insecure_=false;ca_=nullptr;}
  void setInsecure(){insecure_=true;}
  void setHandshakeTimeout(unsigned long seconds){handshakeMs_=seconds*1000UL<8000UL?8000UL:uint32_t(seconds*1000UL);}
  int connect(const char*host,uint16_t port,int32_t timeout)override{
    if(!host||!RoboDual.link.connected()||!RoboDual.internet)return 0;
    uint8_t open[robotunnel::OpenMax];
    stop();generation_=esp_random();if(!generation_)generation_=1;RoboDual.tcpGeneration=generation_;RoboDual.clearTcp();
    const size_t n=robotunnel::encodeOpen(open,sizeof(open),generation_,host,port,robotunnel::RawTcp);
    if(!n||!RoboDual.link.send(robolink::TcpOpen,open,n,0,100)){stop();return 0;}
    const uint32_t start=millis();bool up=false;
    while(uint32_t(millis()-start)<uint32_t(timeout>0?timeout:5000)){if(tunnelUp()){up=true;break;}if(!RoboDual.link.connected())break;delay(1);}
    if(!up||!startTls(host)){stop();return 0;}
    return 1;
  }
  int connect(const char*h,uint16_t p)override{return connect(h,p,6000);}
  int connect(IPAddress,uint16_t)override{return 0;}int connect(IPAddress,uint16_t,int32_t)override{return 0;}
  uint8_t connected()override{return established_&&tunnelUp();}
  void stop()override{
    if(session_&&established_&&tunnelUp())mbedtls_ssl_close_notify(&session_->ssl);
    freeSession();
    if(generation_&&RoboDual.tcpGeneration==generation_){uint8_t p[4];robolink::put32(p,generation_);RoboDual.tcpConnected=false;RoboDual.link.send(robolink::TcpClose,p,4);RoboDual.clearTcp();}
    generation_=0;
  }
  int available()override{
    if(rxPos_<rxLen_)return int(rxLen_-rxPos_);
    if(!connected())return 0;
    const size_t buffered=mbedtls_ssl_get_bytes_avail(&session_->ssl);
    if(!buffered&&!RoboDual.tcpAvailable())return 0;
    const int r=pull(rx_,sizeof(rx_));if(r<=0)return 0;rxLen_=size_t(r);rxPos_=0;return r;
  }
  int read(uint8_t*p,size_t n)override{if(!p||!n)return 0;return pull(p,n);}
  int read()override{uint8_t b;return read(&b,1)==1?b:-1;}
  int peek()override{return -1;}
  size_t write(const uint8_t*p,size_t n)override{return pushAll(p,n,1000)?n:0;}
  size_t write(uint8_t b)override{return write(&b,1);}
  bool enableDirectNonBlocking(){return connected();}
  int directTlsRead(uint8_t*p,size_t n){return pull(p,n);}
  bool directTlsWrite(const uint8_t*p,size_t n,uint32_t,uint32_t budget=12){return pushAll(p,n,budget<50?50:budget);}
  int tlsBuffered(){return int(rxLen_-rxPos_)+(session_&&established_?int(mbedtls_ssl_get_bytes_avail(&session_->ssl)):0);}
  int socketFd()const{return -1;}
  int rawPeek(uint8_t*,size_t,int*err=nullptr)const{if(err)*err=0;return RoboDual.tcpConnected?(RoboDual.tcpAvailable()?1:-1):0;}
};
