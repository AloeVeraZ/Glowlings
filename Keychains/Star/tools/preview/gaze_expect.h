#pragma once
#include <math.h>
#include "FaceConfig.h"

// Expected settled gaze for a gravity lean toward +X in the visual sensor frame.
// The horizontal gravity angle is negative for that lean. Direction fixtures
// also check physical left/right explicitly, independently of this helper.
// Keep the dead zone and saturation explicit; do not call the firmware mapper.
inline float expectedHorizontalGaze(float degrees) {
  float travel = (fabsf(degrees) - HORIZONTAL_GAZE_NEUTRAL_DEG) /
                (HORIZONTAL_GAZE_FULL_DEG - HORIZONTAL_GAZE_NEUTRAL_DEG);
  return -HORIZONTAL_GAZE_SIGN * copysignf(fminf(1.0f, fmaxf(0.0f, travel)), degrees);
}
