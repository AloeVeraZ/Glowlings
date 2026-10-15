#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "MotionGestures.h"

// A physically consistent closed path: roll right, tilt up, undo the roll
// while tilted, then undo the pitch. R = Rz(roll) * Rx(pitch); all four legs
// return to the same orientation, not merely the same gravity readings.
int main() {
  int fails=0;
  const float radians=.017453293f;
  for (int slowLeg : {-1,1,2,3}) for (int sign : {-1,1}) for (int period : {10,25,50}) {
    WorldFollower world; world.setFlatUpSign(1);
    uint32_t now=1;
    float neutral[3]={0,9.81f,0},quiet[3]={};
    for (int i=0;i<1000;i+=period,now+=period) world.feed(neutral,quiet,0,now);
    for (int repeat=0;repeat<3;++repeat) {
      for (int leg=0;leg<4;++leg) {
       int duration=leg==slowLeg ? 8000 : 1000;
       for (int elapsed=period;elapsed<=duration;elapsed+=period,now+=period) {
        float f=elapsed/float(duration),rateScale=1000.0f/duration;
        float roll=sign*60*radians*(leg==0 ? f : leg==1 ? 1 : leg==2 ? 1-f : 0);
        float pitch=50*radians*(leg==0 ? 0 : leg==1 ? f : leg==2 ? 1 : 1-f);
        float rollRate=(leg==0 ? sign : leg==2 ? -sign : 0)*60*radians*rateScale;
        float pitchRate=(leg==1 ? 1 : leg==3 ? -1 : 0)*50*radians*rateScale;
        float a[3]={9.81f*std::sin(roll),9.81f*std::cos(roll)*std::cos(pitch),-9.81f*std::cos(roll)*std::sin(pitch)};
        float g[3]={pitchRate,rollRate*std::sin(pitch),rollRate*std::cos(pitch)};
        world.feed(a,g,0,now);
      }
      }
      for (int i=0;i<1000;i+=period,now+=period) world.feed(neutral,quiet,0,now);
      std::printf("Closed loop slowLeg=%d sign=%d period=%d ms repeat=%d: X=%+.4f Y=%+.4f\n",slowLeg,sign,period,repeat+1,world.tiltX,world.tiltY);
      if (std::fabs(world.tiltX)>.03f || std::fabs(world.tiltY)>.03f) ++fails;
    }
  }
  // Stationary board with the measured gyro bias: the separate corrected
  // heading input must not accumulate a turn or modify vertical fusion.
  {
    WorldFollower rawVertical,headingCorrected;
    rawVertical.setFlatUpSign(1); headingCorrected.setFlatUpSign(1);
    float a[3]={0,9.81f,0},zero[3]={};
    float bias[3]={HORIZONTAL_GYRO_BIAS_RAD_S[0],HORIZONTAL_GYRO_BIAS_RAD_S[1],HORIZONTAL_GYRO_BIAS_RAD_S[2]};
    rotateMotionXY(bias,GAZE_IMU_ROTATION);
    for (uint32_t t=1;t<30001;t+=25) {
      rawVertical.feed(a,bias,0,t);
      headingCorrected.feed(a,bias,0,t,zero);
      if (std::fabs(rawVertical.tiltY-headingCorrected.tiltY)>.000001f) ++fails;
    }
    if (std::fabs(headingCorrected.tiltX)>.03f) ++fails;
    std::printf("Measured gyro-bias compensation at rest: X=%+.4f Y=%+.4f\n",headingCorrected.tiltX,headingCorrected.tiltY);
  }
  // Smooth right/up/left/down circles, with simultaneous changing axes,
  // include slow portions that a per-axis gyro cutoff would discard.
  for (int sign : {-1,1}) for (int period : {10,25,50}) {
    WorldFollower world; world.setFlatUpSign(1);
    uint32_t now=1;
    float neutral[3]={0,9.81f,0},quiet[3]={};
    for (int i=0;i<1000;i+=period,now+=period) world.feed(neutral,quiet,0,now);
    for (int repeat=0;repeat<3;++repeat) {
      for (int elapsed=period;elapsed<=4000;elapsed+=period,now+=period) {
        float phase=elapsed*.001f*1.57079633f;
        float roll=sign*60*radians*std::sin(phase),pitch=25*radians*(1-std::cos(phase));
        float rollRate=sign*60*radians*1.57079633f*std::cos(phase),pitchRate=25*radians*1.57079633f*std::sin(phase);
        float a[3]={9.81f*std::sin(roll),9.81f*std::cos(roll)*std::cos(pitch),-9.81f*std::cos(roll)*std::sin(pitch)};
        float g[3]={pitchRate,rollRate*std::sin(pitch),rollRate*std::cos(pitch)};
        world.feed(a,g,0,now);
      }
      for (int i=0;i<1000;i+=period,now+=period) world.feed(neutral,quiet,0,now);
      std::printf("Smooth circle sign=%d period=%d ms repeat=%d: X=%+.4f Y=%+.4f\n",sign,period,repeat+1,world.tiltX,world.tiltY);
      if (std::fabs(world.tiltX)>.03f || std::fabs(world.tiltY)>.03f) ++fails;
    }
  }
  std::printf("Orientation loop: %s (%d failures)\n",fails ? "FAILED" : "PASSED",fails);
  return fails ? 1 : 0;
}
