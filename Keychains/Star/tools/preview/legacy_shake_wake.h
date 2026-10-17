#pragma once
// Historical detector retained only for comparison tests, not ESP32 firmware.
#include "ShakeDetector.h"

static constexpr float SHAKE_WAKE_STRENGTH = 3.5f;   // average shake strength to count as shaking
static constexpr float SHAKE_WAKE_STROKE_MS2 = 11.0f; // one stroke while waking it
static constexpr uint32_t SHAKE_WAKE_HOLD_MS = 1500; // 25% shorter than the previous two seconds
static constexpr uint32_t SHAKE_WAKE_DROPOUT_MS = 350;
static constexpr float SHAKE_GYRO_RAD_S = 3.0f;   // while asleep, rotation faster than this counts toward the wake shake
static constexpr uint8_t SHAKE_WAKE_SWINGS = 6; // actual alternating strong strokes, not footsteps

// Wake check after the IMU's motion alarm woke the CPU with the screen dark.
// Asleep it is deliberately hard to wake: it needs a steady shake, kept up for
// SHAKE_WAKE_HOLD_MS (about 2 s); stop for longer than SHAKE_WAKE_DROPOUT_MS
// and the count starts over. Gravity is estimated from scratch here (the CPU
// was off), starting from the first sample scaled to 1 g.
class ShakeWakeCheck {
 public:
  uint8_t jolts = 0;      // strokes in the current run (for the log)
  float lastJolt = 0;

  ShakeWakeCheck() {      // the sleep-time thresholds (unchanged by awake tuning)
    det.strokeMs2 = SHAKE_WAKE_STROKE_MS2;
    det.steadyDizzy = false;
    det.strokesForDizzy = 3;
    det.dizzyStrength = 3.5f;
    det.holdMs = 400;
    det.spinRadS = SHAKE_GYRO_RAD_S;
  }

  uint32_t shakingFor(uint32_t now) const { return since ? now - since : 0; }
  bool stopped(uint32_t now) const { return !since && lastOnAt && now - lastOnAt > 1000; }

  bool feed(const float a[3], const float g[3], uint32_t now) {
    float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (!seeded) {
      float s = mag > .1f ? 9.81f / mag : 0.0f;
      for (int i = 0; i < 3; ++i) grav[i] = a[i] * s;
      seeded = true;
      lastAt = now;
    }
    float dt = (now - lastAt) / 1000.0f;
    lastAt = now;
    float k = 1.0f - expf(-dt / .25f), lin[3];
    for (int i = 0; i < 3; ++i) {
      grav[i] += (a[i] - grav[i]) * k;
      lin[i] = a[i] - grav[i];
    }
    float spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    ShakeDetector::Event ev = det.feed(lin, spin, now);
    if (ev == ShakeDetector::STROKE || ev == ShakeDetector::STARTLE || ev == ShakeDetector::DIZZY) {
      if (jolts < 255) ++jolts;
      lastJolt = sqrtf(lin[0] * lin[0] + lin[1] * lin[1] + lin[2] * lin[2]);
    }
    if (det.strength > SHAKE_WAKE_STRENGTH) {
      if (!since) { since = now; swings = 0; lastSwingAt = 0; }
      lastOnAt = now;
    } else if (since && now - lastOnAt > SHAKE_WAKE_DROPOUT_MS) {
      since = 0;   // the shake stopped: start over
      jolts = swings = 0;
    }
    // Retain testing's acceleration + gyro strength, but require repeated
    // strong strokes that reverse. Running's one-sided footfalls cannot qualify.
    float jolt = sqrtf(lin[0]*lin[0] + lin[1]*lin[1] + lin[2]*lin[2]);
    if (since && jolt > SHAKE_WAKE_STROKE_MS2 &&
        (!lastSwingAt || now - lastSwingAt > 60)) {
      float dot = lin[0]*swingDir[0] + lin[1]*swingDir[1] + lin[2]*swingDir[2];
      if (!swings || dot < 0) {
        if (swings < 255) ++swings;
        for (int i = 0; i < 3; ++i) swingDir[i] = lin[i];
        lastSwingAt = now;
      }
    }
    if (since && lastSwingAt && now - lastSwingAt > SHAKE_WAKE_DROPOUT_MS + 150) {
      since = 0;
      jolts = swings = 0;
    }
    // Steady shaking for long enough, and really back and forth.
    return since && now - since >= SHAKE_WAKE_HOLD_MS && jolts >= 6 && swings >= SHAKE_WAKE_SWINGS;
  }

 private:
  ShakeDetector det;
  float grav[3] = {0, 0, 0};
  bool seeded = false;
  uint32_t lastAt = 0, since = 0, lastOnAt = 0, lastSwingAt = 0;
  uint8_t swings = 0;
  float swingDir[3] = {0, 0, 0};
};
