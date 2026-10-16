#pragma once
#include "PhoneNavigationBridge.h"
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <string.h>
inline void roboNavigationOverlay(Adafruit_SSD1306&d,const PhoneNavigationBridge&nav,uint32_t now){
  const auto&n=nav.current();d.clearDisplay();d.setTextColor(SSD1306_WHITE);d.setTextSize(1);d.setTextWrap(false);d.setCursor(0,0);d.print(nav.stale(now)?"NAV - DATA OLD":"NAV");
  if(!strcmp(n.state,"arrived")){d.setCursor(20,22);d.setTextSize(2);d.print("ARRIVED");}
  else if(!strcmp(n.state,"rerouting")){d.setCursor(0,22);d.print("Recalculating...");}
  else{const bool left=strstr(n.turn,"left"),right=strstr(n.turn,"right");
    if((left||right)&&strstr(n.turn,"slight")){ // diagonal arrow so slight/exit/fork differ from a full turn
      const int dir=left?-1:1;d.drawLine(26,38,26,28,SSD1306_WHITE);d.drawLine(26,28,26+dir*6,22,SSD1306_WHITE);d.drawLine(26,29,26+dir*6,23,SSD1306_WHITE);
      d.fillTriangle(26+dir*12,16,26+dir*(-1),17,26+dir*11,27,SSD1306_WHITE);}
    else if(left||right){int tip=left?9:43,tail=left?40:12;d.drawLine(26,37,26,23,SSD1306_WHITE);d.drawLine(26,23,tip,23,SSD1306_WHITE);d.fillTriangle(tip,23,tail,15,tail,31,SSD1306_WHITE);}
    else if(!strcmp(n.turn,"straight")){d.drawLine(26,37,26,17,SSD1306_WHITE);d.fillTriangle(26,13,16,25,36,25,SSD1306_WHITE);}
    else if(!strcmp(n.turn,"uturn")){d.drawCircle(26,22,10,SSD1306_WHITE);d.fillTriangle(17,30,12,23,22,23,SSD1306_WHITE);}
    else if(!strcmp(n.turn,"roundabout")){d.drawCircle(26,26,10,SSD1306_WHITE);d.drawLine(26,16,26,11,SSD1306_WHITE);}
    else{d.setCursor(21,20);d.setTextSize(2);d.print("?");}
    d.setTextSize(1);d.setCursor(61,18);if(!n.distanceKnown){d.print("-- m");}else if(n.distanceMeters>=1000){d.print(float(n.distanceMeters)/1000.f,1);d.print(" km");}else{d.print(n.distanceMeters);d.print(" m");}}
  d.setTextSize(1);d.setCursor(0,43);d.print(n.road);d.setCursor(0,55);d.print(n.eta);d.setTextWrap(true);
}
inline void roboActivityOverlay(Adafruit_SSD1306&d,uint8_t id,uint32_t elapsed,uint8_t variant){
  if(id==8){int x=12+int((elapsed/120+variant*11)%100),y=14+int((elapsed/300)%22);d.drawRect(x,y,10,10,SSD1306_WHITE);d.drawLine(x-4,y+5,x+14,y+5,SSD1306_WHITE);}
  else if(id==10){d.fillRect(0,53,128,11,SSD1306_BLACK);d.setTextSize(1);d.setTextColor(SSD1306_WHITE);d.setCursor(0,55);d.print("Tap head to play");}
  else if(id==12){d.fillRect(0,53,128,11,SSD1306_BLACK);d.setTextSize(1);d.setTextColor(SSD1306_WHITE);d.setCursor(0,55);d.print("FOCUS - quiet company");}
  else if(id==13){unsigned phase=(elapsed/80)%100;int radius=6+int(phase<50?phase:100-phase)/4;d.fillRect(0,0,128,64,SSD1306_BLACK);d.drawCircle(64,30,radius,SSD1306_WHITE);d.setTextSize(1);d.setTextColor(SSD1306_WHITE);d.setCursor(38,55);d.print(phase<50?"Breathe in":"Breathe out");}
}
// Notification / alert cards: a small animated icon (drawn with primitives, no bitmaps) plus styled text.
enum RoboIcon : uint8_t { IconBell, IconMessage, IconCall, IconMail, IconTimer, IconMic, IconCheck, IconCross };
inline bool roboHas(const char*s,const char*w){if(!s||!w)return false;for(;*s;++s){const char*a=s,*b=w;while(*a&&*b&&((*a|32)==(*b|32))){++a;++b;}if(!*b)return true;}return false;}
inline RoboIcon roboIconForApp(const char*appId,const char*app){
  const char*k[2]={appId,app};
  for(auto s:k){
    if(roboHas(s,"dialer")||roboHas(s,"phone")||roboHas(s,"call"))return IconCall;
    if(roboHas(s,"mail")||roboHas(s,"outlook"))return IconMail;
    if(roboHas(s,"whatsapp")||roboHas(s,"telegram")||roboHas(s,"messag")||roboHas(s,"sms")||roboHas(s,"signal")||roboHas(s,"line")||roboHas(s,"chat")||roboHas(s,"instagram"))return IconMessage;
    if(roboHas(s,"clock")||roboHas(s,"alarm")||roboHas(s,"calendar"))return IconTimer;
  }
  return IconBell;
}
inline RoboIcon roboIconForAlert(const char*t){
  if(roboHas(t,"fail")||roboHas(t,"unavailable")||roboHas(t,"not saved")||roboHas(t,"gagal")||roboHas(t,"tidak"))return IconCross;
  if(roboHas(t,"mic"))return IconMic;
  if(roboHas(t,"timer")||roboHas(t,"focus")||roboHas(t,"minute"))return IconTimer;
  if(roboHas(t,"started")||roboHas(t,"resumed")||roboHas(t,"enabled")||roboHas(t,"OK")||roboHas(t,"ditutup")||roboHas(t,"dilanjut")||roboHas(t,"dijeda")||roboHas(t,"paused"))return IconCheck;
  return IconBell;
}
// Icon centred on (cx,cy) inside a (2r)x(2r) box; t = ms since the card appeared (drives the motion).
inline void roboDrawIcon(Adafruit_SSD1306&d,RoboIcon ic,int cx,int cy,int r,uint32_t t){
  const uint16_t W=SSD1306_WHITE,B=SSD1306_BLACK;const int s=int(t/90%8);
  switch(ic){
    case IconMessage:{int dy=(t/250%2)?0:-1;d.fillRoundRect(cx-r,cy-r*3/4+dy,2*r,r*3/2,3,W);d.fillTriangle(cx-r/2,cy+r*3/4+dy-1,cx-r/4,cy+r*3/4+dy-1,cx-r/2-2,cy+r+dy,W);
      for(int i=0;i<3;++i){if((t/200+i)%3==0)d.fillCircle(cx-r/2+i*r/2,cy+dy,1,B);else d.drawPixel(cx-r/2+i*r/2,cy+dy,B);}break;}
    case IconCall:{int sh=(t<2500&&(t/60%2))?1:-1;if(t>=2500)sh=0;int x=cx+sh;d.fillRoundRect(x-r/2,cy-r,r,2*r,3,W);d.fillRect(x-r/2+2,cy-r+3,r-4,2*r-8,B);d.fillCircle(x,cy+r-3,1,B);
      if(t/300%2){d.drawCircle(x+r,cy-r/2,2,W);}break;}
    case IconMail:{d.drawRect(cx-r,cy-r*2/3,2*r,r*4/3,W);int f=t<400?int(t*r/400):r;d.drawLine(cx-r,cy-r*2/3,cx-r+f,cy-r*2/3+f*2/3,W);d.drawLine(cx+r,cy-r*2/3,cx+r-f,cy-r*2/3+f*2/3,W);
      if(t>=400&&t/400%2)d.fillCircle(cx+r,cy-r*2/3,2,W);break;}
    case IconTimer:{d.drawCircle(cx,cy+1,r-1,W);d.fillRect(cx-2,cy-r-1,4,2,W);float a=t/600.f;d.drawLine(cx,cy+1,cx+int((r-3)*sinf(a)),cy+1-int((r-3)*cosf(a)),W);d.fillCircle(cx,cy+1,1,W);break;}
    case IconMic:{d.fillRoundRect(cx-r/3,cy-r,2*r/3+1,r+2,r/3,W);d.drawRoundRect(cx-r*2/3,cy-r/2,r*4/3+1,r+1,r/2,W);d.drawLine(cx,cy+r/2+1,cx,cy+r,W);d.drawLine(cx-r/3,cy+r,cx+r/3,cy+r,W);break;}
    case IconCheck:{int p=t<500?int(t*2*r/500):2*r;d.drawCircle(cx,cy,r,W);int a=p<r/2+2?p:r/2+2;for(int o=0;o<2;++o){d.drawLine(cx-r/2,cy+o,cx-r/2+a,cy+a+o,W);if(p>r/2+2){int b=p-(r/2+2);d.drawLine(cx-r/2+(r/2+2),cy+(r/2+2)+o,cx-r/2+(r/2+2)+b,cy+(r/2+2)-b+o,W);}}break;}
    case IconCross:{d.drawCircle(cx,cy,r,W);int k=(t/120%2)?r/2:r/2-1;for(int o=0;o<2;++o){d.drawLine(cx-k,cy-k+o,cx+k,cy+k+o,W);d.drawLine(cx-k,cy+k+o,cx+k,cy-k+o,W);}break;}
    default:{int sw=(t<3000)?(s%4==1?-2:s%4==3?2:0):0;d.fillTriangle(cx+sw,cy-r,cx-r+sw,cy+r/2,cx+r+sw,cy+r/2,W);d.fillRect(cx-r+sw,cy+r/2-1,2*r,3,W);d.fillCircle(cx+sw,cy+r/2+3,2,W);d.fillCircle(cx+sw,cy-r,1,W);
      if(t/400%2){d.drawLine(cx+r+3,cy-r/2,cx+r+5,cy-r/2-2,W);d.drawLine(cx-r-3,cy-r/2,cx-r-5,cy-r/2-2,W);}}
  }
}
inline void roboPrintClip(Adafruit_SSD1306&d,int x,int y,const char*s,int maxChars){char b[24];int n=0;for(;s&&*s&&n<maxChars&&n<22;++s)if((unsigned char)*s>=32&&(unsigned char)*s<127)b[n++]=*s;b[n]=0;d.setCursor(x,y);d.print(b);}
// Full-screen phone notification card. elapsed = ms since shown, total = display time (progress bar).
inline void roboNotificationCard(Adafruit_SSD1306&d,RoboIcon ic,const char*app,const char*title,const char*body,uint32_t elapsed,uint32_t total){
  d.clearDisplay();d.setTextColor(SSD1306_WHITE);d.setTextSize(1);d.setTextWrap(false);
  d.drawRoundRect(0,0,128,64,4,SSD1306_WHITE);
  const int slide=elapsed<350?int((350-elapsed)*40/350):0; // icon slides in from the left
  d.drawRoundRect(5-slide,5,30,30,5,SSD1306_WHITE);
  roboDrawIcon(d,ic,20-slide,20,9,elapsed);
  roboPrintClip(d,41,7,app,13);d.drawFastHLine(41,17,82,SSD1306_WHITE);
  roboPrintClip(d,41,21,title,13);roboPrintClip(d,42,21,title,13); // doubled 1 px = bold
  roboPrintClip(d,6,38,body,20);
  const char*rest=body;for(int n=0;rest&&*rest&&n<20;++n)++rest;
  roboPrintClip(d,6,48,rest,20);
  uint32_t e=elapsed>total?total:elapsed;int w=total?int(120u-e*120u/total):0;d.fillRect(4,60,w,2,SSD1306_WHITE);
  d.setTextWrap(true);
}
// Bottom strip (y 46..63) over the eyes for short local alerts.
inline void roboAlertStrip(Adafruit_SSD1306&d,RoboIcon ic,const char*text,uint32_t elapsed){
  const int slide=elapsed<300?int((300-elapsed)*20/300):0;
  d.fillRect(0,46,128,18,SSD1306_BLACK);d.drawRoundRect(0,46,128,18,4,SSD1306_WHITE);
  roboDrawIcon(d,ic,11-slide,55,5,elapsed);
  d.setTextColor(SSD1306_WHITE);d.setTextSize(1);d.setTextWrap(false);
  if(strlen(text)<=17)roboPrintClip(d,24,51,text,17);
  else{roboPrintClip(d,24,48,text,17);roboPrintClip(d,24,56,text+17,17);}
  d.setTextWrap(true);
}
