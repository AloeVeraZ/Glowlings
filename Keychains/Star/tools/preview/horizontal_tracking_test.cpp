#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "MotionGestures.h"
#include "gaze_expect.h"

int main() {
  WorldFollower world;
  world.setFlatUpSign(GAZE_FLAT_UP_SIGN);
  uint32_t now = 1;
  auto hold = [&](const float a[3], const float g[3], int count) {
    for (int i = 0; i < count; ++i, now += 25) world.feed(a,g,0,now);
  };
  float flat[3] = {0,0,9.81f}, quiet[3] = {}, yaw[3] = {0,0,-1};
  hold(flat,quiet,40);
  hold(flat,yaw,20);
  hold(flat,quiet,80);
  std::printf("Flat after a 0.5-radian heading turn and 2 s rest: X=%+.3f Y=%+.3f swingX=%+.3f\n",world.tiltX,world.tiltY,world.swingX);
  // A deliberate heading turn stays held. This is history sensitivity, not
  // proof of drift: this six-axis sensor has no absolute heading measurement.
  int fails = std::fabs(world.tiltX + expectedHorizontalGaze(.5f/.017453293f)) > .025f;
  float smallRight[3] = {9.81f*std::sin(.17453293f),0,9.81f*std::cos(.17453293f)};
  hold(smallRight,quiet,40);
  std::printf("Then small gravity tilt with heading still held: X=%+.3f\n",world.tiltX);
  fails += std::fabs(world.tiltX + expectedHorizontalGaze(.5f/.017453293f-10)) > .035f;
  for (float &v : yaw) v=-v;
  hold(flat,yaw,20);
  hold(flat,quiet,80);
  fails += std::fabs(world.tiltX) > .02f;
  // Check both sides of the dead zone and full-travel boundary, plus partial
  // travel. In particular, 30/35 degrees must no longer look fully sideways.
  for (bool hanging : {false,true})
  for (float degree : {0.f,3.5f,-3.5f,4.f,-4.f,4.5f,-4.5f,10.f,-10.f,
                       25.f,-25.f,30.f,-30.f,35.f,-35.f,40.f,-40.f,45.f,-45.f}) {
    WorldFollower mapped; mapped.setFlatUpSign(1);
    float angle=degree*.017453293f;
    float a[3]={9.81f*std::sin(angle),hanging?9.81f*std::cos(angle):0,hanging?0:9.81f*std::cos(angle)};
    for (uint32_t t=1;t<1001;t+=25) mapped.feed(a,quiet,0,t);
    if (std::fabs(mapped.tiltX-expectedHorizontalGaze(degree))>.015f) {
      std::printf("FAIL %s at %.1f degrees: X=%+.3f expected=%+.3f\n",
                  hanging?"hanging":"flat",degree,mapped.tiltX,expectedHorizontalGaze(degree));
      ++fails;
    }
  }
  for (bool hanging : {false,true}) {
    WorldFollower wide; wide.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    uint32_t t = 1;
    for (int degree=0; degree<=170; ++degree) {
      float angle=degree*.017453293f;
      float a[3]={9.81f*std::sin(angle),hanging ? 9.81f*std::cos(angle) : 0,hanging ? 0 : 9.81f*std::cos(angle)};
      for (int i=0;i<20;++i,t+=25) wide.feed(a,quiet,0,t);
      if (degree==30 || degree==90 || degree==120 || degree==170)
        std::printf("%s sideways %d degrees: X=%+.3f Y=%+.3f\n",hanging ? "Hanging" : "Flat",degree,wide.tiltX,wide.tiltY);
      if (degree>=HORIZONTAL_GAZE_FULL_DEG+5) fails += wide.tiltX*HORIZONTAL_GAZE_SIGN > -.98f;
    }
  }
  // Replay the measured near-neutral gravity with the configured enclosure
  // trim. Do not turn the moving-hand gyro capture into a bias calibration.
  {
    WorldFollower trimmed;
    trimmed.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    trimmed.setHorizontalCenter(HORIZONTAL_TABLE_CENTER_DEG,HORIZONTAL_HANGING_CENTER_DEG);
    float measured[3]={-9.426f,-.943f,-.848f};
    rotateMotionXY(measured,GAZE_IMU_ROTATION);
    uint32_t t=1;
    for (;t<1001;t+=25) trimmed.feed(measured,quiet,0,t);
    std::printf("Measured hanging neutral after trim: X=%+.3f Y=%+.3f\n",trimmed.tiltX,trimmed.tiltY);
    fails += std::fabs(trimmed.tiltX)>.02f;
    for (;t<2001;t+=25) trimmed.feed(flat,quiet,0,t);
    fails += std::fabs(trimmed.tiltX)>.02f;
  }
  for (int sign : {-1,1}) {
    WorldFollower trimmed;
    trimmed.setFlatUpSign(1);
    trimmed.setHorizontalCenter(HORIZONTAL_TABLE_CENTER_DEG,HORIZONTAL_HANGING_CENTER_DEG);
    uint32_t t=1;
    auto pose=[&](float displayAngle,int duration) {
      float angle=displayAngle*.017453293f;
      float a[3]={-9.81f*std::sin(angle),9.81f*std::cos(angle),0};
      for (int i=0;i<duration;i+=25,t+=25) trimmed.feed(a,quiet,0,t);
    };
    pose(HORIZONTAL_HANGING_CENTER_DEG,1000);
    fails += std::fabs(trimmed.tiltX)>.02f;
    pose(HORIZONTAL_HANGING_CENTER_DEG+sign*10,150);
    std::printf("10-degree deliberate hanging lean, direction %d: X=%+.3f\n",sign,trimmed.tiltX);
    fails += std::fabs(trimmed.tiltX*sign + expectedHorizontalGaze(10)) > .025f;
    pose(HORIZONTAL_HANGING_CENTER_DEG+sign*(HORIZONTAL_GAZE_FULL_DEG+5),60000);
    fails += trimmed.tiltX*sign*HORIZONTAL_GAZE_SIGN < .98f;
    pose(HORIZONTAL_HANGING_CENTER_DEG,1000);
    fails += std::fabs(trimmed.tiltX)>.02f;
  }
  std::printf("Horizontal tracking: %s (%d failures)\n",fails ? "FAILED" : "PASSED",fails);
  return fails ? 1 : 0;
}
