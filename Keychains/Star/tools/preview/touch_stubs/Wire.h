#pragma once
#include <cstdint>
struct FakeWire {
 void begin(int,int) {}
 void beginTransmission(int) {}
 void write(uint8_t) {}
 int endTransmission(bool=true) {return 0;}
 int requestFrom(int,int length,bool=true) {size=length;at=0;return length;}
 int available() {return size-at;}
 int read() {static const uint8_t packet[6]={0,1,0,120,0,120};return at<6 ? packet[at++] : 0;}
 int size=0,at=0;
};
extern FakeWire Wire;
