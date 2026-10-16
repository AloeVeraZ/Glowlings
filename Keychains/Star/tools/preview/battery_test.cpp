#include <stdio.h>
#include <stdlib.h>
#include <initializer_list>
#include "BatteryState.h"
#include "InteractionPolicy.h"
#include "MotionGestures.h"
#include "TouchTracker.h"
#include "palette.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); ++fails; } } while (0)
static uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
static unsigned pushedWindows=0;
static void countWindow(uint16_t,uint16_t,uint16_t,uint16_t,uint16_t*) { ++pushedWindows; }

static void saveFrame(const char *directory, const char *name) {
  char path[1024]; snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
  FILE *f = fopen(path, "wb"); if (!f) { perror(path); exit(2); }
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
  for (uint16_t value : fb) {
    uint16_t c = (value << 8) | (value >> 8);
    const unsigned char rgb[] = {static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31),
      static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63), static_cast<unsigned char>((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

int main(int argc, char **argv) {
  int fails = 0;
  BatteryState battery;
  const int levels[] = {100, 50, 25, 10, 5, 3};
  float fatigue = -1, sadness = -1;
  for (int i = 0; i < 6; ++i) {
    battery.update(levels[i]);
    CHECK(int(battery.stage) == i);
    CHECK(battery.fatigue() >= fatigue && battery.sadness() >= sadness);
    fatigue = battery.fatigue(); sadness = battery.sadness();
  }
  battery.update(5); CHECK(battery.stage == BatteryState::CRITICAL);
  battery.update(8); CHECK(battery.stage == BatteryState::VERY_LOW);
  battery.update(10); CHECK(battery.stage == BatteryState::VERY_LOW);
  battery.update(11); CHECK(battery.stage == BatteryState::VERY_LOW);
  battery.update(13); CHECK(battery.stage == BatteryState::LOW_CHARGE);
  battery.update(-1); CHECK(battery.stage == BatteryState::NORMAL && battery.fatigue() == 0);
  CHECK(battery.color() == EYE_COLOR);
  fatigue = sadness = -1;
  for (int percent = 100; percent >= 0; percent -= 10) {
    battery.update(percent);
    CHECK(battery.fatigueStep == (100 - percent) / 10);
    CHECK(battery.fatigue() > fatigue && battery.sadness() > sadness);
    fatigue = battery.fatigue(); sadness = battery.sadness();
    if (percent == 100) CHECK(battery.color() == EYE_COLOR);
    if (percent == 50) CHECK(battery.color() == BATTERY_YELLOW_COLOR);
    if (percent <= 10) CHECK(battery.color() == BATTERY_RED_COLOR);
  }
  battery.update(-1); battery.update(90);
  battery.update(91); CHECK(battery.fatigueStep == 1);
  battery.update(92); CHECK(battery.fatigueStep == 1);
  battery.update(93); CHECK(battery.fatigueStep == 0);
  battery.update(100);
  uint32_t lastColor = battery.color();
  for (int percent = 99; percent >= 0; --percent) {
    battery.update(percent);
    uint32_t color = battery.color();
    for (int shift = 16; shift >= 0; shift -= 8)
      CHECK(abs(int((color >> shift) & 255) - int((lastColor >> shift) & 255)) <= 5);
    lastColor = color;
  }
  CHECK(BatteryState::estimate(2.0f) == -1 && BatteryState::estimate(4.8f) == -1);
  CHECK(BatteryState::estimate(2.9f) == 0 && BatteryState::estimate(3.3f) == 0);
  CHECK(BatteryState::estimate(4.2f) == 100);
  CHECK(BatteryState::mustRemainAsleep(3, 3.45f, false));
  CHECK(BatteryState::mustRemainAsleep(7, 3.57f, true));
  CHECK(BatteryState::mustRemainAsleep(-1, 2.5f, true));
  CHECK(!BatteryState::mustRemainAsleep(8, 3.60f, true));
  CHECK(BatteryState::mustRemainAsleep(-1, 4.8f, true)); // VBAT alone is not a USB signal
  CHECK(!BatteryState::mustRemainAsleep(-1, 4.0f, true, true));
  CHECK(!BatteryState::mustRemainAsleep(50, 3.86f, false));
  CHECK(!BatteryState::mustRemainAsleep(3, 4.35f, true, true));
  for (uint32_t began : {100u, UINT32_MAX - 1000u}) {
    BatterySupply supply;
    supply.sample(3.732f, began);
    CHECK(supply.percent == 21 && !supply.charging);
    supply.sample(4.0f, began + 1000); // measured plug-in behavior, below old 4.45 V cutoff
    CHECK(!supply.charging && supply.chargeSuspected() && supply.percent == 21);
    for (uint32_t extra = 1; extra < 900; extra += 100) supply.sample(4.0f, began + 1000 + extra);
    CHECK(!supply.charging); // spam reads cannot confirm a single spike
    supply.sample(4.0f, began + 2000);
    CHECK(supply.charging && supply.percent == 21); // no 21 -> 79% voltage jump
    supply.sample(4.15f, began + 20000);
    CHECK(supply.charging && supply.percent == 21);
    supply.sample(3.78f, began + 21000);
    CHECK(supply.charging && supply.percent == 21); // one drop is not an unplug confirmation
    supply.sample(3.78f, began + 22000);
    CHECK(!supply.charging && supply.percent == 21); // unplug never replaces SOC with terminal voltage
    supply.sample(3.70f, began + 23000);
    CHECK(supply.percent == 21); // transition hold
    supply.sample(3.70f, began + 32000);
    CHECK(supply.percent == 21);
    supply.sample(9.0f, began + 33000);
    CHECK(!supply.charging); // invalid ADC sample cannot assert charging
    BatterySupply usbBoot;
    usbBoot.restore(3.732f, 21);
    usbBoot.sample(3.963f, began);
    CHECK(!usbBoot.charging && usbBoot.chargeSuspected() && usbBoot.percent == 21);
    usbBoot.sample(3.969f, began + 1000);
    CHECK(usbBoot.charging && usbBoot.percent == 21); // boot restoration holds prior estimate
    BatterySupply slept;
    slept.restore(usbBoot.volts, usbBoot.percent, usbBoot.charging, usbBoot.peakVolts);
    slept.sample(3.97f, 100); CHECK(slept.charging && slept.percent == 21);
    slept.sample(3.78f, 1100); CHECK(slept.charging);
    slept.sample(3.78f, 2100); CHECK(!slept.charging && slept.percent == 21);
    BatterySupply noise;
    noise.sample(3.732f, began);
    noise.sample(4.0f, began + 1000); noise.sample(3.732f, began + 2000);
    CHECK(!noise.charging && !noise.chargeSuspected() && noise.percent == 21);
    for (uint32_t seconds = 3; seconds <= 60; ++seconds)
      noise.sample(3.732f + (seconds % 2 ? .006f : -.006f), began + seconds * 1000);
    CHECK(!noise.charging); // quiet ADC noise must not make the eyes green
    BatterySupply gradual;
    gradual.sample(3.732f, began);
    for (uint32_t seconds = 1; seconds <= 10; ++seconds)
      gradual.sample(3.732f + seconds * .003f, began + seconds * 1000);
    CHECK(gradual.charging && gradual.percent == 21);
    BatterySupply fullBattery;
    fullBattery.sample(4.2f, began); fullBattery.sample(4.2f, began + 1000);
    CHECK(!fullBattery.charging && fullBattery.percent == 100); // no absolute-voltage USB inference
  }
  for (uint32_t began : {100u, UINT32_MAX - 1000u}) {
    BatterySupply gauge;
    gauge.restore(3.741f, 22); // keep the collected 22% unplug baseline
    gauge.sample(3.963f, began); gauge.sample(3.969f, began + 1000);
    CHECK(gauge.charging && gauge.percent == 22);
    for (uint32_t seconds = 2; seconds <= 180; ++seconds) {
      int before = gauge.percent;
      gauge.sample(4.07f, began + seconds * 1000);
      CHECK(gauge.charging && gauge.percent >= before && gauge.percent <= before + 1);
      if (seconds < 20) CHECK(gauge.percent == 22);
    }
    CHECK(gauge.percent > 22 && gauge.percent < 45); // charging makes gradual measured progress; no permanent freeze
    int beforeUnplug = gauge.percent;
    gauge.sample(4.0f, began + 181000); gauge.sample(4.0f, began + 182000);
    CHECK(!gauge.charging && gauge.percent == beforeUnplug); // reproduce the old 22 -> 80% bug
    gauge.sample(4.17f, began + 183000); gauge.sample(4.17f, began + 184000);
    CHECK(gauge.charging && gauge.percent == beforeUnplug); // immediate replug cannot change SOC
    auto saved = gauge.snapshot();
    BatterySupply reboot;
    CHECK(reboot.restore(saved));
    for (uint32_t second = 0; second < 10; ++second) reboot.sample(4.17f, second * 1000);
    CHECK(reboot.percent == gauge.percent && reboot.charging); // transition state survives reset
    auto corrupt = saved; corrupt.version = 1; CHECK(!reboot.restore(corrupt));
    corrupt = saved; corrupt.filtered = NAN; CHECK(!reboot.restore(corrupt));
    corrupt = saved; corrupt.percent = 101; CHECK(!reboot.restore(corrupt));
    BatterySupply low;
    low.restore(4.06f, 87);
    low.sample(3.40f, began);
    CHECK(low.percent == 87 && low.rawPercent <= CRITICAL_BATTERY_PERCENT);
    CHECK(BatteryState::mustRemainAsleep(low.rawPercent, low.measuredVolts, false)); // smoothing cannot delay safety
    BatterySupply jitter;
    jitter.restore(4.07f, 88);
    for (uint32_t seconds = 0; seconds < 120; ++seconds) {
      jitter.sample(seconds % 2 ? 4.064f : 4.077f, began + seconds * 1000);
      CHECK(jitter.percent == 88 && !jitter.charging); // 87/88/89 target noise does not chatter
    }
    BatterySupply full;
    full.restore(4.1f, 98, true, 4.2f);
    for (uint32_t seconds = 0; seconds < 120; ++seconds) full.sample(4.2f, began + seconds * 1000);
    CHECK(full.percent <= 99 && full.percent >= 98); // charger CV voltage is not evidence of full capacity
    BatterySupply commissioned;
    commissioned.restore(4.07f, 88); commissioned.sample(4.14f, began);
    commissioned.confirmUsbPower();
    CHECK(commissioned.charging && commissioned.percent == 88); // confirmed host presence with steady near-full cell
    BatterySupply retained;
    CHECK(retained.restore(commissioned.snapshot()));
    retained.sample(4.14f, 0);
    CHECK(retained.charging && retained.percent == 88);
  }
  battery.update(21, true);
  CHECK(battery.percent == 21 && battery.color() == BATTERY_CHARGING_COLOR);
  CHECK(battery.fatigue() == 0 && battery.sadness() == 0);
  battery.update(21, false);
  CHECK(battery.color() != EYE_COLOR && battery.fatigue() > 0);
  int previous = 0;
  for (float v = 3.3f; v <= 4.3f; v += .001f) {
    int p = BatteryState::estimate(v);
    CHECK(p >= previous && p <= 100); previous = p;
  }
  CHECK(START_ASLEEP && AUTO_DEEP_SLEEP && !WALKING_KEEPS_AWAKE);
  CHECK(TOUCH_ENABLED && TOUCH_WAKE_TAPS == 1 && TOUCH_STUCK_MS == 0);
  CHECK(sleepAnimationDelay(IDLE_SLEEP_MS) + SLEEP_SEQUENCE_MS == 15000);
  CHECK(sleepAnimationDelay(WAKE_AWAKE_MS) + SLEEP_SEQUENCE_MS == 30000);
  for (uint32_t began : {100u, UINT32_MAX - 1000u}) {
    AwakeLimit limit; limit.start(began);
    for (uint32_t elapsed = 0; elapsed < 30000; ++elapsed) {
      // No interaction: finish closing at the initial 30-second deadline.
      CHECK(limit.closingDue(began + elapsed) == (elapsed >= 24600));
    }
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 30000);
    limit.interaction(began + 29000); // touch during closing cancels shutdown
    CHECK(!limit.closingDue(began + 30000));
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 44000);
    limit.interaction(began + 42000);
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 57000);
    CHECK(!limit.closingDue(began + 50000));
    CHECK(limit.closingDue(began + 57000));
    limit.start(began);
    limit.interaction(began + 100); // every touch starts exactly 15 seconds of inactivity time
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 15100);
    for (uint32_t elapsed = 1000; elapsed <= 180000; elapsed += 1000) {
      limit.interaction(began + elapsed);
      CHECK(!limit.closingDue(began + elapsed));
    }
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == began + 195000);
    CHECK(!limit.closingDue(began + 189599));
    CHECK(limit.closingDue(began + 189600));
    limit.finish(); CHECK(!limit.closingDue(began + 40000));
    limit.start(began + 50000); CHECK(!limit.closingDue(began + 50000));
  }
  for (uint32_t began : {100u, UINT32_MAX - 1000u}) {
    // Ten real taps must remain countable through the five-tap protected pause.
    ScreenTapRun taps; TouchTracker finger;
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, began);
    creature.begin(&eyes, 1234, began); creature.startWake(began, false);
    for (unsigned tap = 1; tap <= 10; ++tap) {
      uint32_t now = began + tap * 200;
      CHECK(finger.feed(120, 120, 0, 0, true, now).kind == TouchTracker::PRESS);
      auto release = finger.feed(120, 120, 1, 0, true, now + 50);
      CHECK(release.kind == TouchTracker::TAP);
      unsigned count = taps.add(now + 50);
      CHECK(count == tap);
      if (count == 5) creature.startTouchAngerPause(now + 50);
      if (count > 5 && count < 10) CHECK(creature.touchAngerPauseActive(now + 50));
      if (count == TOUCH_BATTERY_TAPS) creature.showBattery(73, now + 50);
      creature.update(.016f, now + 50);
    }
    CHECK(creature.mood() == Mood::BATTERY);
    CHECK(!creature.touchAngerPauseActive(began + 2050));
    CHECK(!creature.react(Mood::BOOP, began + 2100, 600)); // give time to read it
    CHECK(taps.add(began + 4000) == 1); // a gap ends the run
  }
  for (uint32_t began : {100u, UINT32_MAX - 1000u}) {
    TouchWakeCheck taps;
    taps.begin(began, 1);
    CHECK(taps.complete()); // first press wakes without requiring a release
    for (uint32_t t = 5; t < 1000; t += 5) CHECK(!taps.feed(1, began + t));
    CHECK(taps.count == 1); // a continuous hold never counts as several taps
    CHECK(!taps.feed(0xFF, began + 1000));
    CHECK(!taps.feed(0, began + 1005));
    CHECK(taps.count == 0); // a long press is not part of a tap run
    taps.begin(began, 0); // first tap has lifted before CPU boot completes
    CHECK(taps.complete()); // a quick tap still wakes without a second press
    for (uint32_t tap = 2; tap <= TOUCH_WAKE_TAPS; ++tap) {
      uint32_t press = began + (tap - 1) * 200;
      CHECK(taps.feed(1, press) == (tap == TOUCH_WAKE_TAPS));
      CHECK(!taps.feed(1, press + 5)); // duplicate contact reports
      CHECK(!taps.feed(0, press + 50));
    }
    CHECK(taps.count == TOUCH_WAKE_TAPS);
    taps.begin(began, 0);
    CHECK(!taps.feed(1, began + 20)); // release debounce
    CHECK(taps.feed(1, began + 200) == (TOUCH_WAKE_TAPS <= 2));
    CHECK(!taps.feed(0xFF, began + 250)); // I2C error cannot act as release
    CHECK(!taps.feed(1, began + 400));
    CHECK(taps.count == 2);
    CHECK(!taps.feed(0, began + 450));
    CHECK(taps.feed(1, began + 1000) == (TOUCH_WAKE_TAPS == 1)); // slow taps restart the run
    CHECK(taps.count == 1);
  }
  CHECK(tapMood(1) == Mood::BOOP && tapMood(2) == Mood::SURPRISED);
  CHECK(tapMood(3) == Mood::ANGRY && tapMood(4) == Mood::ANGRY);

  for (uint32_t began : {100u, UINT32_MAX - 1000u}) {
    ScreenTapRun run;
    for (unsigned i = 1; i <= 5; ++i) CHECK(run.add(began + i * 200) == i);
    CHECK(run.add(began + 3000) == 1); // a new run after a pause
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, began);
    creature.begin(&eyes, 1234, began);
    creature.startWake(began, false);
    creature.startTouchAngerPause(began + 1000);
    for (uint32_t elapsed = 16; elapsed < TOUCH_ANGER_PAUSE_MS; elapsed += 16) {
      uint32_t now = began + 1000 + elapsed;
      creature.setPointerHeld(true, now);
      creature.setPointer(1, 1, now);
      CHECK(!creature.react(Mood::FOLLOWING, now, 600));
      CHECK(!creature.react(Mood::DIZZY, now, DIZZY_ANIM_MS));
      creature.huff(.2f, now);
      creature.update(.016f, now);
      CHECK(creature.mood() == Mood::ANGRY);
    }
    uint32_t resumed = began + 1000 + TOUCH_ANGER_PAUSE_MS;
    CHECK(!creature.touchAngerPauseActive(resumed));
    CHECK(creature.react(Mood::FOLLOWING, resumed, 600));
    // Battery shutdown and inactivity sleep can interrupt the angry pause.
    creature.startTouchAngerPause(resumed + 1);
    creature.startSleep(resumed + 2, 0);
    CHECK(!creature.touchAngerPauseActive(resumed + 3));
    CHECK(creature.asleep());
  }

  { // Interaction after 30 seconds keeps the face awake; release allows sleep.
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, 100);
    creature.begin(&eyes, 1234, 100); creature.startWake(100, false);
    AwakeLimit limit; limit.start(100); bool closing = false;
    for (uint32_t t = 116; t <= 75108; t += 16) {
      if (t <= 60100) {
        limit.interaction(t);
        creature.setPointerHeld(true, t);
        creature.react(Mood::FOLLOWING, t, 600);
      }
      if (limit.closingDue(t)) {
        if (!closing) {
          creature.setPointerHeld(false, t);
          creature.startSleep(limit.closingAt(), 0);
          closing = true;
        }
      }
      creature.update(.016f, t);
      if (t <= 60100) CHECK(!closing && !creature.asleep());
    }
    CHECK(closing && creature.sleepFinished(75108));
    CHECK(creature.backlight() < .001f);
  }
  { // A tap late in closing restores the face and restarts the deadline.
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, 100);
    creature.begin(&eyes, 1234, 100); creature.startWake(100, false);
    AwakeLimit limit; limit.start(100);
    creature.startSleep(limit.closingAt(), 0);
    creature.update(.016f, 29000);
    limit.interaction(29000);
    CHECK(creature.react(Mood::SURPRISED, 29000, 900));
    creature.update(.016f, 30100);
    CHECK(!creature.asleep() && !limit.closingDue(30100));
    CHECK(limit.closingAt() + SLEEP_SEQUENCE_MS == 44000);
  }

  { // An upright screen rolled/pitched around a cone should trace all quadrants.
    WorldFollower world;
    float a[3] = {0,-9.81f,0}, g[3] = {};
    world.feed(a, g, 0, 1);
    float minX=1, maxX=-1, minY=1, maxY=-1;
    for (uint32_t t=26; t<8026; t+=25) {
      float phase=(t-26)*.001f*6.2831853f/4.0f;
      float s=sinf(.4363f), c=cosf(.4363f);
      a[0]=-9.81f*s*cosf(phase); a[1]=-9.81f*c; a[2]=9.81f*s*sinf(phase);
      g[0]=.4f; // an ongoing turn must not be learned as the new resting pose
      world.feed(a, g, 0, t);
      if (t>4026) {
        minX=fminf(minX,world.tiltX); maxX=fmaxf(maxX,world.tiltX);
        minY=fminf(minY,world.tiltY); maxY=fmaxf(maxY,world.tiltY);
        float radius=sqrtf(world.tiltX*world.tiltX+world.tiltY*world.tiltY);
        CHECK(radius>.25f && radius<1.15f); // vertical range now spans 50 degrees
      }
    }
    CHECK(minX<-.35f && maxX>.35f && minY<-.30f && maxY>.30f);
  }

  // A pan about vertical gravity is invisible to the accelerometer. The gyro
  // must carry this gaze, while sensor bias at rest must leave it centered.
  for (int sign : {-1, 1}) {
    WorldFollower world;
    float a[3] = {0, -9.81f, 0}, g[3] = {0, 0, 0};
    world.feed(a, g, 0, 1);
    g[1] = float(sign);
    for (uint32_t t = 26; t <= 526; t += 25) world.feed(a, g, 0, t);
    CHECK(world.tiltX * sign < -.65f); // heading gaze follows the inverted horizontal direction
  }
  {
    WorldFollower world;
    float a[3] = {0, -9.81f, 0}, g[3] = {.01f, .02f, -.01f};
    for (uint32_t t = 1; t < 60000; t += 25) world.feed(a, g, 0, t);
    CHECK(fabsf(world.tiltX) < .01f && fabsf(world.tiltY) < .01f);
  }

  // Exercise actual springs and rendering for all battery stages; optionally
  // write PPMs for visual inspection of the new battery layer.
  float previousTop = -1000;
  for (int level : {100,90,80,70,60,50,40,30,20,10,5,3}) {
    EyeRenderer renderer;
    Eyes eyes;
    CreatureAnimator creature;
    renderer.begin(fb, nullptr);
    eyes.begin(&renderer, 1234, 0);
    creature.begin(&eyes, 1234, 0);
    creature.setBatteryLevel(level);
    creature.setIdleActsAllowed(false);
    eyes.setAutoBlink(false);
    eyes.setIdle(false); // compare settled battery poses without incidental glances/blinks
    for (uint32_t t = 16; t <= 8000; t += 16) creature.update(.016f, t);
    CHECK(creature.mood() == Mood::IDLE);
    renderer.compose(eyes.frames());
    for (int i = 0; i < 2; ++i) CHECK(isfinite(eyes.frames()[i].topA));
    if (level >= 10) {
      CHECK(eyes.frames()[0].topA > previousTop + .2f);
      previousTop = eyes.frames()[0].topA; // visibly heavier lids each 10% drop
    }
    if (argc > 1) {
      char name[64]; snprintf(name, sizeof(name), "battery-%d", level);
      saveFrame(argv[1], name);
    }
    creature.react(tapMood(3), 8100, ANGRY_ANIM_MS);
    for (uint32_t t = 8116; t < 10300; t += 16) creature.update(.016f, t);
    CHECK(creature.mood() == Mood::IDLE); // anger ends, including at low charge
  }
  for (int level : {-1,0,5,10,25,50,73,85,100}) {
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, 0);
    creature.begin(&eyes, 1234, 0); creature.setBatteryLevel(level);
    creature.setIdleActsAllowed(false); eyes.setAutoBlink(false); eyes.setIdle(false);
    for (uint32_t now = 16; now <= 8000; now += 16) creature.update(.016f, now);
    creature.showBattery(level, 8000);
    for (uint32_t elapsed = 0; elapsed <= BATTERY_SHOW_MS + 300; elapsed += 16) {
      creature.update(.016f, 8000 + elapsed);
      eyes.draw();
      for (int p = 0; p < SCREEN_WIDTH * SCREEN_HEIGHT; ++p)
        if (!inPalette(fb[p], renderer.getEyeColor(), renderer.batteryDisplayActive())) {
          CHECK(false); break;
        }
      // The animation and bar stay within the round panel's safe circle.
      if (elapsed >= BATTERY_MORPH_MS && elapsed <= BATTERY_SHOW_MS - BATTERY_MORPH_MS)
      for (int y = 0; y < SCREEN_HEIGHT; ++y) for (int x = 0; x < SCREEN_WIDTH; ++x) {
        float dx = x - SCREEN_CX, dy = y - SCREEN_CY;
        if (dx * dx + dy * dy > SCREEN_RADIUS * SCREEN_RADIUS) CHECK(fb[y * SCREEN_WIDTH + x] == 0);
      }
      if (elapsed == 1600 && argc > 1) {
        char name[64]; snprintf(name, sizeof(name), "readout-%d", level); saveFrame(argv[1], name);
      }
      if (level == 73 && argc > 1 && elapsed % 48 == 0) {
        char name[64]; snprintf(name, sizeof(name), "swirl-%04u", elapsed); saveFrame(argv[1], name);
      }
    }
    CHECK(creature.mood() == Mood::IDLE && !renderer.batteryDisplayActive());
    // Normal drawing must clear the readout without a stale white digit.
    for (uint16_t pixel : fb) CHECK(inPalette(pixel, renderer.getEyeColor()));
    creature.showBattery(level, 14000);
    creature.update(.016f, 15000);
    CHECK(renderer.batteryDisplayActive());
    creature.startSleep(15000, 0); creature.update(.016f, 15016);
    CHECK(!renderer.batteryDisplayActive()); // critical shutdown can cancel the readout
  }
  for (int level : {21, 100, -1}) {
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, 0);
    creature.begin(&eyes, 1234, 0); creature.setBatteryLevel(level);
    creature.setIdleActsAllowed(false); eyes.setAutoBlink(false); eyes.setIdle(false);
    for (uint32_t now = 16; now <= 8000; now += 16) creature.update(.016f, now);
    creature.setBatteryLevel(level, true);
    for (uint32_t now = 8016; now <= 10000; now += 16) creature.update(.016f, now);
    CHECK(renderer.getEyeColor() == BATTERY_CHARGING_COLOR); // charging tints the eyes green
    creature.showBattery(level, 10000);
    uint16_t first[SCREEN_WIDTH * SCREEN_HEIGHT];
    for (uint32_t elapsed : {800u, 1200u}) {
      creature.update(.016f, 10000 + elapsed); eyes.draw();
      CHECK(renderer.batteryDisplayActive());
      for (int y = 0; y < SCREEN_HEIGHT; ++y) for (int x = 0; x < SCREEN_WIDTH; ++x) {
        CHECK(inPalette(fb[y * SCREEN_WIDTH + x], BATTERY_CHARGING_COLOR, true));
        float dx = x - SCREEN_CX, dy = y - SCREEN_CY;
        if (dx * dx + dy * dy > SCREEN_RADIUS * SCREEN_RADIUS) CHECK(fb[y * SCREEN_WIDTH + x] == 0);
      }
      if (elapsed == 800) for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) first[i] = fb[i];
      else {
        int changed = 0;
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) changed += fb[i] != first[i];
        CHECK(changed > 0); // the charging highlight sweeps across the bar
      }
      if (argc > 1) {
        char name[64]; snprintf(name, sizeof(name), "charging-%d-%u", level, elapsed);
        saveFrame(argv[1], name);
      }
    }
    creature.setBatteryLevel(level, false); // unplug while readout is open
    creature.update(.016f, 11300); eyes.draw();
    for(uint16_t pixel:fb) CHECK(inPalette(pixel,renderer.getBatteryColor(),true)); // no green liquid remains after unplug
    for (uint32_t now = 11316; now <= 14500; now += 16) creature.update(.016f, now);
    eyes.draw(); CHECK(!renderer.batteryDisplayActive());
    for (uint16_t pixel : fb) CHECK(inPalette(pixel, renderer.getEyeColor()));
  }
  for (uint32_t began : {0u, UINT32_MAX - 4500u}) {
    BatterySupply supply;
    supply.restore(4.06f, 88, true, 4.15f); supply.sample(4.14f, began);
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, began); creature.begin(&eyes, 1234, began);
    creature.setIdleActsAllowed(false); eyes.setAutoBlink(false); eyes.setIdle(false);
    creature.setBatteryLevel(88, true);
    for (uint32_t elapsed = 16; elapsed <= 2800; elapsed += 16) creature.update(.016f, began + elapsed);
    creature.showBattery(88, began + 3000);
    creature.update(.016f, began + 3800); eyes.draw();
    CHECK(renderer.getBatteryColor() == BATTERY_CHARGING_COLOR);
    supply.sample(4.06f, began + 4000, BATTERY_LIVE_POWER_CHECK_MS);
    CHECK(supply.charging);
    for (uint32_t extra = 1; extra < 100; extra += 10)
      supply.sample(4.06f, began + 4000 + extra, BATTERY_LIVE_POWER_CHECK_MS);
    CHECK(supply.charging); // render/tap calls within a poll cannot fake confirmation
    supply.sample(4.06f, began + 4100, BATTERY_LIVE_POWER_CHECK_MS);
    CHECK(!supply.charging && supply.percent == 88);
    creature.setBatteryLevel(supply.percent, supply.charging);
    creature.update(.016f, began + 4100); eyes.draw();
    BatteryState normal; normal.update(88);
    CHECK(creature.mood() == Mood::BATTERY && renderer.batteryDisplayActive());
    CHECK(renderer.getBatteryColor() == normal.color()); // green ends with the charge, not later
    CHECK(renderer.getEyeColor() != renderer.getBatteryColor()); // the bar bypasses the slow green eye fade
    for (uint16_t pixel : fb) CHECK(inPalette(pixel, normal.color(), true));
    CHECK(fb[130*SCREEN_WIDTH+120]!=0); // the measured liquid level remains visible after unplug
    supply.sample(4.15f, began + 4200, BATTERY_LIVE_POWER_CHECK_MS);
    supply.sample(4.15f, began + 4300, BATTERY_LIVE_POWER_CHECK_MS);
    CHECK(supply.charging && supply.percent == 88);
    creature.setBatteryLevel(supply.percent, supply.charging);
    creature.update(.016f, began + 4300); eyes.draw();
    CHECK(renderer.getBatteryColor() == BATTERY_CHARGING_COLOR);
    for (uint16_t pixel : fb) CHECK(inPalette(pixel, BATTERY_CHARGING_COLOR, true));
    CHECK(creature.batteryDisplayActive(began + 4300));
    creature.update(.016f, began + 3000 + BATTERY_SHOW_MS + 1);
    CHECK(creature.mood() == Mood::IDLE && !renderer.batteryDisplayActive()); // transitions never restart the readout
  }
  for (uint32_t began : {100u, UINT32_MAX-35000u}) {
    // Plugged in near full: held voltage, +-3 mV ADC noise (as measured on the
    // board) and a worst-case 1.5 mV/min drift down. Ten minutes stays charging.
    BatterySupply hold;
    hold.restore(4.07f,92,true,4.15f);
    uint32_t seed=1,ms=0;
    auto noise=[&seed]{ seed=seed*1103515245u+12345u; return int((seed>>16)%7)*.001f-.003f; };
    for (;ms<=600000;ms+=100) {
      hold.sample(4.15f-ms*.000000025f+noise(),began+ms,100);
      CHECK(hold.charging);
    }
    auto recent=hold.snapshot();
    CHECK(recent.peak<4.14f && recent.peak>4.13f);
    // Unplugged near full: only a 10 mV step, then the cell keeps relaxing.
    // The reference no longer follows it down, so the sag is caught.
    float base=4.135f;uint32_t unplugged=ms;int gauge=hold.percent;
    for (;ms<=unplugged+60000 && hold.charging;ms+=100) {
      float t=(ms-unplugged)*.001f;
      hold.sample(base-.010f-.025f*(1-expf(-t/20))+noise(),began+ms,100);
    }
    CHECK(!hold.charging && ms-unplugged<=15000 && hold.percent==gauge);
    // Charging in constant current: our own load steps the voltage down 12 mV,
    // then charging keeps raising it. That is not an unplug.
    BatterySupply cc;
    cc.restore(3.90f,60,true,3.90f);
    for (ms=0;ms<=300000;ms+=100) {
      float v=3.90f+ms*.00000005f-(ms>=60000?.012f:0)+noise();
      cc.sample(v,began+ms,100);
      CHECK(cc.charging);
    }
    BatterySupply wake;
    CHECK(wake.restore(recent));
    wake.sample(4.125f,began,100); wake.sample(4.125f,began+100,100);
    CHECK(wake.charging); // resume against the recent pre-sleep measurement
    wake.sample(4.04f,began+200,100); wake.sample(4.04f,began+300,100);
    CHECK(!wake.charging); // a real abrupt unplug still takes effect
    auto legacy=recent; legacy.version=0x42544733; legacy.peak=4.24f; legacy.filtered=4.15f;
    CHECK(wake.restore(legacy));
    // A fresh object represents a reset; accepted-sample timestamps are not retained.
    BatterySupply migrated; CHECK(migrated.restore(legacy));
    migrated.sample(4.14f,began,100); migrated.sample(4.14f,began+100,100);
    CHECK(migrated.charging && migrated.percent==recent.percent);
    BatterySupply smallStep;
    smallStep.restore(4.07f,88);
    for(uint32_t ms=0;ms<=60000;ms+=100) smallStep.sample(4.07f,began+ms,100);
    smallStep.sample(4.105f,began+60100,100);
    smallStep.sample(4.07f,began+60200,100);
    CHECK(!smallStep.charging && !smallStep.chargeSuspected());
    smallStep.sample(4.105f,began+60300,100);
    smallStep.sample(4.105f,began+60400,100);
    CHECK(!smallStep.charging);
    smallStep.sample(4.105f,began+60500,100);
    CHECK(smallStep.charging && smallStep.percent==88); // small first plug after a minute of settled battery use
    BatterySupply spike;
    spike.restore(4.06f,88,true,4.15f);
    spike.sample(4.15f,began,100);
    spike.sample(4.28f,began+100,100);
    for (uint32_t ms=200;ms<=2000;ms+=100) spike.sample(4.15f,began+ms,100);
    CHECK(spike.charging && spike.percent==88); // one high outlier cannot create a false unplug
    spike.sample(4.06f,began+2100,100); spike.sample(4.06f,began+2200,100);
    CHECK(!spike.charging);
    spike.sample(4.095f,began+2300,100); spike.sample(4.095f,began+2400,100);
    CHECK(spike.charging && spike.percent==88); // small near-full replug, without five seconds of waiting

    // Replug after relaxation: the last unplug observation and the SOC filter
    // still sit above the actual low plateau. An edge must use recent samples.
    BatterySupply relaxed;
    relaxed.restore(4.13f,91);
    relaxed.sample(4.10f,began,100); relaxed.sample(4.10f,began+100,100);
    relaxed.sample(4.135f,began+200,100);
    relaxed.sample(4.135f,began+300,100);
    CHECK(!relaxed.charging);
    relaxed.sample(4.135f,began+400,100);
    CHECK(relaxed.charging && relaxed.percent==91);
    for(int cycle=0;cycle<4;++cycle) {
      uint32_t t=1000+cycle*6000;
      float peak=4.135f-cycle*.020f;
      for(uint32_t age=0;age<3500;age+=100) relaxed.sample(peak-.030f,began+t+age,100);
      CHECK(!relaxed.charging);
      relaxed.sample(peak-.055f,began+t+3500,100); relaxed.sample(peak-.055f,began+t+3600,100);
      for(uint32_t age=3700;age<=3900;age+=100) relaxed.sample(peak-.020f,began+t+age,100);
      CHECK(relaxed.charging && relaxed.percent==91);
    }
    // A single low ADC glitch followed by a stable return is not a cable edge.
    BatterySupply lowSpike;lowSpike.restore(4.13f,91);
    for(uint32_t ms=0;ms<1000;ms+=100) lowSpike.sample(4.13f,began+ms,100);
    lowSpike.sample(4.08f,began+1000,100);
    for(uint32_t ms=1100;ms<3000;ms+=100) lowSpike.sample(4.13f,began+ms,100);
    CHECK(!lowSpike.charging);
    // The connected-board trace rose ~35 mV over 80 seconds. Resetting the
    // trend every ten seconds erased this evidence before it could qualify.
    BatterySupply gradual;gradual.restore(4.049f,91);
    for(uint32_t ms=0;ms<=100000 && !gradual.charging;ms+=100)
      gradual.sample(4.049f+ms*.00000045f+noise(),began+ms,100);
    CHECK(gradual.charging && gradual.percent<=91);

    BatterySupply top;
    top.restore(4.20f,98,true,4.20f);
    for (uint32_t ms=0;ms<=120000;ms+=100) top.sample(4.20f,began+ms,100);
    CHECK(top.percent==99); // deadband allows the last estimated charging point

    for (uint32_t age : {0u,2700u,SLEEP_SEQUENCE_MS-100u,SLEEP_SEQUENCE_MS}) {
      EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
      renderer.begin(fb,nullptr); eyes.begin(&renderer,1234,began); creature.begin(&eyes,1234,began);
      AwakeLimit limit; limit.start(began); bool closing=true,deep=true;
      uint32_t closingAt=limit.closingAt(),now=closingAt+age;
      creature.startSleep(closingAt,0); creature.setBatteryLevel(88,true);
      CHECK(!cancelClosingForCharge(limit,creature,closing,deep,false,now));
      CHECK(closing && deep && creature.asleep());
      CHECK(cancelClosingForCharge(limit,creature,closing,deep,true,now));
      CHECK(!closing && !deep && !creature.asleep() && !limit.closingDue(now));
      CHECK(!cancelClosingForCharge(limit,creature,closing,deep,true,now+100));
      for (uint32_t ms=16;ms<=2400;ms+=16) creature.update(.016f,now+ms);
      eyes.draw(); CHECK(renderer.getEyeColor()==BATTERY_CHARGING_COLOR);
      if (argc>1 && age==2700) saveFrame(argv[1],"charging-interrupted-close");
      limit.finish(); closing=deep=true;
      CHECK(!cancelClosingForCharge(limit,creature,closing,deep,true,now+2500));
    }
  }
  { // The charging halo stays on the actual eyes and clears after unplug.
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb,nullptr); eyes.begin(&renderer,1234,0); creature.begin(&eyes,1234,0);
    creature.setIdleActsAllowed(false); eyes.setAutoBlink(false); eyes.setIdle(false);
    creature.setBatteryLevel(88,true);
    for (uint32_t ms=16;ms<=4000;ms+=16) creature.update(.016f,ms);
    for (uint32_t ms=4016;ms<=6400;ms+=16) {
      creature.update(.016f,ms); eyes.draw();
      for (int y=195;y<SCREEN_HEIGHT;++y) for (int x=0;x<SCREEN_WIDTH;++x) {
        float dx=x-SCREEN_CX,dy=y-SCREEN_CY;
        if (dx*dx+dy*dy>SCREEN_RADIUS*SCREEN_RADIUS) CHECK(fb[y*SCREEN_WIDTH+x]==0);
      }
      for (uint16_t pixel:fb) CHECK(inPalette(pixel,renderer.getEyeColor()));
      if (argc>1 && (ms-4016)%400==0) {
        char name[64];snprintf(name,sizeof(name),"charging-face-%u",ms);
        saveFrame(argv[1],name);
      }
    }
    creature.setBatteryLevel(88,false);
    for (uint32_t ms=6416;ms<=14400;ms+=16) creature.update(.016f,ms);
    eyes.draw();
    for (int y=205;y<SCREEN_HEIGHT;++y) for (int x=0;x<SCREEN_WIDTH;++x) CHECK(fb[y*SCREEN_WIDTH+x]==0);
  }
  { // Charging shares the real eyelids; no background layer is exposed by blinking.
    EyeRenderer renderer; Eyes eyes;
    renderer.begin(fb,nullptr); renderer.setEyeColor(BATTERY_CHARGING_COLOR);
    eyes.begin(&renderer,1234,0); eyes.setIdle(false); eyes.setAutoBlink(false);
    for(uint32_t ms=16;ms<=2000;ms+=16) eyes.update(.016f,ms);
    EyeFrame open[2]={eyes.frames()[0],eyes.frames()[1]};
    EyeFrame closed[2]={open[0],open[1]};
    for(int i=0;i<2;++i) {
      closed[i].topA=closed[i].botA=0;
      closed[i].topB=closed[i].botB=closed[i].topC=closed[i].botC=0;
    }
    uint16_t baseline[SCREEN_WIDTH*SCREEN_HEIGHT];
    renderer.setFaceCharging(0,0); renderer.compose(closed);
    for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT;++i) baseline[i]=fb[i];
    for(int phase=0;phase<24;++phase) {
      renderer.setFaceCharging(1,phase/24.0f);renderer.compose(closed);
      for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT;++i) CHECK(fb[i]==baseline[i]);
    }
    // Near-closed lids must also be clean, before the final zero-aperture frame.
    EyeFrame narrow[2]={closed[0],closed[1]};
    for(auto &f:narrow) { f.topA=-f.ry*.08f; f.botA=f.ry*.08f; }
    renderer.setFaceCharging(0,0);renderer.compose(narrow);
    for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT;++i) baseline[i]=fb[i];
    for(int phase=0;phase<24;++phase) {
      renderer.setFaceCharging(1,phase/24.0f);renderer.compose(narrow);
      for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT;++i) CHECK(fb[i]==baseline[i]);
    }
    EyeFrame wink[2]={open[0],closed[1]};
    renderer.setFaceCharging(0,0);renderer.compose(wink);
    for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT;++i) baseline[i]=fb[i];
    for(int phase=0;phase<24;++phase) {
      renderer.setFaceCharging(1,phase/24.0f);renderer.compose(wink);
      for(int y=0;y<SCREEN_HEIGHT;++y) for(int x=145;x<SCREEN_WIDTH;++x)
        CHECK(fb[y*SCREEN_WIDTH+x]==baseline[y*SCREEN_WIDTH+x]);
    }
  }
  for(uint32_t began:{100u,UINT32_MAX-1000u}) {
    BatterySupply nearFull;nearFull.restore(4.07f,92,true,4.14f);
    nearFull.sample(4.14f,began,100);nearFull.sample(4.14f,began+100,100);
    for(uint32_t ms=200;ms<=1200;ms+=100) nearFull.sample(4.14f+(ms%200?.006f:-.006f),began+ms,100);
    CHECK(nearFull.charging);
    uint32_t ms=1300;
    for(;ms<=1500;ms+=100) nearFull.sample(4.112f,began+ms,100);
    CHECK(nearFull.charging); // a small fall is never confirmed by its first polls
    for(;ms<=4000 && nearFull.charging;ms+=100) nearFull.sample(4.112f,began+ms,100);
    CHECK(!nearFull.charging && ms<=3100 && nearFull.percent==92); // ...but is within about two seconds
    nearFull.sample(4.14f,began+ms,100);nearFull.sample(4.14f,began+ms+100,100);
    CHECK(nearFull.charging && nearFull.percent==92);
  }
  { // Static battery frames do not consume render/SPI time; live changes redraw.
    EyeRenderer r;Eyes eyes;r.begin(fb,countWindow);eyes.begin(&r,1234,0);
    pushedWindows=0;r.setBatteryDisplay(92,1,0,true);eyes.draw();CHECK(pushedWindows==1);
    for(int i=0;i<20;++i) {r.setBatteryDisplay(92,1,0,true,i/20.0f);eyes.draw();}
    CHECK(pushedWindows==16); // the charging sweep redraws only on its 16 steps
    r.setBatteryDisplay(91,1,0,true);eyes.draw();CHECK(pushedWindows==17);
    r.setBatteryDisplay(91,1,0,false);eyes.draw();CHECK(pushedWindows==18);
    for(int i=0;i<20;++i) {r.setBatteryDisplay(91,1,0,false,i/20.0f);eyes.draw();}
    CHECK(pushedWindows==18); // unplugged, the readout is static
    CHECK(r.getBatteryColor()==r.getEyeColor());
    r.setBatteryDisplay(91,0,0,false);eyes.draw();CHECK(pushedWindows==19);
    for(uint16_t pixel:fb) CHECK(inPalette(pixel,r.getEyeColor()));
  }
  printf("Battery, power timing, tapping and pan: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
