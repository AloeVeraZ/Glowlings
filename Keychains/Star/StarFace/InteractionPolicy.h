#pragma once
#include "CreatureAnimator.h"
#include "TouchTracker.h"

inline Mood tapMood(unsigned streak) {
  return streak >= 3 ? Mood::ANGRY : streak == 2 ? Mood::SURPRISED : Mood::BOOP;
}

// Screen taps only; case knocks cannot contribute to anger or battery readout.
struct ScreenTapRun {
  uint8_t count = 0;
  uint32_t lastAt = 0;
  uint8_t add(uint32_t now) {
    count = count && now - lastAt <= TOUCH_TAP_RUN_GAP_MS ?
            (count < TOUCH_BATTERY_TAPS ? count + 1 : TOUCH_BATTERY_TAPS) : 1;
    lastAt = now;
    return count;
  }
};

// Keep the existing closing choreography, with the display off at the deadline.
inline uint32_t sleepAnimationDelay(uint32_t timeout) {
  return timeout > SLEEP_SEQUENCE_MS ? timeout - SLEEP_SEQUENCE_MS : 0;
}

// A wake grants 30 seconds when untouched. Each confirmed touch sets an exact
// 15-second inactivity window; motion/animations/IRQ noise cannot extend it.
// Unsigned subtraction handles millis wrap even across repeated renewals.
struct AwakeLimit {
  uint32_t startedAt = 0;
  uint32_t timeout = WAKE_AWAKE_MS;
  bool active = false, alwaysOn = false;
  void start(uint32_t now) { startedAt = now; timeout = WAKE_AWAKE_MS; active = true; }
  void interaction(uint32_t now, bool confirmedTouch = true) {
    if (active && confirmedTouch) {
      startedAt = now;
      timeout = IDLE_SLEEP_MS;
    }
  }
  void finish() { active = false; }
  bool closingDue(uint32_t now) const {
    return active && !alwaysOn && now - startedAt >= sleepAnimationDelay(timeout);
  }
  uint32_t closingAt() const { return startedAt + sleepAnimationDelay(timeout); }
};

// Tap, then hold the second press. Presence must stay confirmed by TouchTracker;
// single holds, ordinary double taps, drags and missing contact never toggle.
struct AlwaysOnGesture {
  static constexpr uint32_t GAP_MS=TOUCH_CENTER_GAP_MS, HOLD_MS=850;
  bool doubleTapped=false; // second short release; a held second press only toggles the mode
  bool haveTap=false, candidate=false, latched=false;
  uint32_t tapAt=0,pressAt=0;
  int tapX=0,tapY=0,pressX=0,pressY=0;
  bool reserved() const { return candidate || latched; }
  bool feed(const TouchTracker::Result &r,uint32_t now,bool allowed=true) {
    doubleTapped=false;
    bool consumed=reserved();
    if(!allowed) { haveTap=candidate=latched=false; return false; }
    if(r.kind==TouchTracker::PRESS) {
      int dx=r.x-tapX,dy=r.y-tapY;
      candidate=haveTap && now-tapAt<=GAP_MS && dx*dx+dy*dy<=30*30;
      latched=false; haveTap=false; pressAt=now;pressX=r.x;pressY=r.y;
      return candidate;
    }
    if(r.kind==TouchTracker::MOVE && candidate) {
      int dx=r.x-pressX,dy=r.y-pressY;
      if(dx*dx+dy*dy>20*20) candidate=false;
    }
    if(r.kind==TouchTracker::TAP || r.kind==TouchTracker::RELEASE || r.kind==TouchTracker::SWIPE) {
      bool wasLatched=latched;
      int dx=r.x-tapX,dy=r.y-tapY;
      // TouchTracker can also confirm a completed quick tap whose press IRQ
      // was missed. It must still be near the preceding tap and within time.
      bool pairedTap=(candidate && now-pressAt<=TAP_MAX_MS) ||
                     (haveTap && now-tapAt<=GAP_MS && dx*dx+dy*dy<=30*30);
      doubleTapped=pairedTap && !wasLatched && r.kind==TouchTracker::TAP;
      candidate=latched=false;
      haveTap=r.kind==TouchTracker::TAP && !wasLatched;
      if(haveTap) {tapAt=now;tapX=r.x;tapY=r.y;}
      return wasLatched;
    }
    return consumed || reserved();
  }
  bool poll(bool tracking,uint32_t now) {
    if(!tracking) {candidate=latched=false;return false;}
    if(!candidate || latched || now-pressAt<HOLD_MS) return false;
    candidate=false;latched=true;haveTap=false;return true;
  }
};

inline bool confirmedTouchReport(uint8_t points, uint8_t event, uint8_t gesture,
                                 bool wasDown, bool validCoordinates) {
  if (points > 1 || event > 2) return false;
  if (points == 1 && event != 1) return true;
  bool completedGesture = (gesture >= 1 && gesture <= 5) || gesture == 0x0B || gesture == 0x0C;
  return event == 1 && (wasDown || (validCoordinates && completedGesture));
}

// A newly detected cable connection can rescue a visible closing sequence.
// A steady charging state never keeps renewing the inactivity deadline.
inline bool cancelClosingForCharge(AwakeLimit &limit, CreatureAnimator &creature,
                                  bool &preparing, bool &deepPlanned, bool connected, uint32_t now) {
  if (!connected || !preparing || !limit.active) return false;
  preparing = deepPlanned = false;
  limit.start(now);
  creature.startWake(now, false);
  return true;
}

inline bool cancelClosingForActivity(AwakeLimit &limit, CreatureAnimator &creature,
                                     bool &preparing, bool &deepPlanned, bool critical, uint32_t now,
                                     bool confirmedTouch = true) {
  if (critical || !confirmedTouch) return false;
  limit.interaction(now);
  if (!preparing || !limit.active) return false;
  preparing = deepPlanned = false;
  if (creature.asleep()) creature.react(Mood::SURPRISED, now, 900);
  return true;
}

// The first screen press woke the CPU. Count only distinct subsequent presses
// separated by a release; contact reports, IRQ pulses and double-click gesture
// codes cannot multiply one touch. Unknown I2C readings never count as release.
struct TouchWakeCheck {
  uint8_t count = 1;
  bool down = true;
  uint32_t lastPressAt = 0, releasedAt = 0;
  void begin(uint32_t now, uint8_t fingers) {
    count = 1;
    down = fingers != 0;
    lastPressAt = releasedAt = now;
  }
  bool complete() const { return count >= TOUCH_WAKE_TAPS; }
  bool feed(uint8_t fingers, uint32_t now) {
    if (fingers == 0xFF) return false;
    if (fingers == 0) {
      if (down) {
        down = false;
        releasedAt = now;
        if (now - lastPressAt > TAP_MAX_MS) count = 0;
      }
    } else if (!down && now - releasedAt >= 35 && now - lastPressAt >= 60) {
      count = count && now - lastPressAt <= TOUCH_TAP_RUN_GAP_MS ? count + 1 : 1;
      lastPressAt = now;
      down = true;
      return count >= TOUCH_WAKE_TAPS;
    }
    return false;
  }
};
