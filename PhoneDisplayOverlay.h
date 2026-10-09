#pragma once
#include "PhoneNavigationBridge.h"
#include <Adafruit_SSD1306.h>
inline void roboNavigationOverlay(Adafruit_SSD1306&d,const PhoneNavigationBridge&nav,uint32_t now){
  const auto&n=nav.current();d.clearDisplay();d.setTextColor(SSD1306_WHITE);d.setTextSize(1);d.setTextWrap(false);d.setCursor(0,0);d.print(nav.stale(now)?"NAV - DATA OLD":"NAV");
  if(!strcmp(n.state,"arrived")){d.setCursor(20,22);d.setTextSize(2);d.print("ARRIVED");}
  else if(!strcmp(n.state,"rerouting")){d.setCursor(0,22);d.print("Recalculating...");}
  else{const bool left=strstr(n.turn,"left"),right=strstr(n.turn,"right");
    if(left||right){int tip=left?9:43,tail=left?40:12;d.drawLine(26,37,26,23,SSD1306_WHITE);d.drawLine(26,23,tip,23,SSD1306_WHITE);d.fillTriangle(tip,23,tail,15,tail,31,SSD1306_WHITE);}
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
