#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "esp_sleep.h"
#include "esp_heap_caps.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "DEV_Config.h"
#include "LCD_1in28.h"
#include "QMI8658.h"
#include "CST816S.h"
#include "FaceConfig.h"
#include "FaceLog.h"
#include "EyeRenderer.h"
#include "Eyes.h"
#include "CreatureAnimator.h"
#include "ShakeDetector.h"
#include "LeftRightWake.h"
#include "MotionGestures.h"
#include "TouchTracker.h"
#include "InteractionPolicy.h"

// Star Face for Waveshare ESP32-S3-Touch-LCD-1.28 (240x240 round).
// Hardware, input and power live here. The eyes are an animation system of
// their own (Eyes: expressions, gaze, blinks, idle behaviour; EyeRenderer:
// anti-aliased vector drawing); CreatureAnimator turns touch, motion and time into
// moods that drive them. Eye size, colours, animation feel, shake sensitivity
// and sleep timing are tuned in FaceConfig.h.
//
// The eyes can also be driven directly from anywhere in the sketch:
//   eyes.setExpression(HAPPY);   eyes.lookAt(0.5f, -0.3f);   eyes.blink();
uint16_t *BlackImage = nullptr; // Required by Waveshare's LCD_1in28.cpp.
CST816S touch(6, 7, 13, 5);
EyeRenderer renderer;
Eyes eyes;
CreatureAnimator creature;

static constexpr int W = SCREEN_WIDTH, H = SCREEN_HEIGHT;
static_assert(W == LCD_1IN28_WIDTH && H == LCD_1IN28_HEIGHT, "FaceConfig.h screen size must match the panel");
static constexpr uint32_t FRAME_US = 16667;      // frame cap (~60 FPS); render time sets the real rate
static constexpr uint32_t NAP_FRAME_US = 33333;  // 30 FPS is plenty while the eyes are shut
static constexpr float MAX_FRAME_DT = .12f;      // cover real frame intervals; spring integration sub-steps
static constexpr bool LOG_FPS = false;           // print frames per second over serial
// Prints shake strength while it is being shaken (silent when still), and
// every knock, flick, rock and upside-down/face-down gesture. Open the serial
// monitor at 115200 to see whether the motion sensor responds and how hard
// your shakes and knocks are; set false once tuned.
static constexpr bool LOG_SHAKE = true;

// Power (sleep timeouts, battery thresholds and backlight are in FaceConfig.h).
RTC_DATA_ATTR bool criticalBatterySleep = false;
RTC_DATA_ATTR BatterySupply::Snapshot retainedBatteryGauge = {};
RTC_DATA_ATTR float gazeGyroBias[3] = {};
RTC_DATA_ATTR bool gazeGyroBiasValid = false;
static constexpr float gazeFlatUpSign = GAZE_FLAT_UP_SIGN;

// Coarse low-power motion alarm (max 255). Higher means walking wakes the CPU
// less often for a dark-screen rotation check.
static constexpr uint8_t WOM_THRESHOLD_MG = 200;
static constexpr int IMU_WAKE_PIN = 3;    // QMI8658 INT2, active low in WoM
static constexpr int TOUCH_WAKE_PIN = 5;  // CST816S IRQ, active-low touch pulse

uint32_t lastActivity = 0, lastFrameUs = 0;
bool sleepPreparing = false, deepSleepPlanned = false;
bool imuReady = false, wokeByShake = false;
AwakeLimit awakeLimit;
LeftRightWakeCheck sleepingShake;
float tiltX = 0, tiltY = 0;
WorldFollower gazeWorld;
float linX = 0, linY = 0; // smoothed gravity-free acceleration
TouchTracker finger;
uint32_t lastTouchActionAt = 0, lastSwipeAt = 0;
uint32_t sleepRetryAt = 0, lastBatteryCheck = 0;
uint8_t tapStreak = 0;
uint32_t lastTapAt = 0;
ScreenTapRun screenTaps;
AlwaysOnGesture alwaysOnGesture;
HoldToCenterGesture holdToCenter;
float dieTempC = 25.0f, batteryVolts = 0;
int batteryPercent = -1;
BatterySupply batterySupply;
Preferences batteryMemory;
bool batteryMemoryReady = false;
int savedBatteryPercent = -1;
bool savedCharging = false;
bool savedGaugeValid = false;
uint32_t lastBatterySave = 0;
uint32_t lastTempRead = 0, coldSince = 0, lastColdReaction = 0;

TwistDetector activeTwist;
ShakeDetector shaker;
TiltFlickDetector flicker;
RockDetector rocker;
StepDetector steps;
bool touchStuck = false;   // a touch that never lifts is being ignored
bool imuSyncEnabled = false;
bool configureImuSync(bool enabled);

// ---- Fast motion sampling on the second core ----
// A knock on the case is over in a few milliseconds, far quicker than the
// render loop samples the sensor, so a small task on core 0 reads the IMU
// every FAST_SAMPLE_MS and looks for knocks. The render loop takes the latest
// sample from it for everything else (tilt, shakes, flicks, rocking).
static constexpr uint32_t FAST_SAMPLE_MS = 3;   // ~333 samples per second
struct KnockReport { uint8_t count; float x, y; };
portMUX_TYPE motionLock = portMUX_INITIALIZER_UNLOCKED;
volatile bool samplerOn = false, samplerBusy = false;
TaskHandle_t samplerTask = nullptr;
float latestA[3] = {0, 0, 9.81f}, latestG[3] = {0, 0, 0};
bool latestValid = false;
uint32_t latestMotionAt = 0;
bool latestCarriedMotion = false;
float latestShakeStrength = 0;
uint32_t latestShakeFor = 0;
ShakeDetector::Event pendingShake = ShakeDetector::NONE;
KnockReport knockQueue[8];
uint8_t knockHead = 0, knockTail = 0;
// Wire locks the bus per transfer but hands the received bytes over after
// unlocking, so two cores reading at once could swap each other's data. Every
// sensor read that can overlap the sampler (touch, IMU temperature) holds this.
SemaphoreHandle_t i2cMutex = nullptr;
struct I2CGuard {
  I2CGuard() { if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY); }
  ~I2CGuard() { if (i2cMutex) xSemaphoreGive(i2cMutex); }
};

void applyBacklight(float level) {
  static int lastDuty = -1;
  level = constrain(level, 0.0f, 1.0f);
  // Squared so fades look even to the eye instead of rushing at the dark end.
  int duty = int(BACKLIGHT_PERCENT * 2.55f * level * level + .5f);
  if (duty != lastDuty) {
    analogWrite(LCD_BL_PIN, duty);
    lastDuty = duty;
  }
}

bool batteryLow() {
  return !batterySupply.charging && batterySupply.rawPercent >= 0 && batterySupply.rawPercent <= CRITICAL_BATTERY_PERCENT;
}

uint32_t idleTimeout() {
  return AUTO_DEEP_SLEEP ? IDLE_SLEEP_MS : IDLE_NAP_MS;
}

// A user interaction: always counts as activity and ends any sleep sequence.
void recordActivity(uint32_t now) {
  lastActivity = now;
  if (cancelClosingForActivity(awakeLimit, creature, sleepPreparing, deepSleepPlanned, batteryLow(), now)) {
    FACE_LOG(println, "Activity canceled closing; 15-second window renewed");
  }
}

bool react(Mood m, uint32_t now, uint32_t duration = 900, bool fromTouch = true) {
  if (!fromTouch && sleepPreparing) return false;
  if (fromTouch) { recordActivity(now); sleepPreparing = false; }
  return creature.react(m, now, duration, fromTouch);
}

bool reactMotion(Mood m, uint32_t now, uint32_t duration = 900) {
  return react(m, now, duration, false);
}

void startGazeSession() {
  gazeWorld.beginWakeSession();
  tiltX=0;
  creature.setTilt(0,tiltY);
  creature.setSwing(0,gazeWorld.swingY);
}

void startWakeAnimation(uint32_t now, bool fromShake, bool touched = false) {
  startGazeSession();
  awakeLimit.start(now);
  sleepingShake = LeftRightWakeCheck();
  sleepPreparing = false;
  lastActivity = now;
  creature.startWake(now, fromShake, touched);
}

void startSleepAnimation(uint32_t now) {
  if ((RAW_IMU_SERIAL_ONLY || awakeLimit.alwaysOn) && !batteryLow()) return;
  sleepingShake = LeftRightWakeCheck();
  sleepPreparing = true;
  deepSleepPlanned = AUTO_DEEP_SLEEP || batteryLow();
  float napLevel = deepSleepPlanned ? 0.0f :
                   sqrtf(NAP_BACKLIGHT_PERCENT / float(BACKLIGHT_PERCENT));
  creature.startSleep(now, napLevel);
  FACE_LOG(printf, "%s after %lu ms idle\n", deepSleepPlanned ? "Falling asleep" : "Dozing off",
                (unsigned long)(now - lastActivity));
}

bool confirmTouchWake();

// Closing can be interrupted by touch; a fully dark face needs TOUCH_WAKE_TAPS presses.
bool cancelSleepForTouch(uint32_t now) {
  if (batteryLow()) return false;
  if (!sleepPreparing) return false;
  if (awakeLimit.active) {
    recordActivity(now); // even a touch at the display-off deadline cancels closing
    return false;
  }
  if (creature.sleepFinished(now)) {
    if (!confirmTouchWake()) return false;
    startWakeAnimation(millis(), false, true);
    FACE_LOG(println, "Nap ended by touch");
    return true;
  }
  react(Mood::SURPRISED, now, 900);
  FACE_LOG(println, "Sleep animation canceled by touch");
  return false;
}

void restoreBatteryMemory() {
  batteryMemoryReady = batteryMemory.begin("starbattery", false);
  bool restored = false;
  if (batteryMemoryReady) {
    size_t storedSize = batteryMemory.getBytesLength("gauge");
    if (storedSize == sizeof(BatterySupply::Snapshot)) {
      BatterySupply::Snapshot stored{};
      if (batteryMemory.getBytes("gauge", &stored, sizeof(stored)) == sizeof(stored)) {
        restored = batterySupply.restore(stored);
        if (restored) {
          savedBatteryPercent = stored.percent; savedCharging = stored.charging;
          savedGaugeValid = stored.version == BatterySupply::SNAPSHOT_VERSION;
        }
      }
    } else if (storedSize == 0) {
      // Migrate the current estimate once. A historical probe minimum is not
      // the present state of charge and must never reseed a charged cell.
      float storedVolts = batteryMemory.getFloat("volts", 0);
      int storedPercent = batteryMemory.getInt("percent", -1);
      batterySupply.restore(storedVolts, storedPercent);
    }
  }
  // The complete filter, transition and step state survives deep sleep.
  batterySupply.restore(retainedBatteryGauge);
}

void persistBatteryMemory(bool force = false) {
  if (!batteryMemoryReady) return;
  BatterySupply::Snapshot state = batterySupply.snapshot();
  if (state.version != BatterySupply::SNAPSHOT_VERSION) return;
  bool modeChanged = !savedGaugeValid || savedCharging != bool(state.charging);
  bool levelChanged = savedBatteryPercent != state.percent;
  uint32_t now = millis();
  if (!modeChanged && !(force && levelChanged) && now - lastBatterySave < 60000) return;
  // One versioned NVS blob: percentage, voltage and charge mode cannot disagree
  // after a reset between separate writes. Preferences commits this key atomically.
  if (batteryMemory.putBytes("gauge", &state, sizeof(state)) == sizeof(state)) {
    savedBatteryPercent = state.percent; savedCharging = state.charging; savedGaugeValid = true;
    lastBatterySave = now;
  }
}

void handleBatterySerial() {
  // Explicit commissioning handshake. A detected COM port and an authorized
  // upload establish USB presence even when a full cell's voltage is steady.
  static char line[16] = {};
  static uint8_t used = 0;
  while (Serial.available()) {
    char c = char(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      line[used] = 0;
      if (strcmp(line, "USB_POWER=1") == 0 && batterySupply.rawPercent >= 0) {
        bool newlyConnected = !batterySupply.charging;
        batterySupply.confirmUsbPower();
        creature.setBatteryLevel(batterySupply.percent, true);
        if(cancelClosingForCharge(awakeLimit, creature, sleepPreparing, deepSleepPlanned, newlyConnected, millis()))
          startGazeSession();
        retainedBatteryGauge = batterySupply.snapshot();
        persistBatteryMemory(true);
        FACE_LOG(printf, "USB power confirmed by host; estimate retained at %d%%\n", batterySupply.percent);
      }
      used = 0;
    } else if (used < sizeof(line) - 1) line[used++] = c;
    else used = 0;
  }
}

void readBattery() {
  // Rev3 schematic senses VBAT via a 200k/100k divider.
  // Calibrated ADC millivolts already include ESP32 calibration; multiply by 3.
  uint32_t sum = 0;
  uint32_t lo = UINT32_MAX, hi = 0;
  for (int i = 0; i < 16; ++i) {
    uint32_t v = analogReadMilliVolts(BAT_ADC_PIN);
    sum += v; lo = min(lo, v); hi = max(hi, v);
  }
  float volts = (sum - lo - hi) / 14.0f * .003f;
  bool wasCharging = batterySupply.charging;
  // Reject ADC outliers, detect sustained charge rises, smooth battery-only estimates.
  uint32_t sampledNow = millis();
  uint32_t checkMs = BATTERY_LIVE_POWER_CHECK_MS;
  batterySupply.sample(volts, sampledNow, checkMs);
  handleBatterySerial();
  batteryVolts = batterySupply.volts;
  batteryPercent = batterySupply.percent;
  creature.setBatteryLevel(batteryPercent, batterySupply.charging);
  if (cancelClosingForCharge(awakeLimit, creature, sleepPreparing, deepSleepPlanned,
                            !wasCharging && batterySupply.charging, sampledNow)) {
    startGazeSession();
    lastActivity = sampledNow;
    FACE_LOG(println, "Plug-in canceled closing; charging wake animation");
  }
  retainedBatteryGauge = batterySupply.snapshot();
  persistBatteryMemory();
  static bool logged = false;
  static int lastLoggedPercent = -1;
  if (!logged || wasCharging != batterySupply.charging || lastLoggedPercent != batteryPercent) {
    FACE_LOG(printf, "Battery: VBAT=%.3fV filtered=%.3fV estimate=%d%% target=%d%% charging-inferred=%d pending=%d settled=%d\n",
             volts, batterySupply.filteredVolts, batteryPercent, batterySupply.voltageTarget(),
             batterySupply.charging, batterySupply.chargeSuspected(), batterySupply.settled());
    lastLoggedPercent = batteryPercent;
    logged = true;
  }
}

void showBatteryLevel(int percent) {
  uint32_t now = millis();
  recordActivity(now);
  sleepPreparing = false;
  creature.showBattery(percent, now);
}

void beginSwipe(int x0, int y0, int dx, int dy, uint32_t now, bool fromTouch = true) {
  if (now - lastSwipeAt < 140) return; // CST816S can report gesture and release together.
  lastSwipeAt = now;
  bool horizontal = abs(dx) >= abs(dy);
  int sx = horizontal ? (dx >= 0 ? 1 : -1) : 0;
  int sy = horizontal ? 0 : (dy >= 0 ? 1 : -1);
  creature.setSwipe(sx, sy);
  creature.setPointer(sx, sy, now);
  creature.setTouchPoint(constrain(x0 + dx, 0, W - 1), constrain(y0 + dy, 0, H - 1));
  if (!react(Mood::SWIPING, now, 740, fromTouch)) return;
  if (sy < 0) {
    readBattery();
    creature.queue(Mood::BATTERY, now + 700, BATTERY_SHOW_MS, batteryPercent);
  } else if (sy > 0) creature.queue(Mood::SAD, now + 700, 1400);
  else if (sx < 0) creature.queue(Mood::SHY, now + 700, 1400);
  else creature.queue(Mood::HAPPY, now + 700, 1400);
}

void pointAt(int x, int y, uint32_t now) {
  creature.setTouchPoint(x, y);
  const float reach = SCREEN_MIN_SIDE / 3.0f;
  creature.setPointer((x - SCREEN_CX) / reach, (y - SCREEN_CY) / reach, now);
}

// One tap (or knock) in a run: 1 boop, 2 surprise, 3/4 a brief angry huff.
void tapReaction(int streak, int x, int y, uint32_t now, bool fromTouch = true) {
  pointAt(x, y, now);
  tapStreak = streak;
  if (react(tapMood(tapStreak), now, tapStreak >= 3 ? ANGRY_ANIM_MS : tapStreak == 2 ? 1050 : 900, fromTouch)
      && fromTouch && tapStreak == 1) creature.queue(Mood::GIGGLE,now+900,1600);
}

void finishTap(int x, int y, uint32_t now) {
  if (now - lastTouchActionAt < 75) return;
  lastTouchActionAt = now;
  int streak = screenTaps.add(now);
  lastTapAt = now;
  if (streak == TOUCH_BATTERY_TAPS) {
    readBattery();
    showBatteryLevel(batteryPercent);
    screenTaps = ScreenTapRun();
    FACE_LOG(printf, "touch: ten taps, battery %d%%\n", batteryPercent);
    return;
  }
  if (creature.batteryDisplayActive(now) || creature.touchAngerPauseActive(now)) return;
  if (streak == 5) {
    recordActivity(now);
    sleepPreparing = false;
    creature.startTouchAngerPause(now);
    FACE_LOG(printf, "touch: five taps, angry pause for %lu ms\n", (unsigned long)TOUCH_ANGER_PAUSE_MS);
    return;
  }
  tapReaction(streak, x, y, now);
}

// A finger lifted after being held (no tap, no swipe).
void finishHold(const TouchTracker::Result &r, uint32_t now) {
  float anger = creature.annoyance();
  if (anger > .35f) {
    // Held on too long: a huff, then it cools down.
    creature.huff(anger, now);
    FACE_LOG(printf, "Let go after %lu ms: huff %.2f\n", (unsigned long)r.heldMs, anger);
  } else if (r.heldMs < HOLD_ANGER_START_MS) {
    react(Mood::PETTED, now, 1100); // a short, gentle press feels nice
  }
}

void centerAfterTouch(uint32_t now) {
  if(batteryLow())return;
  gazeWorld.requestHorizontalCenter();tiltX=0;
  creature.centerAfterHold(now);
  FACE_LOG(println,"Manual hold: adopting current horizontal center for five seconds");
}

void toggleAlwaysOn(uint32_t now) {
  if(batteryLow()) return;
  awakeLimit.alwaysOn=!awakeLimit.alwaysOn;
  recordActivity(now);
  sleepPreparing=deepSleepPlanned=false;
  screenTaps=ScreenTapRun();
  creature.setAlwaysOn(awakeLimit.alwaysOn,now);
  FACE_LOG(printf,"Always-on mode %s (tap, then hold)\n",awakeLimit.alwaysOn?"enabled":"disabled");
}

uint8_t rawTouchFingerCount();
bool confirmedTouchContact() { return rawTouchFingerCount() == 1; }

void handleTouch(uint32_t now) {
  if (!TOUCH_ENABLED) return;
  TouchTracker::Result r;
  // A finger that never lifts is not a finger (a cover pressing on the glass,
  // moisture): ignore it until it lifts, so it can't keep it angry or awake.
  if (TOUCH_STUCK_MS && finger.tracking() && finger.heldFor(now) > TOUCH_STUCK_MS) {
    finger.ignoreUntilLift(now);
    touchStuck = true;
    creature.setPointerHeld(false, now);
    FACE_LOG(println, "Touch held for too long: ignoring it until it lifts");
  }
  bool touched;
  {
    I2CGuard bus;
    touched = touch.available();
  }
  if (touched) {
    const auto &d = touch.data;
    bool valid = d.x >= 0 && d.x < W && d.y >= 0 && d.y < H;
    touched = confirmedTouchReport(d.points,d.event,d.gestureID,finger.down,valid);
  }
  if (!touched && finger.down && !touchStuck) {
    static uint32_t lastPresenceRead = 0;
    if (now - lastPresenceRead >= 40) {
      lastPresenceRead = now;
      uint8_t contacts = rawTouchFingerCount();
      if (contacts == 1) {
        finger.confirmContact(now);
        recordActivity(now);
      } else if (contacts == 0) r = finger.confirmLift(now);
    }
  }
  if (creature.touchAngerPauseActive(now)) {
    holdToCenter=HoldToCenterGesture();
    // Keep counting distinct taps so the same run can reach ten. Pointer and
    // other reactions stay paused; touches still renew the inactivity timer.
    if (touched) {
      if (!touchStuck) recordActivity(now);
      auto d = touch.data;
      bool valid = d.x >= 0 && d.x < W && d.y >= 0 && d.y < H;
      r = finger.feed(d.x, d.y, d.event, d.gestureID, valid, now);
    } else if (r.kind == TouchTracker::NONE) r = finger.poll(now);
    if (r.kind == TouchTracker::TAP) finishTap(r.x, r.y, now);
    creature.setPointerHeld(false, now);
    return;
  }
  if (touched) {
    auto d = touch.data;
    bool wasStuck = touchStuck;
    if (wasStuck && d.event == 1) touchStuck = false; // lifted at last
    if (wasStuck) {
      finger.feed(d.x, d.y, d.event, d.gestureID, true, now);
      return;
    }
    if (sleepPreparing && !awakeLimit.active && creature.sleepFinished(now)) {
      cancelSleepForTouch(now);
      finger.ignoreUntilLift(millis());
      return; // incomplete wake taps cannot become ordinary awake reactions
    }
    recordActivity(now);
    // Even if the controller reports a partial/invalid coordinate, its IRQ is
    // real activity. Never finish closing the eyes while a finger is present.
    if (cancelSleepForTouch(now)) {
      finger.ignoreUntilLift(now); // this touch woke it; don't also follow it
      return;
    }
    bool valid = d.x >= 0 && d.x < W && d.y >= 0 && d.y < H;
    r = finger.feed(d.x, d.y, d.event, d.gestureID, valid, now);
  } else {
    if (r.kind == TouchTracker::NONE) r = finger.poll(now); // a lift report can go missing
    if (touchStuck && !finger.down) touchStuck = false;
  }
  bool allowed=!creature.batteryDisplayActive(now);
  bool modeConsumed=alwaysOnGesture.feed(r,now,allowed);
  if(alwaysOnGesture.poll(finger.tracking(),now)) toggleAlwaysOn(now);
  bool centerConsumed=holdToCenter.feed(r,now,allowed && !modeConsumed && !alwaysOnGesture.reserved());
  if(holdToCenter.poll(finger.tracking(),now) || holdToCenter.released)centerAfterTouch(now);
  if(holdToCenter.reserved())creature.keepCenterHeld(now);
  if(modeConsumed || alwaysOnGesture.reserved() || centerConsumed || holdToCenter.reserved()) {
    creature.setPointerHeld(false,now);
    return;
  }
  switch (r.kind) {
    case TouchTracker::PRESS:
      pointAt(r.x, r.y, now);
      react(Mood::FOLLOWING, now, 600); // follows the finger for as long as it stays down
      break;
    case TouchTracker::MOVE:
      pointAt(r.x, r.y, now);
      break;
    case TouchTracker::TAP:
      finishTap(r.x, r.y, now);
      break;
    case TouchTracker::SWIPE:
      if (creature.annoyance() > .35f) finishHold(r, now); // too cross to play along
      else beginSwipe(r.x - int(r.dx), r.y - int(r.dy), int(r.dx), int(r.dy), now);
      break;
    case TouchTracker::RELEASE:
      finishHold(r, now);
      break;
    default:
      break;
  }
}

// Reads m/s^2 and rad/s in the canonical frame used by recorded shake gestures.
// This is not the display frame: followWorld applies GAZE_IMU_ROTATION to both
// vectors before sensor fusion and mapping angles into screen gaze.
bool readMotion(float a[3], float g[3]) {
  short ra[3], rg[3]; unsigned int stamp;
  static unsigned int previousStamp = 0;
  static bool haveStamp = false;
  if (!QMI8658_read_xyz_raw(ra, rg, &stamp) || (haveStamp && stamp == previousStamp)) return false;
  previousStamp = stamp; haveStamp = true;
  // Configured +/-8 g and +/-2048 dps: 4096 counts/g, 16 counts/dps.
  const float accelScale = 9.80665f / 4096, gyroScale = .017453293f / 16;
  float ax = ra[0] * accelScale, ay = -ra[1] * accelScale, gx = rg[0] * gyroScale, gy = -rg[1] * gyroScale;
  for (uint8_t r = 0; r < (IMU_ROTATION & 3); ++r) {
    float t = ax; ax = -ay; ay = t;
    t = gx; gx = -gy; gy = t;
  }
  a[0] = ax; a[1] = ay; a[2] = -ra[2] * accelScale;
  g[0] = gx; g[1] = gy; g[2] = -rg[2] * gyroScale;
  return true;
}

void calibrateGazeGyro() {
  // Cold boot/upload: accept a bias estimate only from a short still interval.
  // Retain it across deep sleep; a shake wake never calibrates against a shake.
  delay(300);
  float meanG[3] = {}, meanA[3] = {}, squaresG[3] = {}, squaresA[3] = {};
  unsigned count = 0;
  for (unsigned i = 0; i < 80; ++i) {
    float a[3], g[3];
    if (readMotion(a, g)) {
      ++count;
      for (int axis = 0; axis < 3; ++axis) {
        meanA[axis] += a[axis]; squaresA[axis] += a[axis]*a[axis];
        meanG[axis] += g[axis]; squaresG[axis] += g[axis]*g[axis];
      }
    }
    delay(5);
  }
  if (count < 20) {
    FACE_LOG(printf, "Gaze calibration skipped: only %u fresh sensor samples\n", count);
    return;
  }
  bool still = true;
  for (int i = 0; i < 3; ++i) {
    meanA[i] /= count; meanG[i] /= count;
    still &= squaresG[i]/count - meanG[i]*meanG[i] < .0004f;
    still &= squaresA[i]/count - meanA[i]*meanA[i] < .0625f;
  }
  still &= motion::len3(meanG) < .35f && fabsf(motion::len3(meanA) - 9.81f) < .6f;
  if (still) {
    for (int i = 0; i < 3; ++i) gazeGyroBias[i] = meanG[i];
    gazeGyroBiasValid = true;
    FACE_LOG(println, "Stationary gyro offset calibrated for gaze");
    FACE_LOG(printf, "Gaze flat-up sign %.0f, acceleration %.2f %.2f %.2f\n", gazeFlatUpSign, meanA[0], meanA[1], meanA[2]);
  } else FACE_LOG(printf, "Gaze calibration skipped: movement detected; acceleration %.2f %.2f %.2f, gyro %.3f %.3f %.3f\n", meanA[0], meanA[1], meanA[2], meanG[0], meanG[1], meanG[2]);
  if (!still) FACE_LOG(printf, "Calibration variation: accel %.3f %.3f %.3f m/s2, gyro %.3f %.3f %.3f rad/s\n",
    sqrtf(fmaxf(0,squaresA[0]/count-meanA[0]*meanA[0])),sqrtf(fmaxf(0,squaresA[1]/count-meanA[1]*meanA[1])),sqrtf(fmaxf(0,squaresA[2]/count-meanA[2]*meanA[2])),
    sqrtf(fmaxf(0,squaresG[0]/count-meanG[0]*meanG[0])),sqrtf(fmaxf(0,squaresG[1]/count-meanG[1]*meanG[1])),sqrtf(fmaxf(0,squaresG[2]/count-meanG[2]*meanG[2])));
}

void printRawImu(uint32_t now) {
  static uint32_t lastPrintAt = 0;
  static unsigned int lastSample = 0;
  static bool haveSample = false;
  if (!RAW_IMU_SERIAL_ONLY || !imuReady || now - lastPrintAt < RAW_IMU_PRINT_MS) return;
  // Skip a row rather than blocking animation when the serial buffer is full.
  if (Serial.availableForWrite() < 64) return;
  lastPrintAt = now;
  short rawA[3], rawG[3];
  unsigned int sample = 0;
  {
    I2CGuard bus;
    if (!QMI8658_read_xyz_raw(rawA, rawG, &sample)) return;
  }
  // Sensor timestamps distinguish a fresh stationary reading from registers
  // frozen in WoM mode. Never present the latter as another live sample.
  if (haveSample && sample == lastSample) return;
  haveSample = true;
  lastSample = sample;
  // Native sensor axes: no screen rotation, scaling, gravity subtraction or
  // software smoothing. Accel is 4096 counts/g; gyro is 16 counts/(degree/s).
  Serial.printf("AX:%d,AY:%d,AZ:%d,GX:%d,GY:%d,GZ:%d\n",
                int(rawA[0]), int(rawA[1]), int(rawA[2]),
                int(rawG[0]), int(rawG[1]), int(rawG[2]));
}

// Waveshare's QMI8658_config_acc() builds the accelerometer's low-pass
// setting and then writes CTRL5 = 0, leaving it off: a knock is then a spike
// of a millisecond or two that sampling easily misses. Turn the filter on
// (~54 Hz: mode 2 at 1 kHz, well above any shake or step) next to the
// gyro's, without changing the driver.
void tuneImuFilters() {
  uint8_t ctrl5 = 0;
  QMI8658_read_reg(QMI8658Register_Ctrl5, &ctrl5, 1);
  QMI8658_write_reg(QMI8658Register_Ctrl5, uint8_t((ctrl5 & 0xF0) | A_LSP_MODE_2 | 0x01));
  configureImuSync(true);
}

void motionSamplerTask(void *) {
  KnockDetector knocks;
  ShakeDetector fastShake;
  LeftRightWakeCheck motionDirection;
  float gravity[3] = {0, 0, 0};
  uint32_t previousAt = 0;
  TickType_t wakeAt = xTaskGetTickCount();
  for (;;) {
    if (!samplerOn) {
      samplerBusy = false;
      vTaskDelay(pdMS_TO_TICKS(5));
      wakeAt = xTaskGetTickCount();
      continue;
    }
    samplerBusy = true;
    float a[3], g[3];
    bool fresh;
    {
      I2CGuard bus;
      fresh = readMotion(a, g);
    }
    if (!fresh) { samplerBusy = false; vTaskDelay(pdMS_TO_TICKS(FAST_SAMPLE_MS)); continue; }
    KnockDetector::Event k = knocks.feed(a, g, millis());
    uint32_t now = millis();
    motionDirection.feed(a, g, now);
    bool carriedMotion = motionDirection.carryingMotion();
    if (!previousAt) for (int i = 0; i < 3; ++i) gravity[i] = a[i];
    float dt = previousAt ? min(uint32_t(100), now - previousAt) / 1000.0f : .003f;
    previousAt = now;
    float lin[3], alpha = 1.0f - expf(-dt / .25f);
    for (int i = 0; i < 3; ++i) {
      gravity[i] += (a[i] - gravity[i]) * alpha;
      lin[i] = a[i] - gravity[i];
    }
    const float quiet[3] = {0, 0, 0};
    ShakeDetector::Event shake = fastShake.feed(carriedMotion ? quiet : lin,
                                              carriedMotion ? 0 : motion::len3(g), now);
    portENTER_CRITICAL(&motionLock);
    for (int i = 0; i < 3; ++i) { latestA[i] = a[i]; latestG[i] = g[i]; }
    latestValid = true;
    latestMotionAt = now;
    latestCarriedMotion = carriedMotion;
    if (carriedMotion) pendingShake = ShakeDetector::NONE;
    latestShakeStrength = fastShake.strength;
    latestShakeFor = fastShake.shakingFor(now);
    // Keep the most meaningful event until the rendering core consumes it.
    if (shake == ShakeDetector::DIZZY ||
        (pendingShake != ShakeDetector::DIZZY && shake == ShakeDetector::STARTLE) ||
        (pendingShake == ShakeDetector::NONE && shake != ShakeDetector::NONE)) pendingShake = shake;
    if (k.count && uint8_t(knockHead + 1) % 8 != knockTail) {
      knockQueue[knockHead] = {k.count, k.x, k.y};
      knockHead = (knockHead + 1) % 8;
    }
    portEXIT_CRITICAL(&motionLock);
    const TickType_t period = pdMS_TO_TICKS(FAST_SAMPLE_MS);
    vTaskDelayUntil(&wakeAt, period > 0 ? period : 1);
  }
}

void startMotionSampler() {
  if (!imuReady) return;
  if (!i2cMutex) i2cMutex = xSemaphoreCreateMutex();
  if (!samplerTask)
    xTaskCreatePinnedToCore(motionSamplerTask, "motion", 4096, nullptr, 2, &samplerTask, 0);
  samplerOn = true;
}

// Stops the fast sampler before the IMU is reconfigured (deep sleep).
void stopMotionSampler() {
  samplerOn = false;
  for (int i = 0; i < 40 && samplerBusy; ++i) delay(2);
  latestValid = false;
  portENTER_CRITICAL(&motionLock);
  pendingShake = ShakeDetector::NONE;
  portEXIT_CRITICAL(&motionLock);
}

// The latest motion sample: from the fast sampler, or read directly.
bool currentMotion(float a[3], float g[3]) {
  if (samplerOn) {
    portENTER_CRITICAL(&motionLock);
    bool fresh = latestValid && millis() - latestMotionAt <= 100;
    if (fresh) for (int i = 0; i < 3; ++i) { a[i] = latestA[i]; g[i] = latestG[i]; }
    portEXIT_CRITICAL(&motionLock);
    return fresh;
  } else {
    I2CGuard bus;
    return readMotion(a, g);
  }
}

bool nextKnock(KnockReport &k) {
  bool any = false;
  portENTER_CRITICAL(&motionLock);
  if (knockTail != knockHead) {
    k = knockQueue[knockTail];
    knockTail = (knockTail + 1) % 8;
    any = true;
  }
  portEXIT_CRITICAL(&motionLock);
  return any;
}

// Reacts to how the star is moved: inertia, bumps, shakes, pick-ups and naps.
void impactFromMotion(float x, float y) {
  float visual[3] = {x,y,0};
  gestureMotionToScreen(visual);
  creature.impact(visual[0],visual[1]);
}

void handleSurroundings(const float a[3], const float g[3], bool heldStill, uint32_t now) {
  // A slow gravity estimate leaves the fast, gravity-free part of the motion.
  static bool gravityReady = false;
  static uint32_t gravityAt = 0;
  static float grav[3];
  if (!gravityReady) {
    for (int i = 0; i < 3; ++i) grav[i] = a[i];
    gravityReady = true;
  }
  float lin[3];
  float gravityDt = gravityAt ? min(uint32_t(100), now - gravityAt) / 1000.0f : .025f;
  gravityAt = now;
  float gravityK = 1.0f - expf(-gravityDt / .25f);
  for (int i = 0; i < 3; ++i) {
    grav[i] += (a[i] - grav[i]) * gravityK;
    lin[i] = a[i] - grav[i];
  }
  // Light smoothing so sensor noise does not make the eyes jitter.
  linX += (lin[0] - linX) * .55f;
  linY += (lin[1] - linY) * .55f;
  float visualInertia[3] = {linX,linY,0}; gestureMotionToScreen(visualInertia);
  creature.applyInertia(visualInertia[0], visualInertia[1]); // moving right throws the eyes left, and so on
  float jolt = sqrtf(lin[0] * lin[0] + lin[1] * lin[1] + lin[2] * lin[2]);

  static uint32_t stillSince = 0;
  uint32_t stillFor = stillSince ? now - stillSince : 0;
  stillSince = heldStill ? (stillSince ? stillSince : now) : 0;
  creature.setStillFor(stillFor);   // lying still for long: bored, then sleepy

  // Carried around: footsteps bob the eyes and it watches the world go by.
  steps.feed(a, shaker.strength, now);
  creature.setCarried(steps.walking, steps.running, steps.step, steps.strength);
  static bool wasWalking = false;
  if (steps.walking != wasWalking && LOG_SHAKE)
    FACE_LOG(println, steps.walking ? (steps.running ? "motion: running" : "motion: walking") : "motion: stopped walking");
  wasWalking = steps.walking;

  // ---- Shaking while awake ----
  // How hard it is being shaken drives a live rattle in the eyes; a single
  // strong push only sloshes them (so being carried, even running, stays
  // calm); a push back the other way startles it; a real shake (see
  // ShakeDetector) makes it dizzy, then angry, then it calms down.
  static uint32_t lastLogAt = 0;
  float spin = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
  ShakeDetector::Event ev;
  uint32_t shakeFor;
  if (samplerOn && latestValid) {
    // Recognition runs on every fast IMU sample, independent of LCD frame time.
    portENTER_CRITICAL(&motionLock);
    ev = pendingShake;
    pendingShake = ShakeDetector::NONE;
    shaker.strength = latestShakeStrength;
    shakeFor = latestShakeFor;
    portEXIT_CRITICAL(&motionLock);
  } else {
    ev = shaker.feed(lin, spin, now);
    shakeFor = shaker.shakingFor(now);
  }
  if (steps.walking) {
    // Worn steps are scenery, not touch-like flicks/knocks. In particular a
    // pocket swing must not queue the downward swipe's sad/downward gaze.
    shaker.strength = 0;
    flicker = TiltFlickDetector(); rocker = RockDetector();
    return; // pending shake reports were consumed above, so none replay later
  }
  if (shaker.rattle() > 0) creature.shake(shaker.rattle());
  // Gestures that stand in for touch (see MotionGestures.h).
  TiltFlickDetector::Dir flick = flicker.feed(a, g, shaker.strength, now);
  bool rocking = rocker.feed(a, g, now);
  if (LOG_SHAKE && shaker.strength > 1.0f && now - lastLogAt > 200) {
    lastLogAt = now;
    FACE_LOG(printf, "shake: strength %.1f (dizzy at %.1f, kept up %.1f of %.1f s)  jolt %.1f m/s^2\n",
                  shaker.strength, SHAKE_DIZZY_STRENGTH, shakeFor / 1000.0f,
                  SHAKE_DIZZY_HOLD_MS / 1000.0f, jolt);
  }
  if (ev == ShakeDetector::DIZZY) {
    // Wobble, spiral eyes, glare, calm down. Keep shaking: it spins again.
    impactFromMotion(lin[0], lin[1]);
    bool started = reactMotion(Mood::DIZZY, now, DIZZY_ANIM_MS);
    if (LOG_SHAKE) FACE_LOG(println, started ? "shake: DIZZY" : "shake: (already dizzy)");
    return;
  }
  if (ev == ShakeDetector::STARTLE) {
    impactFromMotion(lin[0] * .6f, lin[1] * .6f);
    if (creature.mood() != Mood::DIZZY && creature.mood() != Mood::ANGRY) reactMotion(Mood::SURPRISED, now, 650);
    return;
  }
  if (ev == ShakeDetector::STROKE) {
    impactFromMotion(lin[0] * .45f, lin[1] * .45f); // just slosh; not an interaction
    return;
  }
  if (sleepPreparing) return;
  if (flick != TiltFlickDetector::NONE) {
    // Tipped one way and back: the same as a swipe that way.
    static const int8_t FX[] = {0, -1, 1, 0, 0}, FY[] = {0, 0, 0, -1, 1};
    static const char *NAMES[] = {"", "left", "right", "up", "down"};
    if (LOG_SHAKE) FACE_LOG(printf, "motion: flick %s\n", NAMES[flick]);
    float visualFlick[3] = {float(FX[flick]),float(FY[flick]),0}; gestureMotionToScreen(visualFlick);
    beginSwipe(int(SCREEN_CX), int(SCREEN_CY), int(visualFlick[0]*60), int(visualFlick[1]*60), now, false);
    return;
  }
  if (rocking) {
    // Rocked gently: it feels petted for as long as the rocking goes on.
    if (!creature.sustain(Mood::PETTED, now, 1400)) {
      if (LOG_SHAKE) FACE_LOG(println, "motion: rocking");
      reactMotion(Mood::PETTED, now, 1400);
    }
    return;
  }
  if (ev == ShakeDetector::BUMP && !samplerOn) {
    // A knock on the resting star (when knocks are not counted on the fast
    // sampler): the eyes recoil and it looks startled.
    impactFromMotion(lin[0] * .6f, lin[1] * .6f);
    creature.reactPassive(Mood::SURPRISED, now, 520);
  } else if (jolt > PICKUP_MS2 && stillFor > 1200 && creature.mood() == Mood::IDLE) {
    // Bumped, nudged or picked up while it was calm: it notices.
    creature.notice(jolt / 8.0f, now);
    if (stillFor > 20000) {
      // Picked up after a long rest: "oh!" then a happy hello.
      reactMotion(Mood::SURPRISED, now, 520);
      creature.queue(Mood::HAPPY, now + 540, 1200);
      if (LOG_SHAKE) FACE_LOG(println, "motion: picked up after a rest -> hello");
    } else reactMotion(Mood::PEEK,now,1450);
  }
}

// Knocks on the case, counted by the fast sampler: the same as taps.
void handleKnocks(uint32_t now) {
  KnockReport k;
  while (nextKnock(k)) {
    if (steps.walking) continue;
    if (LOG_SHAKE) FACE_LOG(printf, "motion: knock %u (push %.1f, %.1f m/s^2)\n", k.count, k.x, k.y);
    if (sleepPreparing) {
      if (!creature.sleepFinished(now)) reactMotion(Mood::SURPRISED, now, 900);
      continue;
    }
    // A knock from the left pushes the star right: the nearer (left) eye flinches.
    float visualKnock[3] = {k.x,k.y,0}; gestureMotionToScreen(visualKnock);
    float side = fabsf(visualKnock[0]) > fabsf(visualKnock[1]) * .5f ? (visualKnock[0] > 0 ? -1.0f : 1.0f) : 0.0f;
    impactFromMotion(k.x * .5f, k.y * .5f);
    if (k.count == 2) reactMotion(Mood::SNEEZE,now,1250);
    else tapReaction(min(4, int(k.count)), int(SCREEN_CX + side * SCREEN_MIN_SIDE * .25f), int(SCREEN_CY), now, false);
  }
}

// The eyes follow the world: they look toward the low side when it is
// tilted, the face rolls to stay level when it is turned, the eyes
// counter-move to keep looking at you when it is swung, and spinning it
// around, tossing it or bumping it get their own reactions. a and g are in the
// canonical gesture frame; both receive the same visual rotation below.
void followWorld(const float a[3], const float g[3], uint32_t now, bool allowReactions = true) {
  WorldFollower &world=gazeWorld;
  float visualA[3] = {a[0],a[1],a[2]}, visualG[3] = {g[0]-gazeGyroBias[0],g[1]-gazeGyroBias[1],g[2]-gazeGyroBias[2]};
  const float *headingBias = gazeGyroBiasValid ? gazeGyroBias : HORIZONTAL_GYRO_BIAS_RAD_S;
  float visualHeadingG[3] = {g[0]-headingBias[0],g[1]-headingBias[1],g[2]-headingBias[2]};
  rotateMotionXY(visualA, GAZE_IMU_ROTATION);
  rotateMotionXY(visualG, GAZE_IMU_ROTATION);
  rotateMotionXY(visualHeadingG, GAZE_IMU_ROTATION);
  world.setFlatUpSign(gazeFlatUpSign);
  world.setHorizontalCenter(HORIZONTAL_TABLE_CENTER_DEG, HORIZONTAL_HANGING_CENTER_DEG);
  bool hadOrigin=world.horizontalOriginReady();
  uint32_t recenters=world.horizontalRecenterCount();
  bool allowRecenter=!sleepPreparing && !finger.tracking() && !steps.walking;
  world.feed(visualA, visualG, shaker.strength, now, visualHeadingG,allowRecenter);
  if(world.horizontalRecenterCount()!=recenters)
    FACE_LOG(println,"Periodic gaze recenter completed after steady hold");
  if(!hadOrigin && world.horizontalOriginReady())
    FACE_LOG(printf,"Wake gaze center captured: %.1f degrees (horizontal)\n",world.horizontalOriginDegrees());
  static int lastGazePosture = -1;
  int posture = world.uprightReferenceActive() ? 1 : 0;
  if (LOG_SHAKE && posture != lastGazePosture) {
    lastGazePosture = posture;
    FACE_LOG(printf,"Gaze posture: %s; tilt %.2f %.2f\n",posture ? "hanging" : "table",world.tiltX,world.tiltY);
  }
  creature.setTilt(world.tiltX, world.tiltY);
  creature.setFaceRoll(FACE_STAYS_LEVEL ? world.roll : 0.0f);
  creature.setSwing(world.swingX, world.swingY);
  creature.setMotionActivity(world.motionLevel, now);
  tiltX = world.tiltX;
  tiltY = world.tiltY;
  if (allowReactions && world.weightless && !sleepPreparing) {
    creature.impact(0, 12);                 // the eyes float up
    reactMotion(Mood::SURPRISED, now, 1300);
    if (LOG_SHAKE) FACE_LOG(println, "motion: weightless (tossed or dropped)");
  }
  if (allowReactions && world.spun && SPIN_MAKES_DIZZY) {
    if (LOG_SHAKE) FACE_LOG(println, "motion: spun around -> dizzy");
    impactFromMotion(linX + 8, linY);
    reactMotion(Mood::DIZZY, now, DIZZY_ANIM_MS);
  }
}

void handleMotion(uint32_t now) {
  if (!imuReady) return;
  if (sleepPreparing && !awakeLimit.active && creature.sleepFinished(now)) {
    // A dark fallback if WoM could not be armed: use the full wake gesture.
    float a[3], g[3];
    if (!currentMotion(a, g)) return;
    if (sleepingShake.feed(a, g, now)) {
      startWakeAnimation(now, true);
    }
    return;
  }
  static uint32_t lastSample = 0;
  if (now - lastSample < 25) return;
  lastSample = now;
  float a[3], g[3];
  if (!currentMotion(a, g)) return;
  bool carriedMotion;
  portENTER_CRITICAL(&motionLock);
  carriedMotion = latestCarriedMotion;
  portEXIT_CRITICAL(&motionLock);
  bool angryPause = creature.touchAngerPauseActive(now);
  followWorld(a, g, now, !carriedMotion && !angryPause);
  // Motion reacts visually but only confirmed screen touches renew the awake timer.
  if (carriedMotion || angryPause) {
    if (carriedMotion && !angryPause) {
      steps.feed(a,0,now);
      creature.setCarried(steps.walking,steps.running,steps.step,steps.strength);
      creature.setHeldUp(false,now);
      creature.setStillFor(0);
    }
    // This motion is transport, not play: consume knocks queued during it and
    // leave lastActivity alone. The existing visual gaze follower still runs.
    KnockReport ignored;
    while (nextKnock(ignored)) {}
    shaker.strength = 0;
    return;
  }
  handleKnocks(now);
  if (activeTwist.feed(g, now) && TWIST_MAKES_DIZZY) {
    impactFromMotion(linX, linY);
    reactMotion(Mood::DIZZY, now, DIZZY_ANIM_MS); // tumbles, then glares
  }
  // Held up in front of someone: screen upright, in a hand (a little tremor,
  // not perfectly still like on a stand), not being walked around. It pays
  // attention visually: looks at them and gives happy blinks.
  {
    static uint32_t uprightSince = 0;
    float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float planar = sqrtf(a[0] * a[0] + a[1] * a[1]);
    float spinNow = fabsf(g[0]) + fabsf(g[1]) + fabsf(g[2]);
    bool upright = planar > 7.5f && fabsf(n - 9.81f) < 1.2f && spinNow < .8f && !steps.walking;
    uprightSince = upright ? (uprightSince ? uprightSince : now) : 0;
    bool held = uprightSince && now - uprightSince > 600;
    creature.setHeldUp(held, now);
    // Gyro noise on a table and passive carrying are visual cues, not activity.
  }
  float accelLength = motion::len3(a);
  bool heldStill = fabsf(accelLength - 9.81f) < .85f &&
                   fabsf(g[0]) + fabsf(g[1]) + fabsf(g[2]) < .45f;
  handleSurroundings(a, g, heldStill, now);
  // A face-down pause produces a puzzled, asymmetric expression; left face
  // down, it goes to sleep (a way to switch it off without the touch screen).
  static uint32_t faceDownSince = 0;
  static bool faceDownReacted = false;
  if (a[2] * gazeFlatUpSign < -8.5f && fabsf(g[0]) + fabsf(g[1]) < 1.2f) {
    if (!faceDownSince) faceDownSince = now;
    if (!faceDownReacted && now - faceDownSince > 500) {
      reactMotion(Mood::SASSY, now, 1500);
      faceDownReacted = true;
    }
    if (FACE_DOWN_SLEEP_MS && !sleepPreparing && !finger.down &&
        now - lastActivity >= FACE_DOWN_SLEEP_MS && now - faceDownSince > FACE_DOWN_SLEEP_MS) {
      if (LOG_SHAKE) FACE_LOG(println, "motion: face down, going to sleep");
      startSleepAnimation(now);
    }
  } else {
    faceDownSince = 0;
    faceDownReacted = false;
  }
  // Held upside down: worried, then cross, then furious, like a finger held
  // on the screen. Turned back over, it huffs if it got angry, or just looks
  // puzzled. "Upside down" is relative to how it is usually held (learned
  // after it has been held upright and steady for a second, then slowly
  // adapted), so it works whichever way the sensor sits in the enclosure.
  static bool upside = false, haveUsual = false;
  static uint32_t upsideCandidate = 0, uprightSince = 0;
  static float usualX = 0, usualY = 1;
  float planar = sqrtf(a[0] * a[0] + a[1] * a[1]);  // gravity across the screen
  float dirX = planar > .1f ? a[0] / planar : 0, dirY = planar > .1f ? a[1] / planar : 0;
  bool upright = planar > 7.0f && fabsf(g[0]) + fabsf(g[1]) + fabsf(g[2]) < .6f;
  if (!upside && upright) {
    if (!haveUsual) {
      uprightSince = uprightSince ? uprightSince : now;
      if (now - uprightSince >= 1000) { usualX = dirX; usualY = dirY; haveUsual = true; }
    } else if (dirX * usualX + dirY * usualY > .5f) {
      usualX += (dirX - usualX) * .002f;  // drifts toward how it is held (~15 s)
      usualY += (dirY - usualY) * .002f;
      float n = sqrtf(usualX * usualX + usualY * usualY);
      usualX /= n; usualY /= n;
    }
  } else if (!haveUsual) {
    uprightSince = 0;
  }
  float facing = dirX * usualX + dirY * usualY;    // 1 as usual, -1 upside down
  bool upsideNow = haveUsual && planar > 6.5f && facing < -.75f && shaker.strength < 1.5f;
  if (!upside) {
    upsideCandidate = upsideNow ? (upsideCandidate ? upsideCandidate : now) : 0;
    if (upsideCandidate && now - upsideCandidate >= UPSIDE_DOWN_MS && !sleepPreparing) {
      upside = true;
      creature.setUpsideDown(true, now);
      reactMotion(Mood::UPSIDE_DOWN, now, 600);
      if (LOG_SHAKE) FACE_LOG(println, "motion: upside down");
    }
  } else if (planar < 4.0f || facing > -.4f || sleepPreparing) {
    upside = false;
    upsideCandidate = 0;
    float anger = creature.annoyance();
    creature.setUpsideDown(false, now);
    if (anger > .35f && !sleepPreparing) creature.huff(anger, now);
    else if (!sleepPreparing) reactMotion(Mood::CONFUSED, now, 700);
    if (LOG_SHAKE) FACE_LOG(printf, "motion: turned back over (anger %.2f)\n", anger);
  } // Remaining in one upside-down orientation is not ongoing interaction.
  // Idle eye motion and merely being held up do not restart the activity timer.
}

// The QMI8658 measures its own chip temperature, not sound or room temperature.
// Temperature entry cues use hysteresis and run once per sustained condition.
void handleTemperature(uint32_t now) {
  if (!imuReady || sleepPreparing || now-lastTempRead < 1000) return;
  lastTempRead=now;
  { I2CGuard bus; dieTempC=QMI8658_readTemp(); }
  if (dieTempC < -30 || dieTempC > 90) return;
  static bool coldReacted=false,hotReacted=false;
  static uint32_t hotSince=0;
  if (dieTempC < 18) {
    if (!coldSince) coldSince=now;
    if (!coldReacted && now-coldSince >= 8000)
      coldReacted=creature.reactPassive(Mood::SHIVER,now,1700);
  } else if (dieTempC > 20) { coldSince=0; coldReacted=false; }
  if (dieTempC > HOT_C) {
    if (!hotSince) hotSince=now;
    if (!hotReacted && now-hotSince >= 10000)
      hotReacted=creature.reactPassive(Mood::YAWN,now,1900);
  } else if (dieTempC < HOT_C-2) { hotSince=0; hotReacted=false; }
}

bool touchRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(0x15);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

int readTouchRegister(uint8_t reg) {
  Wire.beginTransmission(0x15);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x15, 1) != 1) return -1;
  return Wire.read();
}

void configureTouchWake() {
  // Keep the touch chip scanning while the CPU/LCD sleep. Slow standby scans
  // can miss quick taps. Critical battery sleep still sends full touch sleep.
  bool configured = touchRegister(0xFE, 1);
  configured &= touchRegister(0xEC, 0x00);
  // 20 ms pulse makes it easier for EXT1 to catch than the 1 ms default.
  configured &= touchRegister(0xED, 200);
  int previous = readTouchRegister(0xFA);
  uint8_t irqCtl = uint8_t(previous < 0 ? 0 : previous);
  // Touch detection + contact changes; never retain the periodic IRQ test bit.
  configured &= touchRegister(0xFA, uint8_t((irqCtl & ~0x80) | 0x60));
  int scanning = readTouchRegister(0xFE), irq = readTouchRegister(0xFA);
  configured &= scanning == 1 && irq >= 0 && (irq & 0xE0) == 0x60;
  FACE_LOG(printf, "Touch wake config: %s, active_scan=%d irq=%02X\n",
           configured ? "verified" : "FAILED", scanning, irq);
}

uint8_t rawTouchFingerCount() {
  I2CGuard bus;
  Wire.beginTransmission(0x15);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x15, 1) != 1)
    return 0xFF; // unknown; do not mistake an I2C failure for a release
  return Wire.read();
}

bool confirmTouchWake() {
  // The first tap woke the ESP32. Keep the LCD dark while counting TOUCH_WAKE_TAPS
  // separate presses, with release debounce and no more than 650 ms apart.
  if (TOUCH_WAKE_TAPS == 1) return true; // the latched touch IRQ is the first press
  pinMode(TOUCH_WAKE_PIN, INPUT_PULLUP);
  uint32_t began = millis();
  TouchWakeCheck check;
  check.begin(began, rawTouchFingerCount());
  // The touch IRQ already represents the first press, even if a quick tap
  // lifted before CPU startup. Single-touch wake must not wait for another.
  if (check.complete()) return true;
  while (millis() - began < TOUCH_WAKE_WINDOW_MS) {
    if (check.feed(rawTouchFingerCount(), millis())) return true;
    delay(5);
  }
  return false;
}

bool acceptPendingWakeTouch(bool &confirmed) {
  if (!confirmed && TOUCH_ENABLED && touch.hasPending()) {
    confirmed = confirmTouchWake();
    if (confirmed) {
      touch.clearPending();
      FACE_LOG(printf, "Touch captured during wake startup at %lu ms\n", millis());
    }
  }
  return confirmed;
}

void rawImuWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(0x6B); // address of QMI8658 on this Waveshare board
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t rawImuRead(uint8_t reg) {
  Wire.beginTransmission(0x6B);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x6B, 1) != 1) return 0;
  return Wire.read();
}

void exitMotionWake() {
  bool wasSynced = imuSyncEnabled || (rawImuRead(QMI8658Register_Ctrl7) & 0x80);
  // The IMU keeps WoM state through ESP32 deep sleep AND upload/reset. Rev 0.9 requires the
  // zero-threshold CTRL9 command before returning to normal accel/gyro mode.
  rawImuRead(QMI8658Register_Status1); // release the latched interrupt
  rawImuWrite(QMI8658Register_Ctrl7, 0);
  rawImuWrite(QMI8658Register_Ctrl8, 0x80);
  rawImuWrite(QMI8658Register_Cal1_L, 0);
  rawImuWrite(QMI8658Register_Ctrl9, QMI8658_Ctrl9_Cmd_WoM_Setting);
  for (int i = 0; i < 60; ++i) {
    if (rawImuRead(QMI8658Register_StatusInt) & 0x80) break;
    delay(2);
  }
  rawImuWrite(QMI8658Register_Ctrl9, 0x00); // acknowledge the command
  if (wasSynced) configureImuSync(false);
}

bool configureImuSync(bool enabled) {
  // QMI8658C section 10: I2C sample locking also requires disabling internal
  // AHB clock gating with CTRL9 command 0x12. Restore gating before WoM.
  QMI8658_setRawReadLock(false); imuSyncEnabled = false;
  rawImuWrite(QMI8658Register_Ctrl7, 0);
  delayMicroseconds(800); rawImuRead(QMI8658Register_Gz_H);
  rawImuWrite(QMI8658Register_Ctrl8, rawImuRead(QMI8658Register_Ctrl8) | 0x80);
  rawImuWrite(QMI8658Register_Ctrl9, 0);
  for (int i = 0; i < 30 && (rawImuRead(QMI8658Register_StatusInt) & 0x80); ++i) delay(1);
  rawImuWrite(QMI8658Register_Cal1_L, enabled ? 1 : 0);
  rawImuWrite(QMI8658Register_Ctrl9, 0x12);
  bool accepted = false;
  for (int i = 0; i < 60; ++i) {
    if (rawImuRead(QMI8658Register_StatusInt) & 0x80) { accepted = true; break; }
    delay(2);
  }
  rawImuWrite(QMI8658Register_Ctrl9, 0);
  if (enabled) {
    rawImuWrite(QMI8658Register_Ctrl7, accepted ? 0x83 : 0x03);
    imuSyncEnabled = accepted;
    QMI8658_setRawReadLock(accepted);
    FACE_LOG(println, accepted ? "IMU synchronized sample locking enabled" : "IMU sample lock unavailable; checked burst reads retained");
  }
  return accepted;
}

bool armMotionWake() {
  if (imuSyncEnabled) configureImuSync(false);
  // The bundled Waveshare WoM helper never sends CTRL9 and uses INT1.
  // This board connects INT2 to RTC-capable GPIO3.
  QMI8658_enableSensors(QMI8658_CTRL7_DISABLE_ALL);
  QMI8658_config_acc(QMI8658AccRange_2g, QMI8658AccOdr_LowPower_21Hz,
                     QMI8658Lpf_Disable, QMI8658St_Disable);
  QMI8658_write_reg(QMI8658Register_Ctrl8, 0x80); // route CTRL9 done to STATUSINT bit 7
  QMI8658_write_reg(QMI8658Register_Cal1_L, WOM_THRESHOLD_MG);
  QMI8658_write_reg(QMI8658Register_Cal1_H, 0xC0 | 4); // INT2 idle high, 4-sample blanking
  QMI8658_write_reg(QMI8658Register_Ctrl9, QMI8658_Ctrl9_Cmd_WoM_Setting);
  bool accepted = false;
  uint8_t lastStatus = 0;
  for (int i = 0; i < 60; ++i) {
    uint8_t status = 0;
    // Rev 0.9 silicon signals CTRL9 completion in STATUSINT bit 7.
    // The older bundled header incorrectly names STATUS1 bit 0.
    QMI8658_read_reg(QMI8658Register_StatusInt, &status, 1);
    lastStatus = status;
    if (status & 0x80) { accepted = true; break; }
    delay(2);
  }
  if (!accepted) {
    uint8_t ctrl2 = 0, ctrl7 = 0, ctrl8 = 0, ctrl9 = 0, calL = 0, calH = 0;
    QMI8658_read_reg(QMI8658Register_Ctrl2, &ctrl2, 1);
    QMI8658_read_reg(QMI8658Register_Ctrl7, &ctrl7, 1);
    QMI8658_read_reg(QMI8658Register_Ctrl8, &ctrl8, 1);
    QMI8658_read_reg(QMI8658Register_Ctrl9, &ctrl9, 1);
    QMI8658_read_reg(QMI8658Register_Cal1_L, &calL, 1);
    QMI8658_read_reg(QMI8658Register_Cal1_H, &calH, 1);
    FACE_LOG(printf, "WoM regs: statusInt=%02X ctrl2=%02X ctrl7=%02X ctrl8=%02X ctrl9=%02X cal=%02X,%02X\n",
                  lastStatus, ctrl2, ctrl7, ctrl8, ctrl9, calL, calH);
    return false;
  }
  // Acknowledge the command so the next one is accepted (CTRL_CMD_ACK).
  QMI8658_write_reg(QMI8658Register_Ctrl9, 0x00);
  for (int i = 0; i < 30; ++i) {
    uint8_t st = 0;
    QMI8658_read_reg(QMI8658Register_StatusInt, &st, 1);
    if (!(st & 0x80)) break;
    delay(1);
  }
  QMI8658_enableSensors(QMI8658_CTRL7_ACC_ENABLE);
  // The QMI8658A keeps its INT pins high-impedance until CTRL1 bit 4 (INT2)
  // is set; without this the motion alarm never reaches GPIO3, so shaking
  // could not wake the board. (The Waveshare init writes CTRL1 = 0x60.)
  uint8_t ctrl1 = 0;
  QMI8658_read_reg(QMI8658Register_Ctrl1, &ctrl1, 1);
  QMI8658_write_reg(QMI8658Register_Ctrl1, ctrl1 | 0x10);
  uint8_t status = 0;
  QMI8658_read_reg(QMI8658Register_Status1, &status, 1); // clear stale WoM event
  return true;
}

void enterCriticalSleep(bool lcdReady) {
  criticalBatterySleep = true;
  stopMotionSampler();
  if (imuReady) {
    exitMotionWake();
    QMI8658_enableSensors(QMI8658_CTRL7_DISABLE_ALL);
  } else rawImuWrite(QMI8658Register_Ctrl7, 0); // early boot: disable without normal-mode init
  if (!lcdReady) { DEV_Module_Init(); DEV_SET_PWM(0); }
  LCD_1IN28_Sleep();
  if (TOUCH_ENABLED) touch.sleep(); // critical sleep has no touch wake
  analogWrite(LCD_BL_PIN, 0);
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);
  gpio_hold_en(GPIO_NUM_2);
  gpio_deep_sleep_hold_en();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(uint64_t(BATTERY_RECHECK_S) * 1000000ULL);
  FACE_LOG(println, "Battery <=3%: sleeping; shake/touch disabled until charge recovers");
  persistBatteryMemory(true);
  Serial.flush();
  esp_deep_sleep_start();
}

bool enterDeepSleep(bool lcdReady) {
  handleBatterySerial();
  if (lcdReady) {
    readBattery();
    // Finish a pending cable vote before committing to display-off/deep sleep.
    for (uint8_t vote = 0; vote < 2 && batterySupply.chargeSuspected(); ++vote) {
      delay(BATTERY_LIVE_POWER_CHECK_MS); readBattery();
    }
    if (!sleepPreparing) return false;
  }
  if (batteryLow()) { enterCriticalSleep(lcdReady); return true; }
  stopMotionSampler(); // the IMU is reconfigured below
  if (!lcdReady) {
    // Cold/reset boot and rejected wakes also put the LCD controller to sleep,
    // not just its backlight. Initialize the bus without lighting the panel.
    DEV_Module_Init();
    DEV_SET_PWM(0);
    LCD_1IN28_Sleep();
    if (TOUCH_ENABLED) touch.begin(FALLING); // restart from the prior touch-disabled firmware
  }
  if (TOUCH_ENABLED) configureTouchWake();
  if (imuReady && !armMotionWake()) {
    FACE_LOG(println, "WoM setup failed; staying dark and retrying in 5 s");
    exitMotionWake();
    QMI8658_init();
    tuneImuFilters();
    startMotionSampler();
    sleepRetryAt = millis() + 5000;
    return false;
  }
  pinMode(IMU_WAKE_PIN, INPUT_PULLUP);
  pinMode(TOUCH_WAKE_PIN, INPUT_PULLUP);
  // A currently asserted wake line causes an instant reboot. Let it clear.
  for (int i = 0; imuReady && i < 25 && digitalRead(IMU_WAKE_PIN) == LOW; ++i) {
    uint8_t status = 0;
    QMI8658_read_reg(QMI8658Register_Status1, &status, 1);
    delay(10);
  }
  if (imuReady && digitalRead(IMU_WAKE_PIN) == LOW) {
    exitMotionWake();
    QMI8658_init();
    tuneImuFilters();
    startMotionSampler();
    sleepRetryAt = millis() + 5000;
    return false;
  }
  // Input may arrive while WoM is being armed. Do not exclude an asserted
  // touch IRQ and go to sleep underneath an active-session interaction.
  if (lcdReady) {
    readBattery();
    for (uint8_t vote = 0; vote < 2 && batterySupply.chargeSuspected(); ++vote) {
      delay(BATTERY_LIVE_POWER_CHECK_MS); readBattery();
    }
    if (!sleepPreparing) {
      if (imuReady) { exitMotionWake(); imuReady = QMI8658_init() != 0; if (imuReady) tuneImuFilters(); }
      startMotionSampler();
      return false;
    }
  }
  if (lcdReady && awakeLimit.active && TOUCH_ENABLED && !touchStuck) {
    handleTouch(millis());
    if (!sleepPreparing || confirmedTouchContact()) {
      exitMotionWake();
      QMI8658_init(); tuneImuFilters(); startMotionSampler();
      recordActivity(millis());
      return false;
    }
  }
  if (lcdReady) LCD_1IN28_Sleep();
  if (lcdReady && awakeLimit.active && TOUCH_ENABLED && !touchStuck) {
    handleTouch(millis());
    if (!sleepPreparing || confirmedTouchContact()) {
      LCD_1IN28_Wake(); renderer.invalidate();
      if (imuReady) { exitMotionWake(); imuReady = QMI8658_init() != 0; if (imuReady) tuneImuFilters(); }
      startMotionSampler(); recordActivity(millis());
      return false;
    }
  }
  analogWrite(LCD_BL_PIN, 0);
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);
  gpio_hold_en(GPIO_NUM_2);
  gpio_deep_sleep_hold_en();
  uint64_t pins = imuReady ? 1ULL << IMU_WAKE_PIN : 0;
  // Restore testing: touch IRQ and IMU INT2 can wake the CPU.
  if (TOUCH_ENABLED && !touchStuck) pins |= 1ULL << TOUCH_WAKE_PIN;
  // Keep both wake lines pulled up while asleep so neither floats low (a
  // phantom wake) or can't be pulled low (no wake). Digital pull-ups set by
  // pinMode() switch off in deep sleep; the RTC ones need RTC_PERIPH powered.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  for (gpio_num_t pin : {gpio_num_t(IMU_WAKE_PIN), gpio_num_t(TOUCH_WAKE_PIN)}) {
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (pins) esp_sleep_enable_ext1_wakeup(pins, ESP_EXT1_WAKEUP_ANY_LOW);
  if (!pins) esp_sleep_enable_timer_wakeup(uint64_t(BATTERY_RECHECK_S) * 1000000ULL);
  FACE_LOG(println, "Entering deep sleep");
  persistBatteryMemory(true);
  Serial.flush();
  esp_deep_sleep_start();
  return true;
}

// After the IMU's coarse motion alarm woke the CPU (screen still dark): turn
// the face on after a brief, brisk recorded left/right pattern. Slow dangling,
// walking, a bump and a single sweep leave the display dark.
bool confirmMotionWake(bool &touchConfirmed) {
  if (acceptPendingWakeTouch(touchConfirmed)) return true;
  delay(70); // let the gyro settle after leaving low-power mode
  uint32_t began = millis(), lastLogAt = 0;
  LeftRightWakeCheck shake;
  while (millis() - began < DIRECTIONAL_WAKE_HOLD_MS + 3000) {
    if (acceptPendingWakeTouch(touchConfirmed)) return true;
    float a[3], g[3];
    if (!readMotion(a, g)) { delay(3); continue; }
    uint32_t now = millis();
    if (shake.feed(a, g, now)) return true;
    if (LOG_SHAKE && now - lastLogAt >= 500) {
      lastLogAt = now;
      FACE_LOG(printf, "wake: left/right %lu / %lu ms, AY %.0f%%, GZ %.0f%%, Z/Y %.2f, swings %u%s\n",
                    (unsigned long)shake.shakingFor(now), (unsigned long)DIRECTIONAL_WAKE_HOLD_MS,
                    shake.accelShare * 100, shake.gyroShare * 100, shake.zOverY,
                    shake.swings, shake.carryingMotion() ? " (carried motion ignored)" : "");
    }
    // Save the battery: give up quickly if no shake starts, or once it stops.
    if (!shake.shakingFor(now) && (now - began > 1200 || shake.stopped(now))) break;
    delay(3);
  }
  if (acceptPendingWakeTouch(touchConfirmed)) return true;
  if (LOG_SHAKE) FACE_LOG(println, "wake check: no sustained left/right gesture; back to sleep");
  return false;
}

void renderFrame() {
  uint32_t nowUs = micros();
  bool napping = sleepPreparing && creature.sleepFinished(millis());
  if (nowUs - lastFrameUs < (napping ? NAP_FRAME_US : FRAME_US)) return;
  // Frame-independent animation: everything advances by real elapsed time.
  float dt = (nowUs - lastFrameUs) / 1000000.0f;
  lastFrameUs = nowUs;
  if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
  creature.update(dt, millis());
  eyes.draw();
  applyBacklight(creature.backlight());
  if (LOG_FPS) {
    static uint32_t frames = 0, windowStart = 0;
    ++frames;
    if (millis() - windowStart >= 5000) {
      FACE_LOG(printf, "%.1f FPS\n", frames * 1000.0f / (millis() - windowStart));
      frames = 0;
      windowStart = millis();
    }
  }
}

void setup() {
  Serial.begin(115200);
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_2);
  // Pins that woke the chip stay in RTC mode until released; GPIO5 must be a
  // normal GPIO again for the touch interrupt.
  rtc_gpio_deinit(gpio_num_t(IMU_WAKE_PIN));
  rtc_gpio_deinit(gpio_num_t(TOUCH_WAKE_PIN));
  if (TOUCH_ENABLED) touch.listen(FALLING); // retain taps during all blocking startup work
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);

  Wire.begin(6, 7);
  Wire.setClock(400000);
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  restoreBatteryMemory();
  readBattery(); // before lighting the screen or accepting a wake
  if (batterySupply.chargeSuspected()) { delay(POWER_CHECK_MS); readBattery(); }
  if (BatteryState::mustRemainAsleep(batterySupply.rawPercent, batterySupply.measuredVolts, criticalBatterySleep, batterySupply.charging)) {
    enterCriticalSleep(false);
    return;
  }
  criticalBatterySleep = false;
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  uint64_t source = cause == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
  FACE_LOG(printf, "Wake cause=%d, pins=0x%llX\n", int(cause), source);
  bool touchWake = TOUCH_ENABLED && (source & (1ULL << TOUCH_WAKE_PIN)) != 0;
  bool touchConfirmed = touchWake && (RAW_IMU_SERIAL_ONLY || confirmTouchWake());
  if (touchConfirmed) touch.clearPending();
  acceptPendingWakeTouch(touchConfirmed);
  // Upload/reset restarts only the ESP32, not the separately powered IMU.
  // Clear retained WoM even when this boot was not an EXT1 sleep wake.
  exitMotionWake();
  imuReady = QMI8658_init() != 0;
  if (imuReady) tuneImuFilters();
  if (imuReady && cause == ESP_SLEEP_WAKEUP_UNDEFINED) calibrateGazeGyro();
  FACE_LOG(println, imuReady ? "Motion sensor ready" :
                 "Motion sensor NOT found: shake, tilt and shake-wake are disabled");
  acceptPendingWakeTouch(touchConfirmed);
  if (!RAW_IMU_SERIAL_ONLY && START_ASLEEP && !source && !touchConfirmed) {
    FACE_LOG(printf, "Starting asleep; %u touch%s or brisk left/right shake (%lu ms confirmation) to wake\n",
             unsigned(TOUCH_WAKE_TAPS), TOUCH_WAKE_TAPS == 1 ? "" : "es",
             (unsigned long)DIRECTIONAL_WAKE_HOLD_MS);
    if (enterDeepSleep(false)) return;
  }
  if (!RAW_IMU_SERIAL_ONLY && touchWake && !touchConfirmed && !(source & (1ULL << IMU_WAKE_PIN))) {
    FACE_LOG(println, "Touch wake incomplete; returning to sleep");
    if (enterDeepSleep(false)) return;
  }
  if (!RAW_IMU_SERIAL_ONLY && imuReady && (source & (1ULL << IMU_WAKE_PIN)) && !touchConfirmed) {
    // Motion wakes the CPU but not the LCD. Only a real shake (or twist)
    // turns the face on; it then wakes startled, dizzy and grumpy.
    if (!confirmMotionWake(touchConfirmed)) {
      FACE_LOG(println, "Motion wake rejected; returning to sleep");
      if (enterDeepSleep(false)) return;
    } else if (!touchConfirmed) wokeByShake = true;
  }
  // Internal RAM is much faster than PSRAM for per-pixel blending.
  size_t bytes = W * H * sizeof(uint16_t);
  BlackImage = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!BlackImage) BlackImage = (uint16_t *)ps_malloc(bytes);
  if (!BlackImage) BlackImage = (uint16_t *)malloc(bytes);
  if (!BlackImage) { FACE_LOG(println, "Display buffer allocation failed"); return; }
  DEV_Module_Init();
  DEV_SET_PWM(0);
  LCD_1IN28_Init(HORIZONTAL);
  if (TOUCH_ENABLED) {
    touch.begin(FALLING);
    // Do not treat the waking press as a second interaction.
    if (touchConfirmed) finger.ignoreUntilLift(millis());
  }
  Wire.setClock(400000);
  if (TOUCH_ENABLED) configureTouchWake();
  else FACE_LOG(println, "Touch disabled (TOUCH_ENABLED = false): motion gestures only");
  startMotionSampler(); // knocks and fast motion, on core 0
  randomSeed(esp_random());
  uint64_t mac = ESP.getEfuseMac();
  uint32_t unitSeed = uint32_t(mac ^ (mac >> 32)); // small per-unit quirks in the idle motion
  lastActivity = millis();
  renderer.begin(BlackImage, LCD_1IN28_DisplayWindows);
  eyes.begin(&renderer, unitSeed, lastActivity);
  creature.begin(&eyes, unitSeed, lastActivity);
  readBattery();
  lastBatteryCheck = lastActivity;
  if (!RAW_IMU_SERIAL_ONLY && START_ASLEEP && !touchConfirmed && !wokeByShake) {
    // Sleep setup failed: remain black while retrying; no automatic wake animation.
    sleepPreparing = true;
    deepSleepPlanned = true;
    creature.startSleep(lastActivity - SLEEP_SEQUENCE_MS, 0.0f);
  } else startWakeAnimation(lastActivity, wokeByShake, touchConfirmed);
  if (wokeByShake) FACE_LOG(println, "Shake confirmed; waking into dizzy eyes");
  else if (touchConfirmed) FACE_LOG(println, "Touch wake confirmed; waking face");
  // First frame: the whole screen is cleared while the backlight is still off.
  lastFrameUs = micros() - FRAME_US;
  renderFrame();
}

void loop() {
  handleBatterySerial();
  printRawImu(millis());
  if (!BlackImage) { delay(1000); return; }
  uint32_t now = millis();
  uint32_t powerCheckMs = BATTERY_LIVE_POWER_CHECK_MS;
  if (now - lastBatteryCheck >= powerCheckMs) {
    lastBatteryCheck = now;
    readBattery();
  }
  if (batteryLow()) {
    // Critical shutdown is immediate and cannot be canceled by any interaction.
    if (!sleepPreparing || !creature.asleep()) startSleepAnimation(now);
    if (creature.sleepFinished(now)) enterCriticalSleep(true);
    renderFrame();
    delay(1);
    return;
  }
  // Handle input before committing a completed closing sequence to deep sleep.
  handleTouch(now);
  now = millis();
  if (TOUCH_ENABLED && !touchStuck && sleepPreparing && awakeLimit.active && confirmedTouchContact())
    cancelSleepForTouch(now);
  handleMotion(now);
  handleTemperature(millis());
  now = millis();
  if (sleepPreparing && creature.sleepFinished(now)) {
    if (deepSleepPlanned) applyBacklight(0); // darkness precedes any sensor handshake/retry
    if (deepSleepPlanned && int32_t(now - sleepRetryAt) >= 0 && enterDeepSleep(true)) return;
    // A failed WoM handshake may have taken time while new input arrived.
    handleTouch(millis());
    if (sleepPreparing && awakeLimit.active && TOUCH_ENABLED && !touchStuck && confirmedTouchContact())
      cancelSleepForTouch(millis());
    if (sleepPreparing && awakeLimit.active) { delay(5); handleMotion(millis()); }
    if (!sleepPreparing) { renderFrame(); delay(1); return; }
    awakeLimit.finish();
    if (sleepPreparing && TOUCH_ENABLED && !touchStuck && confirmedTouchContact())
      cancelSleepForTouch(now);
    renderFrame();
    delay(1);
    return;
  }
  if (TOUCH_ENABLED && !touchStuck && sleepPreparing && confirmedTouchContact())
    cancelSleepForTouch(now);
  creature.setPointerHeld(finger.tracking() && !touchStuck && !alwaysOnGesture.reserved() && !holdToCenter.reserved(), now);
  uint32_t timeout = idleTimeout();
  if (sleepPreparing) {
    // Napping keeps the screen on. Only planned deep sleep or a nearly empty
    // battery actually switches it off.
    if (creature.sleepFinished(now) && deepSleepPlanned && int32_t(now - sleepRetryAt) >= 0) {
      if (enterDeepSleep(true)) return;
    }
  } else if (!RAW_IMU_SERIAL_ONLY && !awakeLimit.alwaysOn && int32_t(now - sleepRetryAt) >= 0 &&
             (AUTO_DEEP_SLEEP ? awakeLimit.closingDue(now) : now - lastActivity >= sleepAnimationDelay(timeout))) {
    startSleepAnimation(AUTO_DEEP_SLEEP ? awakeLimit.closingAt() : now);
  }
  if (!sleepPreparing) {
    uint32_t idleElapsed = AUTO_DEEP_SLEEP ? now - awakeLimit.startedAt : now - lastActivity;
    if (AUTO_DEEP_SLEEP) timeout = awakeLimit.timeout;
    float idle = idleElapsed / float(timeout);
    // Lively right up to the end: heavy lids only in the last couple of seconds.
    creature.setDrowsiness((RAW_IMU_SERIAL_ONLY || awakeLimit.alwaysOn) ? 0.0f : anim::smoothstep(.82f, 1.0f, idle));
    creature.setIdleActsAllowed(RAW_IMU_SERIAL_ONLY || awakeLimit.alwaysOn || idleElapsed + 1800 < timeout);
  }
  renderFrame();
  delay(1);
}
