#include <cstdio>
#include "imu_stubs/Platform.h"
#include "../../StarFace/QMI8658.cpp"

static int fails=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++fails;}}while(0)
int main() {
  // The cleanup must preserve the real board's register writes, ranges and ODR.
  registers[0][QMI8658Register_WhoAmI]=0xff;
  registers[1][QMI8658Register_WhoAmI]=5;
  CHECK(QMI8658_init()==1);
  const std::vector<std::pair<uint8_t,uint8_t>> expected={
    {QMI8658Register_Ctrl1,0x60},{QMI8658Register_Ctrl2,0x23},
    {QMI8658Register_Ctrl5,0},{QMI8658Register_Ctrl3,0x63},
    {QMI8658Register_Ctrl5,0},{QMI8658Register_Ctrl7,3}};
  CHECK(writes==expected);
  writes.clear();QMI8658_config_acc(QMI8658AccRange_2g,QMI8658AccOdr_LowPower_21Hz);
  CHECK((writes==std::vector<std::pair<uint8_t,uint8_t>>{{QMI8658Register_Ctrl2,0x0d},{QMI8658Register_Ctrl5,0}}));
  auto word=[](unsigned reg,int value) {
    registers[1][reg]=uint16_t(value)&255;registers[1][reg+1]=uint16_t(value)>>8;
  };
  word(QMI8658Register_Tempearture_L,25*256);CHECK(QMI8658_readTemp()==25);
  word(QMI8658Register_Tempearture_L,-128);CHECK(QMI8658_readTemp()==-.5f);
  const int acc[3]={4096,-4096,-32768},gyro[3]={32767,-16,0};
  registers[1][QMI8658Register_Timestamp_L]=0x56;
  registers[1][QMI8658Register_Timestamp_M]=0x34;
  registers[1][QMI8658Register_Timestamp_H]=0x12;
  for(int i=0;i<3;++i){word(QMI8658Register_Ax_L+2*i,acc[i]);word(QMI8658Register_Gx_L+2*i,gyro[i]);}
  short a[3]={},g[3]={};unsigned timestamp=0;
  CHECK(QMI8658_read_xyz_raw(a,g,&timestamp));CHECK(timestamp==0x123456);
  for(int i=0;i<3;++i)CHECK(a[i]==acc[i] && g[i]==gyro[i]);
  Wire.failTransmission=true;CHECK(!QMI8658_read_xyz_raw(a,g,&timestamp));Wire.failTransmission=false;
  Wire.shortRead=true;CHECK(!QMI8658_read_xyz_raw(a,g,&timestamp));CHECK(Wire.available()==0);Wire.shortRead=false;
  QMI8658_setRawReadLock(true);
  registers[1][QMI8658Register_StatusInt]=0;CHECK(!QMI8658_read_xyz_raw(a,g,&timestamp));
  registers[1][QMI8658Register_StatusInt]=1;CHECK(QMI8658_read_xyz_raw(a,g,&timestamp));
  registers[1][QMI8658Register_StatusInt]=3;CHECK(QMI8658_read_xyz_raw(a,g,&timestamp));
  Wire.shortRead=true;CHECK(!QMI8658_read_xyz_raw(a,g,&timestamp));Wire.shortRead=false;
  QMI8658_setRawReadLock(false);
  registers[1][QMI8658Register_WhoAmI]=0xff;CHECK(QMI8658_init()==0);
  std::printf("IMU init registers, raw axes, temperature, sample lock and I2C failures: %s (%d failures)\n",fails?"FAILED":"PASSED",fails);
  return fails?1:0;
}
