#pragma once
// Stand in for DEV_Config's Arduino/SPI headers while testing the real IMU driver.
#define _DEV_CONFIG_H_
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>

inline uint8_t registers[2][256] = {};
inline std::vector<std::pair<uint8_t,uint8_t>> writes;
inline void DEV_I2C_Write_Byte(uint8_t addr,uint8_t reg,uint8_t value) {
  registers[addr&1][reg]=value;writes.emplace_back(reg,value);
}
inline void DEV_I2C_Read_nByte(uint8_t addr,uint8_t reg,uint8_t *out,uint32_t length) {
  std::memcpy(out,registers[addr&1]+reg,length);
}
struct TestWire {
  uint8_t address=0,reg=0;
  unsigned count=0,at=0;
  bool failTransmission=false,shortRead=false;
  void beginTransmission(uint8_t addr) {address=addr;}
  void write(uint8_t value) {reg=value;}
  int endTransmission(bool=true) {return failTransmission?1:0;}
  unsigned requestFrom(uint8_t addr,unsigned length) {
    address=addr;at=0;count=shortRead && length>1?length-1:length;return count;
  }
  int available() {return count-at;}
  int read() {return at<count?registers[address&1][reg+at++]:-1;}
};
inline TestWire Wire;
inline void delayMicroseconds(unsigned) {}
