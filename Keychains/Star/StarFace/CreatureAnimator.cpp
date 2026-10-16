#include "CreatureAnimator.h"
#include "ChargeFlow.h"

using namespace anim;

namespace {

constexpr float PX = SCREEN_MIN_SIDE / 240.0f;   // offsets below are authored at 240 px

// ---- Wake-up choreography. Each key sets new targets; the eyes' springs do the motion. ----
struct WakeKey {
  uint16_t t;                  // ms from wake start (scaled per wake)
  int8_t leftOpen, rightOpen;  // percent
  int8_t gazeX, gazeY;         // percent
  uint8_t flags;
};
enum : uint8_t { WK_SLOW = 1, WK_FAST = 2, WK_BLINK = 4, WK_SQUINT = 8 };

const WakeKey WAKE_CLASSIC[] = {
  {0, 2, 2, 0, 0, 0},                  // screen lights with the eyes shut
  {420, 30, 3, 0, 0, WK_SLOW},         // one eyelid cracks open
  {820, 32, 4, 0, 0, 0},               // ...pause
  {1050, 96, 92, 0, 6, WK_SLOW},       // both eyes slowly open
  {1800, 100, 98, -45, -6, 0},         // look a little left
  {2200, 100, 100, 52, -6, WK_FAST},   // quick look right
  {2550, 100, 100, 0, 0, 0},           // back to the viewer
  {2850, 100, 100, 0, 0, WK_BLINK},    // small blink
  {3200, 100, 100, 0, 0, 0},
};
const WakeKey WAKE_ONE_EYE[] = {
  {0, 2, 2, 0, 0, 0},
  {350, 45, 2, -15, 0, WK_SLOW},       // one eye first
  {820, 60, 4, -25, 5, 0},
  {1150, 100, 52, 10, -8, WK_SQUINT},  // confused squint
  {1800, 100, 70, 35, -10, 0},
  {2200, 100, 100, 0, 0, WK_SLOW},
  {2450, 100, 100, 0, 0, WK_BLINK},
  {2800, 100, 100, 0, 0, 0},
};
const WakeKey WAKE_STARTLED[] = {
  {0, 2, 2, 0, 0, 0},
  {200, 116, 116, 0, 0, WK_FAST},      // pops wide open
  {470, 108, 108, -55, 0, WK_FAST},
  {690, 106, 106, 55, -5, WK_FAST},
  {920, 100, 100, 0, 0, 0},
  {1180, 100, 100, 0, 0, WK_BLINK},
  {1500, 100, 100, 0, 0, 0},
};
const WakeKey WAKE_SLEEPY[] = {
  {0, 2, 2, 0, 10, 0},
  {450, 42, 36, 0, 15, WK_SLOW},
  {1150, 22, 17, 0, 15, WK_SLOW},      // nods off again for a moment
  {1700, 70, 64, -20, 10, WK_SLOW},
  {2400, 100, 98, -35, 0, WK_SLOW},
  {2900, 100, 100, 25, -4, 0},
  {3300, 100, 100, 0, 0, WK_BLINK},
  {3650, 100, 100, 0, 0, 0},
};
struct WakeScript { const WakeKey *keys; uint8_t count; };
#define WAKE_ENTRY(a) {a, uint8_t(sizeof(a) / sizeof(a[0]))}
const WakeScript WAKE_SCRIPTS[] = {
  WAKE_ENTRY(WAKE_CLASSIC), WAKE_ENTRY(WAKE_ONE_EYE),
  WAKE_ENTRY(WAKE_STARTLED), WAKE_ENTRY(WAKE_SLEEPY)
};
constexpr uint8_t WAKE_STARTLED_INDEX = 2;

// Sleep sequence landmarks, ms.
constexpr int32_t SLEEP_BLINK_AT = 1400;
constexpr int32_t SLEEP_DROOP_AT = 2900;
constexpr int32_t SLEEP_CLOSE_AT = 3500;
constexpr int32_t SLEEP_FADE_AT = 4700;
constexpr int32_t SLEEP_FADE_MS = SLEEP_SEQUENCE_MS - SLEEP_FADE_AT;

constexpr float INERTIA_GAIN = 220.0f * PX; // px/s^2 of eye push per m/s^2 of device acceleration

uint8_t priorityOf(Mood m) {
  switch (m) {
    case Mood::DIZZY: return 6;
    case Mood::SURPRISED: return 5;
    case Mood::ANGRY: case Mood::BOOP: case Mood::ANXIOUS: case Mood::SWIPING: case Mood::FOLLOWING:
    case Mood::PETTED: case Mood::BATTERY: case Mood::LOVED: case Mood::UPSIDE_DOWN: return 4;
    case Mood::HAPPY: case Mood::SAD: case Mood::SHY: case Mood::CONFUSED: return 3;
    case Mood::CURIOUS: case Mood::YAWN: case Mood::LOOK_AROUND:
    case Mood::WINK: case Mood::PLAYFUL: case Mood::STRETCH:
    case Mood::PEEK: case Mood::GIGGLE: case Mood::SNEEZE: case Mood::SASSY: return 2;
    case Mood::SHIVER: case Mood::WAKE_UP: return 2;
    case Mood::SLEEPY: return 1;
    default: return 0;
  }
}

// The dizzy spin starts fast and winds down (radians).
float dizzyPhase(float t) {
  float s = fmaxf(0.0f, t - .35f);
  return TAU_F * (2.1f * s - .35f * s * s);
}

} // namespace

// ======================= setup & public API =======================

void CreatureAnimator::begin(Eyes *e, uint32_t unitSeed, uint32_t now) {
  eyes = e;
  seed = unitSeed;
  eyes->snapOpenness(.02f);   // starts shut; the wake script opens it
  blLevel = 0;
  current = previous = Mood::IDLE;
  moodStart = moodUntil = now;
  environment = EnvironmentObserver(); reactedToEnvironment = false;
}

void CreatureAnimator::setAlwaysOn(bool enabled,uint32_t now) {
  alwaysOn=enabled;
  queued=Mood::IDLE; pointerHeld=false; holdAnnoy=0;
  enterMood(enabled?Mood::ALWAYS_ON:Mood::AUTO_SLEEP,now,1800);
}

bool CreatureAnimator::activeAt(uint32_t now) const {
  return current != Mood::IDLE && (current == Mood::SLEEPY || int32_t(now - moodUntil) < 0);
}

bool CreatureAnimator::react(Mood m, uint32_t now, uint32_t durationMs, bool direct) {
  if (touchAngerPauseActive(now) || batteryDisplayActive(now)) return false;
  if(!direct && activeAt(now) && (current==Mood::ALWAYS_ON || current==Mood::AUTO_SLEEP)) return false;
  if (activeAt(now) && current == Mood::DIZZY) {
    if (m != Mood::DIZZY) return false; // the shake reaction always plays through
    if (age < 1600) return true;  // still spinning; impact() already added the jolt
  }
  if (!direct && activeAt(now) && priorityOf(m) < priorityOf(current)) return false;
  queued = Mood::IDLE;
  enterMood(m, now, durationMs);
  return true;
}

void CreatureAnimator::huff(float amount, uint32_t now) {
  if (touchAngerPauseActive(now) || batteryDisplayActive(now)) return;
  amount = clamp01(amount);
  if (activeAt(now) && current == Mood::DIZZY) return;
  queued = Mood::IDLE;
  enterMood(Mood::ANGRY, now, 450 + uint32_t(900 * amount), .55f + .45f * amount);
  angerResidue = amount;
}

void CreatureAnimator::startTouchAngerPause(uint32_t now) {
  if (batteryDisplayActive(now)) return;
  touchAngerProtected = true;
  touchAngerSince = now;
  queued = Mood::IDLE;
  pointerHeld = false;
  shakeLevel = inertiaX = inertiaY = 0;
  enterMood(Mood::ANGRY, now, TOUCH_ANGER_PAUSE_MS);
}

bool CreatureAnimator::reactPassive(Mood m, uint32_t now, uint32_t durationMs) {
  if (activeAt(now)) return false;
  enterMood(m, now, durationMs, frand(.88f, 1.04f) * (1-.25f*batteryFatigue));
  return true;
}

bool CreatureAnimator::sustain(Mood m, uint32_t now, uint32_t durationMs) {
  if (current != m || !activeAt(now)) return false;
  if (int32_t(moodUntil - (now + durationMs)) < 0) moodUntil = now + durationMs;
  return true;
}

void CreatureAnimator::queue(Mood m, uint32_t at, uint32_t durationMs, int param) {
  queued = m;
  queuedAt = at;
  queuedDuration = durationMs;
  queuedParam = param;
}

void CreatureAnimator::showBattery(int percent, uint32_t now) {
  touchAngerProtected = false; // ten taps may finish the five-tap run with a readout
  pointerHeld = false;
  batteryDisplayPercent = percent < 0 ? -1 : constrain(percent, 0, 100);
  bool resume = activeAt(now) && current != Mood::SWIPING && current != Mood::BATTERY &&
                current != Mood::SLEEPY && current != Mood::WAKE_UP && current != Mood::DIZZY;
  batteryReturn = resume ? current : Mood::IDLE;
  batteryReturnRemaining = resume ? moodUntil - now : 0;
  queued = Mood::IDLE;
  enterMood(Mood::BATTERY, now, BATTERY_SHOW_MS);
}

void CreatureAnimator::startWake(uint32_t now, bool fromShake, bool touched) {
  touchAngerProtected = false;
  if (fromShake) {
    wakeVariant = WAKE_STARTLED_INDEX;
    wakeScale = .8f;
  } else {
    // A touch tends to startle; otherwise it wakes in one of several ways.
    float r = frand();
    if (touched) wakeVariant = r < .4f ? WAKE_STARTLED_INDEX : r < .7f ? 0 : r < .85f ? 1 : 3;
    else wakeVariant = r < .45f ? 0 : r < .65f ? 1 : r < .80f ? 2 : 3;
    wakeScale = frand(.88f, 1.15f);
  }
  wakeGazeScale = frand(.8f, 1.2f) * (chance(.5f) ? 1.0f : -1.0f);
  wakeSwap = chance(.5f);
  // Continue from wherever the lids are; only a fully dark start snaps shut.
  if (blLevel <= .01f) eyes->snapOpenness(.02f);
  napLevel = 0;
  const WakeScript &s = WAKE_SCRIPTS[wakeVariant];
  uint32_t duration = uint32_t(s.keys[s.count - 1].t * wakeScale);
  queued = Mood::IDLE;
  enterMood(Mood::WAKE_UP, now, duration);
  if (fromShake) queue(Mood::DIZZY, moodUntil, DIZZY_ANIM_MS);
}

void CreatureAnimator::startSleep(uint32_t now, float nap) {
  touchAngerProtected = false; // battery shutdown or inactivity sleep takes precedence
  queued = Mood::IDLE;
  napLevel = clamp01(nap);
  nextPeekAt = now + SLEEP_SEQUENCE_MS + randMs(9000, 20000);
  peekUntil = 0;
  enterMood(Mood::SLEEPY, now, SLEEP_SEQUENCE_MS);
}

bool CreatureAnimator::sleepFinished(uint32_t now) const {
  return current == Mood::SLEEPY && now - moodStart >= SLEEP_SEQUENCE_MS;
}

void CreatureAnimator::setPointer(float x, float y, uint32_t now) {
  if (touchAngerPauseActive(now)) return;
  pointerX = clampf(x, -1.0f, 1.0f);
  pointerY = clampf(y, -1.0f, 1.0f);
  pointerAt = now;
}

void CreatureAnimator::applyInertia(float ax, float ay) {
  // Soft dead band: sensor noise and gentle handling leave the eyes alone.
  auto band = [](float v) {
    float a = fabsf(v) - .7f;
    return a <= 0 ? 0.0f : copysignf(fminf(a, 20.0f), v);
  };
  inertiaX = band(ax);
  inertiaY = band(ay);
}

void CreatureAnimator::notice(float level, uint32_t now) {
  if (now - lastNoticeAt < 1200 || current == Mood::SLEEPY || current == Mood::WAKE_UP) return;
  lastNoticeAt = now;
  level = clampf(level, .2f, 1.0f);
  for (int i = 0; i < 2; ++i) {
    eyes->kickShape(i, ES_HEIGHT, 1.4f * level);   // the eyes pop open a touch...
    eyes->kickShape(i, ES_PUPIL, -2.0f * level);   // ...and the pupils tighten, then settle
  }
  if (chance(.35f)) eyes->blink(Blinker::FAST);
}

void CreatureAnimator::setCarried(bool walk, bool run, bool step, float stepStrength) {
  walking = walk;
  running = run;
  // Each footstep bobs the eyes, a little more for a heavier step.
  if (step && current != Mood::SLEEPY && current != Mood::DIZZY && !touchAngerProtected)
    eyes->push(0, (12.0f + 10.0f * clampf(stepStrength / 4.0f, 0.0f, 1.5f)) * PX);
}

void CreatureAnimator::impact(float ax, float ay) {
  float mag = sqrtf(ax * ax + ay * ay);
  if (mag < .01f) return;
  impactX = ax / mag;
  impactY = ay / mag;
  float strength = clampf(mag / 15.0f, .4f, 1.6f) * 220.0f * PX;
  eyes->push(-impactX * strength, -impactY * strength);
}

void CreatureAnimator::enterMood(Mood m, uint32_t now, uint32_t durationMs, float strength) {
  previous = current;
  current = m;
  // Anger never just switches off: the lids stay a little heavy, cooling over ANGER_COOLDOWN_S.
  if (previous == Mood::ANGRY && m != Mood::ANGRY) grumpy = fmaxf(grumpy, angerResidue);
  moodStart = now;
  moodUntil = now + durationMs;
  age = 0;
  prevAge = -1;
  intensity = frand(.88f, 1.08f) * strength;
  switch (m) {
    case Mood::BOOP: {
      // The eye nearer the finger flinches first; the other follows in updateBody.
      int nearEye = touchX < SCREEN_CX ? 0 : 1;
      boopFar = 1 - nearEye;
      eyes->kickShape(nearEye, ES_HEIGHT, -5.0f);
      eyes->kickShape(nearEye, ES_WIDTH, 1.6f);
      float dx = clampf((touchX - SCREEN_CX) / (100.0f * PX), -1.0f, 1.0f);
      float dy = clampf((touchY - SCREEN_CY) / (100.0f * PX), -1.0f, 1.0f);
      eyes->push(-dx * 90.0f * PX, -dy * 70.0f * PX);
      break;
    }
    case Mood::DIZZY:
      // The hit: a squash and a small extra recoil on top of impact().
      for (int i = 0; i < 2; ++i) {
        eyes->kickShape(i, ES_HEIGHT, -2.2f);
        eyes->kickShape(i, ES_WIDTH, 1.4f);
      }
      eyes->push(-impactX * 120.0f * PX, -impactY * 120.0f * PX);
      queue(Mood::ANGRY, now + DIZZY_ANIM_MS - 50, ANGRY_ANIM_MS);
      break;
    case Mood::ANGRY:
      nextAngrySquint = now + randMs(700, 1300);
      angrySquintUntil = 0;
      angerResidue = 1;
      break;
    case Mood::ANXIOUS:
      nextDartAt = now;
      break;
    case Mood::CURIOUS: case Mood::LOOK_AROUND:
      lookSide = chance(.5f) ? 1.0f : -1.0f;
      break;
    case Mood::PEEK: case Mood::GIGGLE: case Mood::SNEEZE: case Mood::SASSY:
      lookSide = chance(.5f) ? 1.0f : -1.0f;
      break;
    case Mood::WINK:
      lookSide = chance(.5f) ? 1.0f : -1.0f;
      eyes->wink(lookSide > 0 ? 1 : 0);
      eyes->push(-lookSide * 35 * PX, -22 * PX);
      break;
    case Mood::PLAYFUL:
      lookSide = chance(.5f) ? 1.0f : -1.0f;
      eyes->push(lookSide * 45 * PX, -90 * PX);
      break;
    case Mood::STRETCH:
      for (int i = 0; i < 2; ++i) {
        eyes->kickShape(i, ES_HEIGHT, .9f);
        eyes->kickShape(i, ES_WIDTH, -.35f);
      }
      break;
    case Mood::SURPRISED:
      eyes->push(0, -45.0f * PX);
      break;
    case Mood::HAPPY: case Mood::PETTED: case Mood::LOVED:
      eyes->push(0, -70.0f * PX);  // a little hop
      break;
    default:
      break;
  }
}

// ======================= frame update =======================

void CreatureAnimator::update(float dt, uint32_t now) {
  dt = clampf(dt, .0005f, .12f);
  clock += dt;
  // Battery mood is a persistent layer; event choreography still plays as authored.
  batteryFatigue = approach(batteryFatigue, battery.fatigue(), 1.2f, dt);
  batterySadness = approach(batterySadness, battery.sadness(), 1.2f, dt);
  float phase=clock/3.2f;
  if(chargeWasOn && !battery.charging) {
    chargeStopPhase=(clock-dt)/3.2f;
    chargeDrainUntil=faceCharge>.01f?ChargeFlow::finishPhase(chargeStopPhase):phase;
  }
  chargeWasOn=battery.charging;
  bool visualCharge=battery.charging || finishingCharge();
  uint32_t c = visualCharge ? BATTERY_CHARGING_COLOR : battery.color();
  float colorSpeed = battery.charging ? 4.0f : 3.0f;
  colorR = approach(colorR, float((c >> 16) & 255), colorSpeed, dt);
  colorG = approach(colorG, float((c >> 8) & 255), colorSpeed, dt);
  colorB = approach(colorB, float(c & 255), colorSpeed, dt);
  eyes->setEyeColor((uint32_t(colorR + .5f) << 16) | (uint32_t(colorG + .5f) << 8) | uint32_t(colorB + .5f));
  updateMoodTimeline(now);
  updateFace(dt, now);
  float morph = 0, swirl = 0;
  if (current == Mood::BATTERY) {
    float entering = easeInOut(age / float(BATTERY_MORPH_MS));
    float leaving = easeInOut((age - int32_t(BATTERY_SHOW_MS - BATTERY_MORPH_MS)) / float(BATTERY_MORPH_MS));
    morph = entering * (1.0f - leaving);
    swirl = 0; // a quick, clean transition into the familiar battery icon
  }
  float chargePhase = current == Mood::BATTERY ? (uint32_t(age) % 1400) / 1400.0f : 0;
  // The readout reports the live state immediately; the eyes keep their soft color fade.
  eyes->setBatteryDisplay(batteryDisplayPercent, morph, swirl, battery.charging, chargePhase, battery.color());
  faceCharge = approach(faceCharge, visualCharge ? 1.0f : 0.0f, visualCharge ? 5.0f : 3.0f, dt);
  float closingFade = current == Mood::SLEEPY ? 1.0f - smoothstep(0.0f, float(SLEEP_SEQUENCE_MS), float(age)) : 1.0f;
  eyes->setFaceCharging(visualCharge ? faceCharge * closingFade * (1.0f - morph) : 0, phase,
                        battery.charging ? -1 : chargeStopPhase);
  float cue=(current==Mood::ALWAYS_ON || current==Mood::AUTO_SLEEP)?1-smoothstep(900,1700,float(age)):0;
  eyes->setAwakeMode(alwaysOn && current!=Mood::SLEEPY, cue*closingFade);
  updateGaze(now);
  updateBody();
  updateEffects(dt);
  updateBacklight(dt);
  eyes->update(dt, now);
  prevAge = age;
}

void CreatureAnimator::updateMoodTimeline(uint32_t now) {
  if (touchAngerProtected && !touchAngerPauseActive(now)) touchAngerProtected = false;
  if (queued != Mood::IDLE && int32_t(now - queuedAt) >= 0) {
    Mood m = queued;
    queued = Mood::IDLE;
    if (m == Mood::BATTERY) showBattery(queuedParam, now);
    else enterMood(m, now, queuedDuration);
  }
  // Following a finger lasts as long as the finger stays down.
  if (current == Mood::FOLLOWING && pointerHeld && int32_t(moodUntil - now) < 250) moodUntil = now + 250;
  if (current == Mood::UPSIDE_DOWN && upsideDown && int32_t(moodUntil - now) < 250) moodUntil = now + 250;
  // Held on by a finger, or held upside down: annoyance builds the longer it lasts.
  auto build = [](uint32_t heldMs) {
    return smoothstep(float(HOLD_ANGER_START_MS), float(HOLD_ANGER_FULL_MS), float(heldMs));
  };
  holdAnnoy = fmaxf(pointerHeld ? build(now - holdSince) : 0.0f,
                    upsideDown ? build(now - upsideSince) : 0.0f);
  if (current != Mood::IDLE && current != Mood::SLEEPY && int32_t(now - moodUntil) >= 0) {
    if (current == Mood::BATTERY && batteryReturnRemaining) {
      uint32_t remaining = batteryReturnRemaining;
      batteryReturnRemaining = 0;
      enterMood(batteryReturn, now, remaining);
    } else {
      enterMood(Mood::IDLE, now, 0);
    }
  }
  age = int32_t(now - moodStart);
  const bool attending = heldUp && now-heldSince > 1200;
  const bool bored = stillMs > 18000;
  auto event = environment.feed(motionLevel,stillMs,walking,attending,battery.fatigueStep,now);
  // Look around normally; only real context transitions launch larger scenes.
  // Drop events during touch/wake/sleep instead of queuing a stale show later.
  if (event != EnvironmentObserver::NONE && current == Mood::IDLE && idleActsAllowed &&
      (!reactedToEnvironment || now-lastEnvironmentAt >= 4500)) {
    reactedToEnvironment = true; lastEnvironmentAt = now;
    switch (event) {
      case EnvironmentObserver::MOVED: reactPassive(Mood::CURIOUS,now,1300); break;
      case EnvironmentObserver::WALK_STARTED: reactPassive(Mood::LOOK_AROUND,now,2000); break;
      case EnvironmentObserver::WALK_STOPPED: reactPassive(Mood::CURIOUS,now,1100); break;
      case EnvironmentObserver::CHARGE_DROPPED: reactPassive(Mood::YAWN,now,1900); break;
      case EnvironmentObserver::LONG_REST: reactPassive(Mood::LOOK_AROUND,now,2300); break;
      case EnvironmentObserver::SETTLED: eyes->blink(Blinker::NORMAL); break;
      case EnvironmentObserver::HELD_UP:
        eyes->blink(Blinker::UNEVEN); eyes->lookAt(0,0,.9f); break;
      default: break;
    }
  }
  eyes->setLiveliness(IDLE_LIVELINESS * (walking ? 1.2f : 1.0f) * (bored ? 1.05f : 1.0f)
                     * (1-.75f*batteryFatigue),attending ? .65f : 0);

}

// ---- Expression, lids and blinks for the current mood ----

void CreatureAnimator::updateFace(float dt, uint32_t now) {
  grumpy = approach(grumpy, 0.0f, 3.0f / ANGER_COOLDOWN_S, dt);
  Expression e = NEUTRAL, b = NEUTRAL;
  float t = 0, k = intensity, speed = 1;
  float open[2] = {1, 1}, openSpeed = 1;
  bool blinkOK = true, scripted = false;
  float pace = 1;
  const float sec = age / 1000.0f;
  for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,1,1);

  switch (current) {
    case Mood::IDLE:
      k = 1;
      if (batteryFatigue > .01f) { b = SLEEPY; t = batteryFatigue; }
      else if (grumpy > .01f) { b = ANNOYED; t = grumpy; } // still sulking after anger
      else if (running) { b = SURPRISED; t = .45f; }  // whee: wide-eyed
      else if (walking) { b = HAPPY; t = .18f; }      // out and about: smiling eyes
      else if (heldUp && now - heldSince > 1200) { b = HAPPY; t = .10f; } // looking at you, softly
      break;
    case Mood::HAPPY:
      e = HAPPY; pace = .9f; speed = 1.35f;
      for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,1.04f,.94f+.05f*sinf(sec*TAU_F*2.2f+i*.5f));
      break;
    case Mood::SAD: e = SAD; pace = 1.35f; break;
    case Mood::ANGRY:
      if (age < 110) { e = SQUINT; k = .4f; speed = 2; }   // anticipation
      else e = ANGRY;
      if (crossed(110)) { // impact: lids slam, the eyes drop a hair
        for (int i = 0; i < 2; ++i) eyes->kickShape(i, ES_TOP, 2.0f);
        eyes->push(0, 30.0f * PX);
      }
      if (age >= 110 && int32_t(now - nextAngrySquint) >= 0) {
        angrySquintUntil = now + randMs(180, 280);
        nextAngrySquint = now + randMs(900, 1700);
      }
      if (int32_t(now - angrySquintUntil) < 0) { open[0] = .82f; open[1] = .80f; openSpeed = 2; }
      pace = 1.25f;
      break;
    case Mood::DIZZY:
      if (sec < 2.0f) {
        e = DIZZY;
        open[0] = .90f + .07f * sinf(dizzyPhase(sec) * .5f);
        open[1] = .96f + .06f * sinf(dizzyPhase(sec) * .5f + 2);
      } else {
        e = ANNOYED; k = .6f;    // recovering, and already a little cross
      }
      blinkOK = false;
      break;
    case Mood::SURPRISED:
      if (age < 100) {
        e = SQUINT; k = .65f; speed = 2.5f;
        for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,1.13f,.72f);
      } else {
        e = age < 700 ? SURPRISED : CURIOUS; speed = 1.7f;
        for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,age < 280 ? .92f : 1,age < 280 ? 1.16f : 1);
      }
      blinkOK = age > 700;
      break;
    case Mood::ANXIOUS:
      e = WORRIED;
      open[0] = open[1] = .92f + .07f * sinf(TAU_F * 5 * sec);
      openSpeed = 2;
      pace = .45f;
      break;
    case Mood::BOOP:
      blinkOK = false; speed = 2; openSpeed = 2;
      if (age < 130) {
        e = SQUINT; k = .9f;
        for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,1.12f,i == boopFar ? .9f : .64f);
      } else if (age < 350) {
        e = SURPRISED; k = .55f; open[0] = 1.08f; open[1] = 1.13f;
      } else { e = NEUTRAL; b = HAPPY; t = .32f; }
      if (crossed(420)) eyes->wink(boopFar);
      break;
    case Mood::PETTED:
      e = HAPPY; k *= 1.05f; pace = 1.3f;
      for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,1.04f,.95f+.06f*sinf(sec*TAU_F*1.4f+i*.7f));
      break;
    case Mood::LOVED: e = LOVE; break;
    case Mood::SWIPING:
      // The trailing eye compresses slightly as the pair is pulled.
      if (age < 260) {
        if (swipeX > 0) open[0] = .8f;
        else if (swipeX < 0) open[1] = .8f;
        else open[0] = open[1] = .9f;
        openSpeed = 2;
      }
      blinkOK = false;
      break;
    case Mood::FOLLOWING:
      // Interested at first; held on, it glares harder and harder at the finger.
      e = NEUTRAL; b = ANGRY; t = holdAnnoy; k = 1;
      break;
    case Mood::CONFUSED: e = CONFUSED; break;
    case Mood::WINK:
      e = NEUTRAL; b = HAPPY; t = .12f; speed = 1.4f;
      break;
    case Mood::PLAYFUL:
      e = age < 180 ? SQUINT : HAPPY; speed = 1.8f;
      for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,1.06f,.91f+.10f*sinf(sec*TAU_F*2+i*.8f),.05f*sinf(sec*TAU_F*2));
      if (crossed(550)) eyes->wink(lookSide > 0 ? 1 : 0);
      break;
    case Mood::STRETCH:
      e = age < 1000 ? SURPRISED : NEUTRAL;
      k = .55f; speed = age < 1000 ? .7f : 1.3f;
      open[0] = open[1] = 1 + .10f * sinf(PI_F * clamp01(sec / 1.7f));
      blinkOK = age > 1100;
      if (crossed(1200)) eyes->blink(Blinker::FAST);
      for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,.96f,1+.14f*sinf(PI_F*clamp01(sec/1.7f)));
      break;
    case Mood::PEEK: {
      int peeking = lookSide > 0 ? 1 : 0;
      blinkOK = false; openSpeed = 2; speed = 1.6f;
      e = age < 180 ? SQUINT : age < 1000 ? CURIOUS : NEUTRAL;
      open[0] = open[1] = age < 180 ? .16f : 1;
      if (age >= 180 && age < 620) { open[peeking] = 1.10f; open[1-peeking] = .10f; }
      eyes->setShapeAccent(peeking,.98f,1.10f,.08f);
      eyes->setShapeAccent(1-peeking,1.08f,.85f,-.05f);
      if (crossed(1100)) eyes->blink(Blinker::FAST);
      break;
    }
    case Mood::GIGGLE: {
      blinkOK = false; speed = 1.8f;
      e = age < 150 ? NEUTRAL : age < 1250 ? HAPPY : NEUTRAL;
      float pulse = fmaxf(0,sinf(TAU_F*3.0f*(sec-.15f))) * (1-smoothstep(.95f,1.3f,sec));
      for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,1+.10f*pulse,1-.14f*pulse,.035f*sinf(sec*TAU_F*3+i));
      if (crossed(220) || crossed(520) || crossed(820)) eyes->push(lookSide*22*PX,-110*PX);
      if (crossed(1300)) eyes->blink(Blinker::FAST);
      break;
    }
    case Mood::SNEEZE:
      blinkOK = false; speed = 2.2f; openSpeed = 2;
      if (age < 260) {
        e = SQUINT; open[0] = .5f; open[1] = .4f;
        for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,1.12f,.72f);
      } else if (age < 460) {
        e = SURPRISED; open[0] = open[1] = 1.16f;
        for (int i = 0; i < 2; ++i) eyes->setShapeAccent(i,.89f,1.18f);
      } else if (age < 650) { e = SQUINT; open[0] = open[1] = .15f; }
      else { e = CONFUSED; k = .65f; }
      if (crossed(260)) eyes->push(0,-190*PX);
      if (crossed(460)) eyes->push(0,130*PX);
      if (crossed(850)) eyes->blink(Blinker::FAST);
      break;
    case Mood::SASSY: {
      blinkOK = false; speed = 1.9f; openSpeed = 1.8f;
      e = age < 180 ? CURIOUS : age < 1000 ? ANNOYED : NEUTRAL;
      int raised = lookSide > 0 ? 1 : 0;
      if (age >= 180 && age < 1000) {
        open[raised] = 1.12f; open[1-raised] = .63f;
        eyes->setShapeAccent(raised,.96f,1.09f,-.10f);
        eyes->setShapeAccent(1-raised,1.06f,.87f,.09f);
      }
      if (crossed(1120)) eyes->wink(1-raised);
      break;
    }
    case Mood::ALWAYS_ON:
      scripted=true;blinkOK=false;speed=2;openSpeed=2;
      if(age<160) { e=SQUINT;open[0]=open[1]=.35f; }
      else if(age<600) { e=SURPRISED;k=.5f;open[0]=open[1]=1.12f; }
      else { e=NEUTRAL;b=HAPPY;t=.20f; }
      if(crossed(620)) eyes->wink(0);
      if(crossed(1020)) eyes->wink(1);
      break;
    case Mood::AUTO_SLEEP:
      scripted=true;blinkOK=false;speed=1.5f;openSpeed=1.5f;
      e=NEUTRAL;b=SLEEPY;t=age<700?.35f:0;
      open[0]=open[1]=age<240?1:age<600?.08f:1;
      break;
    case Mood::UPSIDE_DOWN:
      // Worried at first, then cross, then furious (it trembles in updateEffects).
      e = WORRIED; b = ANGRY; t = holdAnnoy; k = 1;
      break;
    case Mood::SHY: e = WORRIED; k *= .7f; open[0] = .62f; open[1] = .52f; break;
    case Mood::SHIVER: e = SQUINT; k *= .8f; break;
    case Mood::CURIOUS: e = CURIOUS; break;
    case Mood::LOOK_AROUND: e = NEUTRAL; b = CURIOUS; t = .35f; break;
    case Mood::YAWN:
      // Lids get heavy, the eyes squeeze shut in a big yawn, then a slow blink.
      blinkOK = false;
      if (age < 450) { e = SLEEPY; k = .7f; open[0] = open[1] = .85f; openSpeed = .6f; }
      else if (age < 1250) { e = SQUINT; k = 1; open[0] = open[1] = .3f; openSpeed = .45f; }
      else { e = SLEEPY; k = .35f; openSpeed = .5f; }
      if (crossed(450)) for (int i = 0; i < 2; ++i) eyes->kickShape(i, ES_WIDTH, 1.2f);
      if (crossed(1500)) eyes->blink(Blinker::SLOW);
      break;
    case Mood::BATTERY: {
      scripted = true;
      blinkOK = false;
      k = 1;
      // Renderer warps these open eyes into one bar, then unwinds them.
      open[0] = open[1] = 1.0f;
      openSpeed = .8f;
      break;
    }
    case Mood::WAKE_UP:
      scripted = true;
      blinkOK = false;
      wakeScript(now, e, k, open, openSpeed);
      break;
    case Mood::SLEEPY:
      scripted = true;
      blinkOK = false;
      sleepScript(now, e, k, open, openSpeed);
      break;
  }

  // Being shaken: the eyes widen in alarm.
  if (current == Mood::IDLE && batteryFatigue > .01f) {
    open[0] = open[1] = 1.0f - .45f * batteryFatigue;
    openSpeed = .6f;
  }
  if (shakeLevel > .01f && !scripted) {
    float a = fminf(shakeLevel, 1.0f);
    open[0] *= 1.0f + .14f * a;
    open[1] *= 1.0f + .14f * a;
    if (e == NEUTRAL && b == NEUTRAL) { b = SURPRISED; t = .6f * a; }
  }
  eyes->setExpressionMix(e, b, t, k, speed);
  bool joyfulRest = current == Mood::IDLE || current == Mood::HAPPY || current == Mood::PETTED ||
                    current == Mood::CURIOUS || (current == Mood::FOLLOWING && holdAnnoy < .05f);
  eyes->setChargingJoy(joyfulRest && grumpy < .1f && shakeLevel < .1f ? faceCharge : 0);
  eyes->setOpenness(open[0], open[1], openSpeed);
  eyes->setBlinkAllowed(blinkOK);
  eyes->setBlinkPace(pace * (grumpy > .3f ? 1.2f : 1.0f) * (1.0f + .5f * batteryFatigue));
  eyes->setDrowsiness(scripted ? 0.0f : fmaxf(current == Mood::IDLE ? drowsy*(1-.7f*faceCharge) : 0.0f, batteryFatigue));
}

void CreatureAnimator::wakeScript(uint32_t now, Expression &e, float &k, float *open, float &openSpeed) {
  const WakeScript &s = WAKE_SCRIPTS[wakeVariant];
  int key = 0;
  for (int i = 0; i < s.count; ++i) {
    int32_t at = int32_t(s.keys[i].t * wakeScale);
    if (age >= at) key = i;
    if ((s.keys[i].flags & WK_BLINK) && crossed(at)) eyes->blink(Blinker::FAST);
  }
  const WakeKey &w = s.keys[key];
  float lo = w.leftOpen / 100.0f, ro = w.rightOpen / 100.0f;
  if (wakeSwap) { float tmp = lo; lo = ro; ro = tmp; }
  open[0] = lo;
  open[1] = ro;
  e = (w.flags & WK_SQUINT) ? CONFUSED : NEUTRAL;
  k = 1;
  scriptGX = w.gazeX / 100.0f * wakeGazeScale;
  scriptGY = w.gazeY / 100.0f;
  if (w.flags & WK_SLOW) { openSpeed = .28f; scriptGazeSpeed = .55f; }
  else if (w.flags & WK_FAST) { openSpeed = 1.7f; scriptGazeSpeed = 2.0f; }
  else { openSpeed = .65f; scriptGazeSpeed = 1.0f; }
}

void CreatureAnimator::sleepScript(uint32_t now, Expression &e, float &k, float *open, float &openSpeed) {
  float droop = smoothstep(0.0f, float(SLEEP_BLINK_AT), float(age));
  float o = age < SLEEP_BLINK_AT ? mix(1.0f, .75f, droop)   // lids droop
          : age < SLEEP_DROOP_AT ? .68f                       // reopen only partway
          : age < SLEEP_CLOSE_AT ? .45f                       // heavier
          : 0.0f;                                             // a last long blink that stays shut
  open[0] = open[1] = o;
  e = SLEEPY;
  k = droop;
  if (crossed(SLEEP_BLINK_AT)) eyes->blink(Blinker::SLOW);
  openSpeed = age < SLEEP_CLOSE_AT ? .22f : .17f;
  scriptGX = 0;
  scriptGY = .22f;
  scriptGazeSpeed = .25f;
  if (!napping()) return;

  // Napping with the screen still on: now and then a sleepy peek or a dream flutter.
  if (!peekUntil && int32_t(now - nextPeekAt) >= 0) {
    float r = frand();
    peekKind = r < .3f ? 1 : r < .6f ? 2 : r < .82f ? 3 : 4; // left, right, flutter, both
    peekUntil = now + (peekKind == 3 ? randMs(110, 180) : randMs(700, 1500));
    nextPeekAt = now + randMs(12000, 35000);
    scriptGX = frand(-.4f, .4f);
  }
  if (peekUntil && int32_t(now - peekUntil) < 0) {
    if (peekKind == 1) open[0] = .30f;
    else if (peekKind == 2) open[1] = .30f;
    else if (peekKind == 3) open[0] = open[1] = .12f;
    else open[0] = open[1] = .2f;
    openSpeed = peekKind == 3 ? 1.2f : .32f;
    scriptGY = .05f;
  } else {
    peekUntil = 0;
  }
}

// ---- Gaze: the pointer, mood-specific looks, or the eyes' own idle wandering ----

void CreatureAnimator::updateGaze(uint32_t now) {
  eyes->setGazeBias(touchAngerProtected ? 0 : tiltX, touchAngerProtected ? 0 : tiltY);
  // The face stays level and the eyes keep looking at you while it is swung
  // (not while waking or asleep).
  bool awake = current != Mood::SLEEPY && !(current == Mood::WAKE_UP && age < 1200);
  bool expressive = current != Mood::IDLE && current != Mood::SLEEPY && current != Mood::BATTERY;
  bool expand = awake && current != Mood::BATTERY;
  eyes->setPresence(expand ? fmaxf(motionLevel,expressive ? .75f : 0)*(1-.45f*batteryFatigue) : 0,expand);
  eyes->setFaceRoll(awake ? faceRoll : 0.0f);
  eyes->setLookOffset(awake && !touchAngerProtected ? swingX : 0.0f,
                      awake && !touchAngerProtected ? swingY : 0.0f);
  // Interested pupils: a little wider while something is going on.
  float attention = fminf(1.0f, sqrtf(tiltX * tiltX + tiltY * tiltY) + (pointerHeld ? .6f : 0.0f)
                               + (heldUp ? .6f : 0.0f) + (walking ? .3f : 0.0f));
  eyes->setPupilDilation(1.0f + .08f * attention);
  bool usePointer = pointerHeld ||
      (now - pointerAt < 1300 && (current == Mood::HAPPY || current == Mood::BOOP || current == Mood::PETTED ||
                                  current == Mood::SWIPING || current == Mood::FOLLOWING || current == Mood::LOVED));
  bool own = usePointer;
  float gx = pointerX, gy = pointerY, speed = 1.3f;
  switch (current) {
    case Mood::IDLE:
      // Purposeful tilt/pan follows the same lookAt springs as a finger.
      if (!usePointer && (fabsf(tiltX) + fabsf(tiltY) > .10f)) {
        own = true; gx = tiltX; gy = tiltY; speed = 1.55f;
      }
      break;
    case Mood::SAD: own = true; gx = 0; gy = .72f; speed = .35f; break;
    case Mood::SHY: {
      own = true; gx = -.85f; gy = .2f; speed = .8f;
      int32_t cycle = age % 1700;
      if (age > 600 && cycle > 1100 && cycle < 1450) gx = -.25f; // a quick peek back
      break;
    }
    case Mood::ANGRY: own = true; gx = .05f * noise1(clock * .8f, seed + 5); gy = -.12f; speed = 1.3f; break;
    case Mood::ANXIOUS:
      if (int32_t(now - nextDartAt) >= 0) {
        dartX = frand(-.75f, .75f);
        dartY = frand(-.55f, -.15f);
        nextDartAt = now + randMs(110, 240);
      }
      own = true; gx = dartX; gy = dartY; speed = 2.0f;
      break;
    case Mood::SURPRISED:
      own = true; gx = age < 420 ? 0 : age < 650 ? -.45f : age < 850 ? .4f : 0;
      gy = -.08f; speed = 2.2f; break;
    case Mood::DIZZY: {
      own = true; gx = gy = 0; speed = 1.3f;
      float t = age / 1000.0f;
      if (t > 2.0f) { // struggles to centre, overshooting once or twice
        if (t < 2.15f) { gx = -.38f; gy = .14f; }
        else if (t < 2.38f) { gx = .28f; gy = -.06f; }
        else if (t < 2.58f) { gx = -.10f; gy = .03f; }
        speed = .8f;
      }
      break;
    }
    case Mood::WAKE_UP: case Mood::SLEEPY: own = true; gx = scriptGX; gy = scriptGY; speed = scriptGazeSpeed; break;
    case Mood::ALWAYS_ON: case Mood::AUTO_SLEEP: own=true;gx=gy=0;speed=1.8f;break;
    case Mood::BATTERY: own = true; gx = gy = 0; speed = .9f; break;
    case Mood::CONFUSED: own = true; gx = .35f; gy = -.4f; speed = .8f; break;
    case Mood::CURIOUS: own = true; gx = .6f * lookSide; gy = -.18f; speed = .9f; break;
    case Mood::WINK: own = true; gx = .18f * lookSide; gy = -.05f; speed = 1.6f; break;
    case Mood::PLAYFUL:
      own = true; speed = 1.8f;
      gx = age < 450 ? .55f * lookSide : age < 950 ? -.45f * lookSide : 0;
      gy = -.15f;
      break;
    case Mood::STRETCH: own = true; gx = 0; gy = age < 1000 ? -.55f : 0; speed = 1.1f; break;
    case Mood::PEEK:
      own = true; gx = age < 180 ? 0 : age < 620 ? .6f*lookSide : age < 1000 ? -.3f*lookSide : 0;
      gy = age < 180 ? .2f : -.15f; speed = 1.9f; break;
    case Mood::GIGGLE:
      own = true; gx = .12f*sinf(age*.012f); gy = -.12f; speed = 1.5f; break;
    case Mood::SNEEZE:
      own = true; gx = 0; gy = age < 260 ? -.22f : age < 650 ? .12f : 0; speed = 2; break;
    case Mood::SASSY:
      own = true; speed = 2.1f;
      gx = age < 300 ? .65f*lookSide : age < 540 ? -.6f*lookSide : age < 1000 ? .2f*lookSide : 0;
      gy = age < 540 ? -.08f : age < 1000 ? -.68f : 0; break;
    case Mood::YAWN: own = true; gx = 0; gy = age < 1250 ? -.35f : 0.0f; speed = .5f; break;
    case Mood::LOOK_AROUND:
      // A look around the room: one side, the other, up, then back to you.
      own = true; speed = 1.6f;
      if (age < 650) { gx = -.85f * lookSide; gy = -.1f; }
      else if (age < 1350) { gx = .85f * lookSide; gy = -.2f; }
      else if (age < 1850) { gx = -.3f * lookSide; gy = -.6f; }
      else { gx = 0; gy = 0; }
      break;
    case Mood::SWIPING: own = true; gx = swipeX; gy = swipeY; speed = 1.5f; break;
    default: break;
  }
  // A new turn redirects spontaneous looks; touch and scripted wake/sleep,
  // battery and dizzy sequences retain their authored gaze choreography.
  bool motionResponsive = current != Mood::SLEEPY && current != Mood::WAKE_UP &&
                          current != Mood::BATTERY && current != Mood::DIZZY &&
                          current != Mood::ALWAYS_ON && current != Mood::AUTO_SLEEP;
  if (motionResponsive && !touchAngerProtected && !usePointer &&
      (fabsf(tiltX) + fabsf(tiltY) > .40f || motionLevel > .12f)) {
    gx = tiltX; gy = tiltY;
    own = true; speed = fmaxf(speed, 1.6f);
  }
  if (own) eyes->lookAt(gx, gy, speed);
  else if (ownedGaze) eyes->lookAround();   // a reaction ended: tilt and idle looks take over at once
  ownedGaze = own;
  bool sacOn = current != Mood::DIZZY && current != Mood::SLEEPY && current != Mood::BATTERY &&
               !(current == Mood::WAKE_UP && age < 900);
  eyes->setSaccades(sacOn && !touchAngerProtected, current == Mood::ANGRY || current == Mood::ANXIOUS);
}

// ---- Body: the pair of eyes as one mass, with weight, inertia and recoil ----

void CreatureAnimator::updateBody() {
  float tx = 0, ty = 0, f = 3.0f, z = .36f;
  const float t = age / 1000.0f;
  switch (current) {
    case Mood::IDLE:
      tx = (.9f * sinf(clock * TAU_F / 5.3f) + tiltX * motionLevel * 2.5f) * (1 - .75f * batteryFatigue);
      ty = .8f * sinf(clock * TAU_F / 4.1f) * (1 - .75f * batteryFatigue);
      f = 2.8f; z = .8f;
      break;
    case Mood::WINK:
      tx = -lookSide * 3.5f * sinf(PI_F * clamp01(t / .76f));
      ty = -1.8f; f = 5; z = .65f;
      break;
    case Mood::PLAYFUL:
      tx = 6.5f * sinf(TAU_F * 2.0f * t) * (1 - smoothstep(.85f, 1.6f, t));
      ty = -6.0f * fmaxf(0, sinf(TAU_F * 2.0f * t));
      f = 6; z = .48f;
      break;
    case Mood::STRETCH:
      ty = -4.0f * sinf(PI_F * clamp01(t / 1.7f));
      f = 3.2f; z = .8f;
      break;
    case Mood::PEEK:
      tx = lookSide*(age < 180 ? -2 : age < 620 ? 7 : age < 1000 ? -2 : 0);
      ty = age < 180 ? 5 : age < 620 ? -4 : 0; f = 5.5f; z = .60f; break;
    case Mood::GIGGLE:
      tx = 3.0f*sinf(TAU_F*3*t)*(1-smoothstep(1.0f,1.5f,t));
      ty = -5.5f*fmaxf(0,sinf(TAU_F*3*(t-.15f)))*(1-smoothstep(1.0f,1.5f,t));
      f = 7; z = .55f; break;
    case Mood::SNEEZE:
      ty = age < 260 ? 4 : age < 460 ? -7 : age < 650 ? 4 : 0;
      f = 6.5f; z = .58f; break;
    case Mood::SASSY:
      tx = lookSide*(age < 540 ? 4 : age < 1000 ? -3 : 0);
      ty = age < 1000 ? -2 : 0; f = 5; z = .68f; break;
    case Mood::FOLLOWING:
      if (pointerHeld) { tx = pointerX * 2.5f; ty = pointerY * 2.0f; }
      break;
    case Mood::SWIPING:
      if (age < 260) { // pulled with the finger, then released into a springy rebound
        float u = easeOut(age / 260.0f);
        tx = swipeX * 15 * u;
        ty = swipeY * 12 * u;
      }
      f = 4.5f; z = .32f;
      break;
    case Mood::PETTED:
      tx = 2.2f * sinf(TAU_F * .45f * t);
      ty = .8f * sinf(TAU_F * .9f * t);
      f = 2; z = .9f;
      break;
    case Mood::HAPPY: case Mood::LOVED: {
      float fade = 1.0f - .5f * smoothstep(0.0f, float(moodUntil - moodStart), float(age));
      ty = -3.5f * fmaxf(0.0f, sinf(TAU_F * 2.2f * t)) * fade;
      f = 6; z = .42f;
      break;
    }
    case Mood::BOOP:
      f = 4; z = .3f;
      if (crossed(55) && boopFar >= 0) {
        eyes->kickShape(boopFar, ES_HEIGHT, -3.0f);
        eyes->kickShape(boopFar, ES_WIDTH, .8f);
      }
      break;
    default:
      break;
  }
  eyes->setBodyTarget(tx * PX, ty * PX, f, z);
  eyes->setForce(-inertiaX * INERTIA_GAIN, -inertiaY * INERTIA_GAIN);
}

// ---- Effects: dizzy wobble and orbit, shake rattle, shivers, trembling ----

void CreatureAnimator::updateEffects(float dt) {
  float ex[2] = {0, 0}, ey[2] = {0, 0}, px[2] = {0, 0}, py[2] = {0, 0};
  const float t = age / 1000.0f;
  if (current == Mood::GIGGLE) {
    float env = smoothstep(.12f,.25f,t)*(1-smoothstep(1.05f,1.4f,t));
    for (int i=0;i<2;++i) ey[i] += 1.5f*env*sinf(TAU_F*3*t+i*.7f);
  } else if (current == Mood::SNEEZE && age >= 260 && age < 700) {
    float env = 1-smoothstep(.46f,.70f,t);
    for (int i=0;i<2;++i) { ex[i] += (i==0?-1:1)*3*env; ey[i] += 2*env*sinf(TAU_F*7*t); }
  }
  if (current == Mood::DIZZY) {
    for (int i = 0; i < 2; ++i) {
      // Wobble along the impact axis with decaying amplitude.
      float ti = t - i * .03f;
      float amp = 7.0f * expf(-fmaxf(0.0f, ti - .08f) / .28f) * smoothstep(0.0f, .08f, ti);
      float s1 = sinf(TAU_F * 6.5f * ti), s2 = sinf(TAU_F * 5.1f * ti + 1);
      ex[i] += amp * (impactX * s1 - impactY * .35f * s2);
      ey[i] += amp * (impactY * s1 + impactX * .35f * s2);
    }
    // Each eye circles slightly out of phase, the circles shrinking.
    float env = smoothstep(.35f, .75f, t) * (1.0f - smoothstep(1.85f, 2.3f, t));
    float u = clamp01((t - .35f) / 1.95f), phase = dizzyPhase(t);
    float radius = mix(10, 3, u) * env, pupilRadius = mix(.6f, .25f, u) * env;
    ex[0] += cosf(phase) * radius;
    ey[0] += sinf(phase) * radius * .8f;
    ex[1] += cosf(phase + .9f) * radius * .85f;
    ey[1] += sinf(phase + .9f) * radius * .68f;
    px[0] += cosf(1.6f - phase * 1.3f) * pupilRadius;
    py[0] += sinf(1.6f - phase * 1.3f) * pupilRadius * .85f;
    px[1] += cosf(2.5f - phase * 1.3f) * pupilRadius;
    py[1] += sinf(2.5f - phase * 1.3f) * pupilRadius * .85f;
    // The spirals spin fast at first and wind down with the tumble.
    float s = fmaxf(0.0f, t - .35f);
    eyes->setSpiralSpeed(1.6f * TAU_F * fmaxf(.4f, 2.1f - .7f * s) + (t < .4f ? 9.0f : 0.0f));
  } else if (current == Mood::SHIVER) {
    for (int i = 0; i < 2; ++i) {
      ex[i] = 2.4f * sinf(TAU_F * 11 * clock + i * .6f) * (.7f + .3f * noise1(clock * 3, seed + i));
      ey[i] = .6f * sinf(TAU_F * 13 * clock + i);
    }
  } else if (current == Mood::ANXIOUS) {
    ex[0] = ex[1] = 2.5f * sinf(TAU_F * 8.7f * clock);
  }
  // Being shaken: both eyes rattle around (a little out of step with each
  // other) and the pupils jiggle inside them. Fades out once shaking stops.
  shakeLevel = approach(shakeLevel, 0.0f, 3.5f, dt);
  if (shakeLevel > .01f && current != Mood::SLEEPY) {
    const float a = shakeLevel;
    for (int i = 0; i < 2; ++i) {
      ex[i] += a * 6.0f * noise1(clock * 13.0f + i * 3.1f, seed + 61 + i);
      ey[i] += a * 4.5f * noise1(clock * 11.0f + i * 1.7f, seed + 71 + i);
      px[i] += a * .55f * noise1(clock * 9.0f + i, seed + 81 + i);
      py[i] += a * .40f * noise1(clock * 8.0f + i, seed + 91 + i);
    }
  }
  // Held on for too long: it trembles with rage.
  if ((current == Mood::FOLLOWING || current == Mood::UPSIDE_DOWN) && holdAnnoy > .6f) {
    float a = (holdAnnoy - .6f) / .4f;
    for (int i = 0; i < 2; ++i) {
      ex[i] += 1.8f * a * noise1(clock * 17.0f + i * 2.3f, seed + 101 + i);
      ey[i] += 1.0f * a * noise1(clock * 15.0f + i * 1.1f, seed + 111 + i);
    }
  }
  if (!touchAngerProtected && (current == Mood::ANGRY || current == Mood::ANXIOUS)) {
    for (int i = 0; i < 2; ++i) {
      px[i] += .05f * noise1(clock * 9, seed + 11 + i);
      py[i] += .04f * noise1(clock * 9, seed + 21 + i);
    }
  }
  for (int i = 0; i < 2; ++i) eyes->setJitter(i, ex[i] * PX, ey[i] * PX, px[i], py[i]);
}

void CreatureAnimator::updateBacklight(float dt) {
  if (current == Mood::WAKE_UP) {
    // The screen lights while the eyes are still shut.
    float fade = wakeVariant == WAKE_STARTLED_INDEX ? 150.0f : 380.0f;
    blLevel = fmaxf(blLevel, easeOut(age / fade));
  } else if (current == Mood::SLEEPY && age >= SLEEP_FADE_AT) {
    float u = easeInOut((age - SLEEP_FADE_AT) / float(SLEEP_FADE_MS));
    if (napping()) // the screen breathes slowly instead of going dark
      blLevel = approach(blLevel, napLevel * (.86f + .14f * sinf(clock * TAU_F / 4.8f)), 3.0f, dt);
    else
      blLevel = fminf(blLevel, mix(1.0f, napLevel, u));
  } else {
    blLevel = fminf(1.0f, blLevel + dt * 5.0f);
  }
}
