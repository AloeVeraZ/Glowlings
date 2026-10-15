#pragma once
#include <stdint.h>

// Sensor context edges, never an awake-time source. Sustained conditions do
// not replay a performance: motion/rest are debounced and charge is bucketed.
class EnvironmentObserver {
 public:
  enum Event : uint8_t { NONE, MOVED, SETTLED, WALK_STARTED, WALK_STOPPED, HELD_UP, CHARGE_DROPPED, LONG_REST };
  Event feed(float motion, uint32_t stillMs, bool walking, bool attending,
             uint8_t fatigueStep, uint32_t now) {
    Event event = NONE;
    if (!seeded) {
      seeded = true; wasWalking = walking; wasAttending = attending; lastFatigue = fatigueStep;
    }
    auto note = [&](Event e) { if (event == NONE) event = e; };
    if (fatigueStep > lastFatigue) note(CHARGE_DROPPED);
    lastFatigue = fatigueStep;
    if (walking != wasWalking) note(walking ? WALK_STARTED : WALK_STOPPED);
    wasWalking = walking;
    if (attending && !wasAttending && !walking) note(HELD_UP);
    wasAttending = attending;

    if (motion > .28f && !walking) {
      quietSince = 0;
      if (!motionSince) motionSince = now;
      if (!moving && now-motionSince >= 160) {
        moving = hadMovement = true; note(MOVED);
      }
    } else if (motion < .08f) {
      motionSince = 0;
      if (!quietSince) quietSince = now;
      if (moving && now-quietSince >= 600) {
        moving = false;
        if (!walking) { note(SETTLED); hadMovement = false; }
      }
    } else { motionSince = quietSince = 0; }
    if (walking) { moving = false; motionSince = quietSince = 0; }
    if (hadMovement && stillMs > 1000 && !walking && motion < .08f) {
      note(SETTLED); hadMovement = moving = false;
    }
    if (stillMs < 200) longRestNoted = false;
    if (stillMs >= 18000 && !longRestNoted && !walking && !attending) {
      note(LONG_REST); longRestNoted = true;
    }
    return event;
  }
 private:
  bool seeded = false, wasWalking = false, wasAttending = false;
  bool moving = false, hadMovement = false, longRestNoted = false;
  uint8_t lastFatigue = 0;
  uint32_t motionSince = 0, quietSince = 0;
};
