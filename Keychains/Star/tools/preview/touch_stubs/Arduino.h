#pragma once
#include <cstdint>
#include <string>
using byte=uint8_t;
using String=std::string;
#define IRAM_ATTR
constexpr int LOW=0,HIGH=1,INPUT=0,OUTPUT=1,INPUT_PULLUP=2,FALLING=3,RISING=4;
void pinMode(int,int);
int digitalRead(int);
void digitalWrite(int,int);
void delay(int);
