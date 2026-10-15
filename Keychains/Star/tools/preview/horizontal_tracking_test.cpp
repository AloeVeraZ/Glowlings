#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "MotionGestures.h"

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
  int fails = world.tiltX > -.94f;
  float smallRight[3] = {9.81f*std::sin(.17453293f),0,9.81f*std::cos(.17453293f)};
  hold(smallRight,quiet,40);
  std::printf("Then small gravity tilt with heading still held: X=%+.3f\n",world.tiltX);
  fails += world.tiltX > -.50f;
  for (float &v : yaw) v=-v;
  hold(flat,yaw,20);
  hold(flat,quiet,80);
  fails += std::fabs(world.tiltX) > .02f;
  for (bool hanging : {false,true}) {
    WorldFollower wide; wide.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    uint32_t t = 1;
    for (int degree=0; degree<=170; ++degree) {
      float angle=degree*.017453293f;
      float a[3]={9.81f*std::sin(angle),hanging ? 9.81f*std::cos(angle) : 0,hanging ? 0 : 9.81f*std::cos(angle)};
      for (int i=0;i<20;++i,t+=25) wide.feed(a,quiet,0,t);
      if (degree==30 || degree==90 || degree==120 || degree==170)
        std::printf("%s sideways %d degrees: X=%+.3f Y=%+.3f\n",hanging ? "Hanging" : "Flat",degree,wide.tiltX,wide.tiltY);
      if (degree>=30) fails += wide.tiltX < .98f;
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
    fails += trimmed.tiltX*sign > -.19f || trimmed.tiltX*sign < -.25f;
    pose(HORIZONTAL_HANGING_CENTER_DEG+sign*35,60000);
    fails += trimmed.tiltX*sign > -.98f;
    pose(HORIZONTAL_HANGING_CENTER_DEG,1000);
    fails += std::fabs(trimmed.tiltX)>.02f;
  }
  std::printf("Horizontal tracking: %s (%d failures)\n",fails ? "FAILED" : "PASSED",fails);
  return fails ? 1 : 0;
}
