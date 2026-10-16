#pragma once
#include <math.h>
#include <stdint.h>
#include "FaceConfig.h"

// Shared launch schedule for rendering and the finish-after-unplug transition.
// Phase is monotonic (one unit = 3.2 seconds), not a repeating animation timer.
namespace ChargeFlow {
inline float noise(uint32_t n) {
  n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; n ^= n >> 16;
  return (n & 0xFFFFFF) * (1.0f / 16777216.0f);
}
inline uint32_t seed(int i,bool right) { return uint32_t(i*977+(right?131071:0)); }
inline float speed(int i,bool right) { return .8f+.45f*noise(seed(i,right)); }
inline float offset(int i,bool right) { return float(i)/CHARGE_BUBBLES+(right?.5f/CHARGE_BUBBLES:0); }
inline float trip(float phase,int i,bool right) { return phase*speed(i,right)+offset(i,right); }
inline bool launchedBefore(float phase,float stop,int i,bool right) {
  return stop<0 || floorf(trip(phase,i,right))<=floorf(trip(stop,i,right));
}
inline float finishPhase(float stop) {
  float end=stop;
  for(int side=0;side<2;++side) for(int i=0;i<CHARGE_BUBBLES;++i) {
    float arrival=(floorf(trip(stop,i,side))+1-offset(i,side))/speed(i,side);
    if(arrival>end) end=arrival;
  }
  return end;
}
}
