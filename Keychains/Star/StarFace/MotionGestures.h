#pragma once
#include <math.h>
#include <stdint.h>
#include "FaceConfig.h"
#include "MotionHeading.h"

// Motion gestures, so every reaction works without the touch screen (for
// example behind a protective cover). Like ShakeDetector they are free of
// Arduino calls and tested on a PC (tools/preview/motion_test.cpp).
//
//   Knocks       tap the case 1-4 times      (like tapping the screen)
//   Tilt flicks  tip it one way and back     (like a swipe in that direction)
//   Rocking      rock it gently side to side (like petting it)
//
// Inputs: acceleration including gravity (m/s^2) and rotation (rad/s), in the
// screen's frame (x right, y down; see readMotion in the sketch). Flick
// directions name the side that was tipped down, the way the eyes look when
// it is tilted, so if tilt gaze is set up right, these are too.

namespace motion {
inline float len3(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
inline float decay(float dt, float tau) { return 1.0f - expf(-dt / tau); }
} // namespace motion

// ---- Knocks: short, sharp jolts on a star that was otherwise still ----------
// Needs fast samples (a few ms apart; the sketch reads the sensor on a
// separate core for this) because a knock is over in a few milliseconds.
// A jolt only counts when the star was quiet just before it, did not swing
// or keep moving, and settled again within KNOCK_RING_MS, so walking,
// running, shaking and swinging it around are not knocks.
class KnockDetector {
 public:
  struct Event {
    uint8_t count = 0;   // 0: nothing; else knocks in this run so far (1, 2, 3 ...)
    float x = 0, y = 0;  // direction the star was pushed by the knock (board axes)
  };

  Event feed(const float a[3], const float g[3], uint32_t now) {
    Event ev;
    if (!seeded) {
      for (int i = 0; i < 3; ++i) grav[i] = a[i];
      seeded = true;
      last = lastBusyAt = now;
      return ev;
    }
    float dt = (now - last) / 1000.0f;
    last = now;
    if (dt > .1f) dt = .1f;
    float lin[3];
    float k = motion::decay(dt, .4f);
    for (int i = 0; i < 3; ++i) {
      lin[i] = a[i] - grav[i];
      grav[i] += lin[i] * k;
    }
    const float mag = motion::len3(lin), spin = motion::len3(g);
    // How suddenly it changed since the last sample: a knock is a jump, while
    // walking, shaking and swinging change smoothly.
    float jerk[3] = {lin[0] - prev[0], lin[1] - prev[1], lin[2] - prev[2]};
    for (int i = 0; i < 3; ++i) prev[i] = lin[i];
    const float sharp = fmaxf(mag, .6f * motion::len3(jerk));

    if (pendingAt) {
      uint32_t age = now - pendingAt;
      if (age <= 15 && mag > peak) { peak = mag; px = lin[0]; py = lin[1]; }
      if (spin > KNOCK_MAX_SPIN_RAD_S) pendingAt = 0;               // it was swung, not knocked
      else if (age >= KNOCK_RING_MS - 40 && mag > fmaxf(3.0f, .25f * peak)) pendingAt = 0; // still moving
      else if (age >= KNOCK_RING_MS) {
        // A clean knock.
        count = (lastKnockAt && now - lastKnockAt < KNOCK_RING_MS + KNOCK_GAP_MS) ? count + 1 : 1;
        lastKnockAt = now;
        pendingAt = 0;
        ev.count = count;
        ev.x = px;
        ev.y = py;
      }
      if (!pendingAt) { lastBusyAt = now; spikeStart = 0; } // quiet is measured from the end of the ring
      return ev;
    }
    // Any movement marks it busy. A new disturbance after a quiet spell
    // remembers how long that spell was: the first samples of a knock are
    // often small, so the quiet is measured up to where the jolt began.
    if (sharp > KNOCK_QUIET_MS2 || spin > KNOCK_MAX_SPIN_RAD_S * .6f) {
      if (now - lastBusyAt > 20) { spikeStart = now; quietSpan = now - lastBusyAt; }
      lastBusyAt = now;
    }
    // Quiet long enough before? (A follow-up knock in a run needs less.)
    bool inRun = lastKnockAt && now - lastKnockAt < KNOCK_GAP_MS + KNOCK_RING_MS;
    uint32_t quietMs = inRun ? 60 : KNOCK_QUIET_BEFORE_MS;
    if (sharp > KNOCK_MS2 && now - spikeStart <= 20 && quietSpan >= quietMs && spin < KNOCK_MAX_SPIN_RAD_S) {
      pendingAt = now;
      peak = sharp; px = lin[0]; py = lin[1];
    }
    return ev;
  }

 private:
  float grav[3] = {0, 0, 0};
  bool seeded = false;
  uint32_t last = 0, lastBusyAt = 0, pendingAt = 0, lastKnockAt = 0, spikeStart = 0, quietSpan = 0;
  float peak = 0, px = 0, py = 0, prev[3] = {0, 0, 0};
  uint8_t count = 0;
};

// ---- Tilt flicks: tip the star one way and straight back --------------------
// It has to be held fairly still first, then tipped quickly by at least
// FLICK_MIN_TILT_MS2 (about 20 degrees) and brought back within FLICK_MAX_MS.
// A tilt that is held stays a tilt (the eyes just look that way), and twisting
// back and forth (dizzy) or shaking cancels it.
class TiltFlickDetector {
 public:
  enum Dir : uint8_t { NONE, LEFT, RIGHT, UP, DOWN };

  Dir feed(const float a[3], const float g[3], float shakeStrength, uint32_t now) {
    const float spin = motion::len3(g);
    const float tx = a[0], ty = a[1];
    if (!seeded) { bx = tx; by = ty; seeded = true; }
    if (!moving) {
      if (spin < .6f) {
        if (!stillSince) stillSince = now;
        lastStillAt = now;
        bx += (tx - bx) * .3f;
        by += (ty - by) * .3f;
      } else if (spin > FLICK_START_RAD_S && stillSince && lastStillAt - stillSince >= 250 &&
                 now - lastStillAt <= 250 && shakeStrength < 1.5f) {
        // Quick turn right after holding still (it may take a sample or two to speed up).
        moving = true;
        start = lastStillAt;
        mx = my = 0;
        lastSign = 0;
        reversals = 0;
      } else if (now - lastStillAt > 250) {
        stillSince = 0;
      }
      return NONE;
    }
    // Tipping: remember the furthest point; count direction changes.
    float dx = tx - bx, dy = ty - by;
    if (dx * dx + dy * dy > mx * mx + my * my) { mx = dx; my = dy; }
    float main = fabsf(g[0]) > fabsf(g[1]) ? g[0] : g[1];
    if (fabsf(main) > 1.2f) {
      int8_t s = main > 0 ? 1 : -1;
      if (lastSign && s != lastSign) ++reversals;
      lastSign = s;
    }
    if (shakeStrength > 2.5f || reversals > 1 || now - start > FLICK_MAX_MS) return cancel();
    bool back = dx * dx + dy * dy < FLICK_RETURN_MS2 * FLICK_RETURN_MS2 && spin < 1.0f;
    if (!back) return NONE;
    float far = sqrtf(mx * mx + my * my);
    moving = false;
    stillSince = 0;   // needs a fresh still moment before the next flick
    if (far < FLICK_MIN_TILT_MS2) return NONE;
    // "Up" (as the sensor reads it) moved away from the side that went down.
    if (fabsf(mx) >= fabsf(my)) return mx < 0 ? RIGHT : LEFT;
    return my < 0 ? DOWN : UP;
  }

 private:
  bool seeded = false, moving = false;
  float bx = 0, by = 0, mx = 0, my = 0;
  uint32_t stillSince = 0, lastStillAt = 0, start = 0;
  int8_t lastSign = 0;
  uint8_t reversals = 0;

  Dir cancel() {
    moving = false;
    stillSince = 0;
    return NONE;
  }
};

// ---- Rocking: slow, gentle turns side to side, like cradling it -------------
// Each swing must take ROCK_MIN_HALF_MS..ROCK_MAX_HALF_MS (quick twists are
// the dizzy gesture instead) and the star must not be bouncing around (so
// walking with it swinging in a hand does not count).
class RockDetector {
 public:
  // Returns true on every swing once it has rocked ROCK_SWINGS times in a row.
  // a includes gravity: turning leaves its length at 1 g, bouncing does not.
  bool feed(const float a[3], const float g[3], uint32_t now) {
    float dt = last ? fminf(.1f, (now - last) / 1000.0f) : .025f;
    last = now;
    bounce += (fabsf(motion::len3(a) - 9.81f) - bounce) * motion::decay(dt, .5f);
    bool any = false;
    for (int i = 0; i < 2; ++i) any |= axis[i].feed(g[i], now, dt, bounce < ROCK_MAX_BOUNCE_MS2);
    return any;
  }

 private:
  struct Axis {
    int8_t dir = 0;
    uint32_t halfStart = 0, lastMoveAt = 0;
    float angle = 0, peak = 0;
    uint8_t swings = 0;

    bool feed(float rate, uint32_t now, float dt, bool calm) {
      if (!calm) { swings = 0; dir = 0; return false; }
      if (dir && now - lastMoveAt > 700) { swings = 0; dir = 0; }   // stopped rocking
      if (fabsf(rate) < .35f) return false;
      lastMoveAt = now;
      int8_t d = rate > 0 ? 1 : -1;
      if (d == dir) {
        angle += fabsf(rate) * dt;
        peak = fmaxf(peak, fabsf(rate));
        return false;
      }
      bool good = false;
      if (dir) {
        uint32_t half = now - halfStart;
        bool ok = half >= ROCK_MIN_HALF_MS && half <= ROCK_MAX_HALF_MS &&
                  angle >= .12f && angle <= 1.2f && peak < 3.0f;
        swings = ok ? swings + 1 : 0;
        good = ok && swings >= ROCK_SWINGS;
      }
      dir = d;
      halfStart = now;
      angle = fabsf(rate) * dt;
      peak = fabsf(rate);
      return good;
    }
  } axis[2];
  float bounce = 0;
  uint32_t last = 0;
};

// ---- Following the world: tilt, roll, swing, spin and toss ------------------
// Screen-coordinate gravity/gyro fusion. A held tilt keeps its actual direction;
// settling flat or upright rebases the pitch reference. Gyro-only heading turns
// work while flat, and cues follow the physical movement rather than oppose it.
inline void rotateMotionXY(float v[3], uint8_t quarterTurns) {
  for (uint8_t i = 0; i < (quarterTurns & 3); ++i) { float x = v[0]; v[0] = -v[1]; v[1] = x; }
}

// Pixel directions use Y down. The positive-Z mounting uses Y up in the
// right-handed fusion frame. Convert 2D effects only AFTER the mounting
// rotation; never use this reflection on angular velocity before fusion.
inline void gestureMotionToScreen(float v[3]) {
  rotateMotionXY(v, GAZE_IMU_ROTATION);
  v[1] *= -GAZE_FLAT_UP_SIGN;
}

// Deliberate turns renew activity; noise and recognized transport do not.
class DeliberateTurnActivity {
 public:
  bool feed(const float g[3], uint32_t now, bool enabled = true) {
    float dt = lastAt ? fminf(.1f, (now - lastAt) * .001f) : 0;
    lastAt = now;
    if (!enabled) { arc = 0; lastMovingAt = 0; return false; }
    float rate = motion::len3(g);
    if (rate < .04f) {
      if (now - lastMovingAt > 300) arc = 0;
      return false;
    }
    lastMovingAt = now;
    arc += (rate - .03f) * dt;
    if (arc < .04f) return false; // about 2.3 degrees, above noise but responsive to a small turn
    arc = 0;
    return true;
  }
  bool feedTilt(const float a[3], bool enabled = true) {
    float n = motion::len3(a);
    if (!enabled || n < 7.5f || n > 12.0f) { haveAnchor = false; return false; }
    float unit[3] = {a[0]/n,a[1]/n,a[2]/n};
    if (!haveAnchor) { for (int i = 0; i < 3; ++i) anchor[i] = unit[i]; haveAnchor = true; return false; }
    float delta = 0;
    for (int i = 0; i < 3; ++i) delta += (unit[i]-anchor[i])*(unit[i]-anchor[i]);
    if (delta < .0016f) return false; // ~2.3 degrees, including a gentle slow lean
    for (int i = 0; i < 3; ++i) anchor[i] = unit[i];
    return true;
  }
 private:
  float anchor[3] = {};
  bool haveAnchor = false;
  float arc = 0;
  uint32_t lastAt = 0, lastMovingAt = 0;
};

class WorldFollower {
 public:
  float tiltX = 0, tiltY = 0, roll = 0;
  float swingX = 0, swingY = 0, motionLevel = 0;
  bool spun = false, weightless = false;
  bool uprightReferenceActive() const { return wearPose; }
  // Explicitly start a new awake interval. Touch renewals and brief sensor
  // gaps must not move this origin after it has been captured.
  void beginWakeSession() {
    wakeRelative=true; originPending=true; originCandidate=false;
    seeded=false; lastAt=0; heading.reset(); tiltX=swingX=0;
  }
  bool horizontalOriginReady() const { return wakeRelative && !originPending; }
  float horizontalOriginDegrees() const { return wakeCenter / .017453293f; }

  // Caller supplies enclosure trim; generic screen-frame fixtures use zero.
  void setHorizontalCenter(float tableDegrees, float hangingDegrees) {
    tableCenter = tableDegrees*.017453293f;
    hangingCenter = hangingDegrees*.017453293f;
  }
  void setFlatUpSign(float sign) {
    sign = sign > 0 ? 1 : -1;
    if (flatUpSign != sign) { flatUpSign = sign; seeded = false; }
  }

  void feed(const float a[3], const float g[3], float shakeStrength, uint32_t now,
            const float headingGyro[3] = nullptr) {
    const float dt = lastAt ? fminf(.1f, (now - lastAt) * .001f) : .025f;
    if (lastAt && now - lastAt > 250) {
      seeded = false;
      // Refresh gravity after missed reads, but keep the held heading through
      // a brief sensor/render stall. Wake-relative sessions reset only through
      // beginWakeSession(); legacy callers still reset after a long gap.
      if (now-lastAt > 2000 && !wakeRelative) heading.reset();
      if (originPending) originCandidate=false;
    }
    lastAt = now;
    spun = weightless = false;
    const float n = motion::len3(a);
    float rate[3];
    for (int i = 0; i < 3; ++i) rate[i] = fabsf(g[i]) < .03f ? 0 : g[i];
    const float spin = motion::len3(rate);
    motionLevel += (fmaxf(0, fminf(1, (spin - .06f) / .8f)) - motionLevel) * (1 - expf(-dt / .10f));
    if (n < 3 && shakeStrength < 3) {
      if (!fallSince) fallSince = now;
      if (!fallen && now - fallSince >= FREEFALL_MS) { fallen = true; weightless = true; }
    } else { fallSince = 0; if (n > 7) fallen = false; }
    if (fabsf(rate[2]) > 2.5f) { turned += rate[2] * dt; spinAt = now; }
    else if (now - spinAt > 400) turned = 0;
    if (fabsf(turned) > SPIN_TURNS_FOR_DIZZY * 6.2831853f) { turned = 0; spun = true; }

    bool reliable = n > 7.5f && n < 12.0f;
    if (!seeded) {
      if (!reliable) return;
      for (int i = 0; i < 3; ++i) up[i] = a[i] / n;
      wearPose = flatUpSign*up[2] < .95f;
      if (wearPose) hangingUpSign = copysignf(1.0f,up[1]);
      // The measured USB-bottom hanging pose is +90 degrees. A transient
      // shake sample must not select the opposite upright pole at startup.
      neutralPitch = wearPose ? 1.57079633f : 0;
      poseCandidate = wearPose; poseSince = now; transitioning = false;
      seeded = true;
    }
    // A body-frame reference vector rotates opposite the board's angular
    // velocity. Rodrigues integration uses all three gyro axes consistently.
    if (spin > 0) {
      float axis[3] = {rate[0]/spin,rate[1]/spin,rate[2]/spin};
      float c = cosf(spin * dt), sn = -sinf(spin * dt);
      float dot = axis[0]*up[0] + axis[1]*up[1] + axis[2]*up[2];
      float cross[3] = {axis[1]*up[2]-axis[2]*up[1], axis[2]*up[0]-axis[0]*up[2], axis[0]*up[1]-axis[1]*up[0]};
      for (int i = 0; i < 3; ++i) up[i] = up[i]*c + cross[i]*sn + axis[i]*dot*(1-c);
    }
    if (reliable) {
      // Transport can resemble shaking to the energy detector. Keep a slower
      // gravity correction in that case instead of integrating gyro forever.
      float k = 1 - expf(-dt / (shakeStrength < 2 ? .04f : .25f));
      for (int i = 0; i < 3; ++i) up[i] += (a[i] / n - up[i]) * k;
    }
    float length = motion::len3(up);
    if (length < .01f || !isfinite(length)) { seeded = false; return; }
    for (float &v : up) v /= length;

    // Select the display posture from gravity even while walking/being held.
    // Requiring a quiet gyro here left the flat reference stuck when worn.
    // Hysteresis keeps a 40/140-degree lean relative to upright, while only
    // an actually screen-up, nearly-flat pose returns to the table reference.
    bool candidate = wearPose;
    if (flatUpSign*up[2] > .95f) candidate = false;
    else if (fabsf(up[2]) < .57f && fabsf(up[1]) > .45f) candidate = true;
    if (candidate != wearPose) {
      if (candidate != poseCandidate) { poseCandidate = candidate; poseSince = now; }
      if (now-poseSince >= GAZE_POSE_SETTLE_MS) {
        wearPose = candidate;
        if (wearPose) hangingUpSign = copysignf(1.0f,up[1]);
        neutralPitch = wearPose ? 1.57079633f : 0;
        poseChangedAt = now; transitioning = true;
      }
    } else { poseCandidate = wearPose; poseSince = now; }

    // Gravity gives tilt, while heading turns around gravity need the gyro.
    // This also works when flat, where a Z turn leaves acceleration unchanged.
    float yaw = -(rate[0]*up[0] + rate[1]*up[1] + rate[2]*up[2]);
    if (shakeStrength >= 2 || fabsf(yaw) < .15f) yaw = 0; // suppress handheld tremor/bias pan
    // Track the full relative rotation. Scalar gyro-dot-gravity integration
    // treated parts of a closed roll/pitch circle as permanent heading turns.
    heading.update(up,headingGyro ? headingGyro : g,dt,shakeStrength<2);
    float pan=heading.value();
    float pitchPlane = up[1]*up[1] + up[2]*up[2];
    // Keep the gravity reference's hemisphere beyond a side-on lean. An
    // unsigned denominator folded 90..180 degrees back toward centre.
    float horizontalReference = wearPose ? hangingUpSign*up[1] : flatUpSign*up[2];
    float horizontalAngle = atan2f(-up[0],copysignf(sqrtf(pitchPlane),horizontalReference));
    if (originPending) {
      // Use a brief quiet, valid gravity window, not the wake shake or a
      // clipped/freefall sample. This never learns gyro bias from hand motion.
      const float *originGyro=headingGyro ? headingGyro : g;
      bool quiet=reliable && shakeStrength<2 && motion::len3(originGyro)<.20f;
      if (!quiet) originCandidate=false;
      else {
        float unit[3]={a[0]/n,a[1]/n,a[2]/n};
        float dot=unit[0]*originUp[0]+unit[1]*originUp[1]+unit[2]*originUp[2];
        if (!originCandidate || dot<.9994f) {
          for(int i=0;i<3;++i) originUp[i]=unit[i];
          originCandidate=true; originSince=now; originSamples=1;
        } else {
          if(originSamples<255)++originSamples;
          if(now-originSince>=150 && originSamples>=3) {
            wakeCenter=horizontalAngle; originPending=false;
            heading.reset();pan=0;tiltX=swingX=0;
          }
        }
      }
    }
    float horizontalCenter = wakeRelative ? wakeCenter : (wearPose ? hangingCenter : tableCenter);
    float relativeAngle=horizontalAngle-horizontalCenter;
    if(wakeRelative) {
      // Cross the atan2 seam by the short path instead of a full-screen jump.
      if(relativeAngle>3.14159265f)relativeAngle-=6.28318531f;
      if(relativeAngle<-3.14159265f)relativeAngle+=6.28318531f;
    }
    float gx = originPending ? 0 : HORIZONTAL_GAZE_SIGN*gentleAngle(relativeAngle+pan,
                                               HORIZONTAL_GAZE_FULL_DEG,HORIZONTAL_GAZE_NEUTRAL_DEG);
    // Project pitch relative to upright directly. Using |Y| prevents rolling
    // left/right through a side-on pose from inventing a vertical 180° turn.
    float pitch = wearPose ? atan2f(-copysignf(1.0f,neutralPitch)*flatUpSign*up[2],fabsf(up[1])) : pitchOf(up);
    float gy = gentleAngle(pitch,pitch > 0 ? DOWNWARD_GAZE_FULL_DEG : VERTICAL_GAZE_FULL_DEG);
    float confidence = fmaxf(0,fminf(1,(pitchPlane-.025f)/.175f));
    gy *= confidence*confidence*(3-2*confidence);
    if (transitioning) {
      float blend = fminf(1,(now-poseChangedAt)/300.0f);
      gy *= blend*blend*(3-2*blend);
      if (blend >= 1) transitioning = false;
    }
    float k = 1 - expf(-dt / .065f);
    float horizontalK = 1 - expf(-dt / HORIZONTAL_GAZE_SMOOTH_S);
    tiltX += (fmaxf(-1, fminf(1, gx)) - tiltX) * horizontalK;
    tiltY += (fmaxf(-1, fminf(1, gy)) - tiltY) * k;
    float planar = sqrtf(up[0]*up[0] + up[1]*up[1]);
    float upright = fmaxf(0, fminf(1, (planar - .35f) / .35f));
    roll = fmaxf(-FACE_ROLL_MAX, fminf(FACE_ROLL_MAX, atan2f(up[0], flatUpSign*up[1]) * upright));

    // Immediate motion cues point with the actual tilt/turn, not against it.
    float sx = 0, sy = 0;
    if (shakeStrength < 2 && spin > .15f) {
      sx = HORIZONTAL_GAZE_SIGN*(up[2]*rate[1] - up[1]*rate[2] + yaw) * HORIZONTAL_SWING_GAZE;
      // Differentiate the same gravity-derived pitch used for held gaze.
      // This includes cross-axis terms during a diagonal tilt and stays in
      // agreement with Rodrigues prediction when flat, upright or inverted.
      float pitchRate = pitchPlane > .04f ?
          (rate[0] - up[0]*(rate[1]*up[1] + rate[2]*up[2])/pitchPlane) : 0;
      if (wearPose) pitchRate *= copysignf(1.0f,neutralPitch)*flatUpSign*copysignf(1.0f,up[1]);
      else pitchRate *= flatUpSign*copysignf(1.0f,up[2]);
      sy = fmaxf(-5, fminf(5, pitchRate)) * SWING_GAZE*confidence*confidence*(3-2*confidence);
    }
    float sk = 1 - expf(-dt / .065f);
    if(originPending) sx=0; // Keep the face centered while its wake pose settles.
    swingX += (fmaxf(-.6f, fminf(.6f, sx)) - swingX) * horizontalK;
    swingY += (fmaxf(-.6f, fminf(.6f, sy)) - swingY) * sk;
  }
 private:
  // A horizontal roll may reverse Z without any vertical tilt. Using its
  // magnitude avoids a fabricated +/-180-degree vertical gaze at that point.
  float pitchOf(const float v[3]) const { return atan2f(flatUpSign*v[1],fabsf(v[2])); }
  static float gentleAngle(float radians,float fullDegrees,float neutralDegrees=GAZE_NEUTRAL_ZONE_DEG) {
    float dead = neutralDegrees*.017453293f;
    float magnitude = fmaxf(0,fabsf(radians)-dead);
    return copysignf(fminf(1,magnitude/fmaxf(.01f,fullDegrees*.017453293f-dead)),radians);
  }
  bool wakeRelative=false,originPending=false,originCandidate=false;
  float wakeCenter=0,originUp[3]={};
  uint32_t originSince=0;
  uint8_t originSamples=0;
  MotionHeading heading;
  float up[3] = {0,0,-1}, neutralPitch = 0, turned = 0;
  float tableCenter = 0, hangingCenter = 0;
  float hangingUpSign = 1;
  float flatUpSign = -1;
  bool seeded = false, fallen = false, wearPose = false, poseCandidate = false, transitioning = false;
  uint32_t lastAt = 0, poseSince = 0, poseChangedAt = 0, fallSince = 0, spinAt = 0;
};

// ---- Being carried: walking and running steps ---------------------------------
// Fed every render-loop sample. Each step is an up-and-down bounce (along
// gravity); steady bounces 0.3-0.9 s apart are walking, quick hard ones
// running. Shaking (sideways, faster, irregular) and turning it in the hand
// (no bounce at all) are not steps.
class StepDetector {
 public:
  bool walking = false, running = false;
  bool step = false;            // this sample: a step landed
  float strength = 0;           // how hard the last step landed, m/s^2

  void feed(const float a[3], float shakeStrength, uint32_t now) {
    step = false;
    float dt = lastAt ? fminf(.1f, (now - lastAt) / 1000.0f) : .025f;
    lastAt = now;
    if (!seeded) { for (int i = 0; i < 3; ++i) down[i] = a[i]; avg = motion::len3(a); seeded = true; }
    const float kg = motion::decay(dt, 1.0f);
    for (int i = 0; i < 3; ++i) down[i] += (a[i] - down[i]) * kg;
    const float dn = motion::len3(down);
    // The bounce along gravity only: a sideways shake hardly moves it.
    const float vertical = dn > 1.0f ? (a[0] * down[0] + a[1] * down[1] + a[2] * down[2]) / dn : 0.0f;
    avg += (vertical - avg) * motion::decay(dt, .6f);
    const float dyn = vertical - avg;
    if (dyn < 0) armed = true;
    peak = fmaxf(peak, dyn);
    if (armed && dyn > STEP_MS2 && shakeStrength < 5.0f) {
      armed = false;
      uint32_t gap = lastStepAt ? now - lastStepAt : 0;
      if (gap >= 300 && gap <= 900) {
        // Regular rhythm: each gap within 40% of the running average.
        if (meanGap == 0 || fabsf(float(gap) - meanGap) < .4f * meanGap) ++regular;
        else regular = 1;
        meanGap = meanGap == 0 ? gap : meanGap * .7f + gap * .3f;
      } else if (gap > 900 || !lastStepAt) {
        regular = 0;
        meanGap = 0;
      } else {
        return;   // too quick to be a step (a shake or a knock): ignore it
      }
      lastStepAt = now;
      strength = peak;
      peak = 0;
      step = regular >= 1;
      if (regular >= 3) walking = true;
    }
    if (walking && now - lastStepAt > 1300) { walking = false; regular = 0; meanGap = 0; }
    running = walking && meanGap > 0 && meanGap < 420 && strength > 4.0f;
  }

 private:
  bool seeded = false, armed = false;
  float avg = 9.81f, peak = 0, meanGap = 0, down[3] = {0, 0, 9.81f};
  uint8_t regular = 0;
  uint32_t lastAt = 0, lastStepAt = 0;
};
