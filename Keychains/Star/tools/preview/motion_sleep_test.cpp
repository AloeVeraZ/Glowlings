#include <stdio.h>
#include <math.h>
#include "InteractionPolicy.h"
#include "MotionGestures.h"
#include "gaze_expect.h"
#include "TouchTracker.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); ++fails; } } while (0)
static uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];

int main() {
  int fails = 0;
  CHECK(confirmedTouchReport(1,0,0,false,true));
  CHECK(confirmedTouchReport(1,2,0,true,false)); // contact is real even if coordinates are unavailable
  CHECK(confirmedTouchReport(0,1,5,false,true)); // fast completed tap without a tracked press
  CHECK(confirmedTouchReport(0,1,0,true,true)); // confirmed lift of tracked contact
  CHECK(!confirmedTouchReport(0,0,0,false,true));
  CHECK(!confirmedTouchReport(0,1,0,false,true)); // unsolicited up/IRQ noise is not interaction
  CHECK(!confirmedTouchReport(0xFF,2,5,false,true));
  CHECK(!confirmedTouchReport(1,3,5,false,true));
  for (uint32_t began : {100u,UINT32_MAX-1000u}) {
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature; AwakeLimit limit;
    renderer.begin(fb,nullptr); eyes.begin(&renderer,1234,began); creature.begin(&eyes,1234,began);
    limit.start(began); bool closing = false, planned = false;
    for (uint32_t elapsed = 0; elapsed <= 60000; elapsed += 25) {
      uint32_t now = began+elapsed;
      // Continuous gyro noise, walking attention, tilt gaze and self-animation
      // must never move the initial 30-second deadline or cancel closing.
      limit.interaction(now,false);
      cancelClosingForActivity(limit,creature,closing,planned,false,now,false);
      creature.setMotionActivity(.8f,now); creature.setTilt(.4f,-.4f);
      CHECK(limit.closingAt()+SLEEP_SEQUENCE_MS == began+30000);
      if (!closing && limit.closingDue(now)) { closing=planned=true; creature.startSleep(limit.closingAt(),0); }
      creature.update(.025f,now);
      if (elapsed >= 30000) CHECK(closing && creature.sleepFinished(now) && creature.backlight() < .001f);
    }
    limit.start(began);
    limit.interaction(began+2000);
    CHECK(limit.closingAt()+SLEEP_SEQUENCE_MS == began+17000);
    limit.interaction(began+5000);
    CHECK(limit.closingAt()+SLEEP_SEQUENCE_MS == began+20000);
    for (uint32_t elapsed=6000; elapsed<20000; elapsed+=25) limit.interaction(began+elapsed,false);
    CHECK(limit.closingAt()+SLEEP_SEQUENCE_MS == began+20000);
  }
  // Input at every closing stage, including the exact off deadline, must revive
  // the real animator and renew the same session rather than committing sleep.
  for (uint32_t began : {100u,UINT32_MAX-1000u}) for (uint32_t elapsed : {24600u,29000u,30000u,30100u}) {
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature; AwakeLimit limit;
    renderer.begin(fb,nullptr); eyes.begin(&renderer,1234,began); creature.begin(&eyes,1234,began);
    limit.start(began); creature.startSleep(limit.closingAt(),0);
    creature.update(.016f,began+elapsed);
    bool preparing = true, deepPlanned = true;
    CHECK(cancelClosingForActivity(limit,creature,preparing,deepPlanned,false,began+elapsed));
    CHECK(!preparing && !deepPlanned && !creature.asleep());
    CHECK(limit.closingAt()+SLEEP_SEQUENCE_MS == began+elapsed+15000);
    for (uint32_t t = 16; t <= 320; t += 16) creature.update(.016f,began+elapsed+t);
    CHECK(creature.backlight() > .9f);
    preparing = deepPlanned = true; creature.startSleep(began+elapsed+400,0);
    CHECK(!cancelClosingForActivity(limit,creature,preparing,deepPlanned,true,began+elapsed+500));
    CHECK(preparing && deepPlanned && creature.asleep()); // critical shutdown still wins
  }
  {
    // Two minutes with a stationary finger and no further coordinate IRQs.
    TouchTracker finger; AwakeLimit limit; EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb,nullptr); eyes.begin(&renderer,1234,100); creature.begin(&eyes,1234,100);
    limit.start(100); finger.feed(120,120,0,0,true,100);
    bool preparing = false, deepPlanned = false;
    for (uint32_t t = 140; t <= 120100; t += 40) {
      finger.confirmContact(t); CHECK(finger.poll(t).kind == TouchTracker::NONE);
      cancelClosingForActivity(limit,creature,preparing,deepPlanned,false,t);
      CHECK(finger.tracking() && !limit.closingDue(t));
    }
    CHECK(finger.confirmLift(120140).kind == TouchTracker::RELEASE);
    CHECK(!finger.down);
    CHECK(limit.closingDue(130100)); // silence after release allows normal sleep
  }
  {
    DeliberateTurnActivity turns; AwakeLimit limit; limit.start(100);
    const float quiet[3] = {.01f,-.02f,.01f}, turn[3] = {0,0,.20f};
    for (uint32_t t = 100; t < 20000; t += 40) CHECK(!turns.feed(quiet,t));
    int recognizedTurns = 0;
    for (uint32_t t = 20000; t < 90000; t += 40) {
      if (turns.feed(turn,t)) { limit.interaction(t,false); ++recognizedTurns; }
      CHECK(limit.closingDue(t) == (t >= 24700)); // turning does not extend the no-touch window
    }
    CHECK(recognizedTurns > 50);
    CHECK(limit.closingAt()+SLEEP_SEQUENCE_MS == 30100);
    for (uint32_t t = 90000; t < 95000; t += 40) CHECK(!turns.feed(turn,t,false)); // carrying rejected
    const float flat[3] = {0,0,-9.81f}, leaned[3] = {-1.0f,0,-9.76f};
    CHECK(!turns.feedTilt(flat)); CHECK(turns.feedTilt(leaned)); CHECK(!turns.feedTilt(leaned));
  }
  // Fixed native sensor fixtures inferred from the user's physical report:
  // left tilt previously looked down, right looked up; front/back drove X.
  // These expectations do not rotate with the configurable implementation.
  // Native Z is negative when this rear-mounted sensor is screen-up.
  const int rawTilt[4][3] = {{0,-1731,-3712},{0,1731,-3712},
                            {1731,0,-3712},{-1731,0,-3712}};
  for (int direction = 0; direction < 4; ++direction) {
    WorldFollower world; world.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    float flat[3] = {0,0,9.80665f}, zero[3] = {};
    for (uint32_t t = 1; t < 800; t += 25) world.feed(flat,zero,0,t);
    const int *r = rawTilt[direction];
    float a[3] = {r[0]*(9.80665f/4096),-r[1]*(9.80665f/4096),-r[2]*(9.80665f/4096)};
    float flick[3] = {-a[0],-a[1],0}; // detector reports the side tipped low
    gestureMotionToScreen(flick);
    CHECK(direction == 0 ? flick[0] < -3 : direction == 1 ? flick[0] > 3 :
          direction == 2 ? flick[1] < -3 : flick[1] > 3);
    CHECK(direction < 2 ? fabsf(flick[1]) < .02f : fabsf(flick[0]) < .02f);
    rotateMotionXY(a,GAZE_IMU_ROTATION);
    for (uint32_t t = 801; t < 2400; t += 25) world.feed(a,zero,0,t);
    CHECK(direction == 0 ? fabsf(world.tiltX-expectedHorizontalGaze(25))<.03f : direction == 1 ? fabsf(world.tiltX+expectedHorizontalGaze(25))<.03f :
          direction == 2 ? world.tiltY < -.35f : world.tiltY > .35f);
    CHECK(direction < 2 ? fabsf(world.tiltY) < .02f : fabsf(world.tiltX) < .02f);
    // Physical directions: a left tilt and a right tilt look opposite ways,
    // the way the board-confirmed HORIZONTAL_GAZE_SIGN sets.
    if(direction < 2) CHECK((direction == 0 ? -1 : 1)*HORIZONTAL_GAZE_SIGN*world.tiltX > .5f);
    // Predict a real continuous tilt with both sensors, through the same map.
    WorldFollower moving; moving.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    moving.feed(flat,zero,0,1);
    for (uint32_t t = 26; t <= 1026; t += 25) {
      float theta = .4f*(t-1)*.001f, s = sinf(theta)*9.80665f, c = cosf(theta)*9.80665f;
      float nativeA[3] = {direction == 2 ? s : direction == 3 ? -s : 0,
                          direction == 0 ? -s : direction == 1 ? s : 0,-c};
      float nativeG[3] = {direction == 0 ? .4f : direction == 1 ? -.4f : 0,
                          direction == 2 ? .4f : direction == 3 ? -.4f : 0,0};
      float visualA[3] = {nativeA[0],-nativeA[1],-nativeA[2]};
      float visualG[3] = {nativeG[0],-nativeG[1],-nativeG[2]};
      rotateMotionXY(visualA,GAZE_IMU_ROTATION); rotateMotionXY(visualG,GAZE_IMU_ROTATION);
      moving.feed(visualA,visualG,0,t);
    }
    float along = direction < 2 ? moving.tiltX : moving.tiltY;
    float cross = direction < 2 ? moving.tiltY : moving.tiltX;
    float cue = direction < 2 ? moving.swingX : moving.swingY;
    CHECK(along*(direction < 2 ? (direction == 0 ? -1 : 1)*HORIZONTAL_GAZE_SIGN : (direction == 2 ? -1 : 1)) > (direction < 2 ? .35f : .30f));
    CHECK(cue*along > 0 && fabsf(cross) < .02f);
  }
  // Generic fusion supports either Z mounting sign after screen conversion.
  for (float flatSign : {-1.0f,1.0f}) {
    WorldFollower diagonal; diagonal.setFlatUpSign(flatSign);
    const float z = flatSign*sqrtf(.28f);
    float initial[3] = {.6f*9.81f,-.6f*9.81f,z*9.81f}, zero[3] = {};
    for (uint32_t t=1;t<801;t+=25) diagonal.feed(initial,zero,0,t);
    float initialY=diagonal.tiltY;
    for (uint32_t t = 826; t <= 1026; t += 25) {
      float angle = (t-801)*.001f;
      float a[3] = {(.6f*cosf(angle)-.6f*sinf(angle))*9.81f,
                    (-.6f*sinf(angle)-.6f*cosf(angle))*9.81f,z*9.81f};
      float g[3] = {0,0,1};
      diagonal.feed(a,g,0,t);
    }
    CHECK(diagonal.swingY*(diagonal.tiltY-initialY)>.0005f); // immediate cue agrees with the actual pitch change
  }
  const float sd = sinf(.35f), cd = cosf(.35f);
  for (float flatSign : {-1.0f,1.0f}) {
    for (int direction = 0; direction < 4; ++direction) {
      WorldFollower world; world.setFlatUpSign(flatSign);
      float a[3] = {0,0,flatSign*9.81f}, g[3] = {};
      for (uint32_t t = 1; t < 800; t += 25) world.feed(a,g,0,t);
      if (direction < 2) a[0] = (direction == 0 ? 1 : -1)*sd*9.81f;
      else a[1] = (direction == 2 ? 1 : -1)*sd*9.81f;
      a[2] = flatSign*cd*9.81f;
      a[0] = -a[0]; a[1] *= flatSign;
      for (uint32_t t = 801; t < 1400; t += 25) world.feed(a,g,0,t);
      CHECK(direction == 0 ? fabsf(world.tiltX+expectedHorizontalGaze(.35f/.017453293f))<.025f : direction == 1 ? fabsf(world.tiltX-expectedHorizontalGaze(.35f/.017453293f))<.025f :
            direction == 2 ? world.tiltY > .25f : world.tiltY < -.25f);
    }
    for (int direction : {-1,1}) {
      WorldFollower world; world.setFlatUpSign(flatSign);
      float a[3] = {0,0,flatSign*9.81f}, g[3] = {};
      world.feed(a,g,0,1);
      g[2] = -flatSign*direction;
      for (uint32_t t = 26; t <= 526; t += 25) world.feed(a,g,0,t);
      CHECK(fabsf(world.tiltX*direction+expectedHorizontalGaze(.525f/.017453293f))<.08f); // heading direction, allowing smoothing lag
    }
  }
  {
    WorldFollower world; world.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    float flat[3] = {0,0,9.81f}, upright[3] = {0,9.81f,0}, zero[3] = {};
    for (uint32_t t = 1; t < 800; t += 25) world.feed(flat,zero,0,t);
    for (uint32_t t = 801; t < 5800; t += 25) world.feed(upright,zero,0,t);
    CHECK(fabsf(world.tiltY) < .02f); // 90-degree hanging posture now rests at center
    for (uint32_t t = 5801; t < 7800; t += 25) world.feed(flat,zero,0,t);
    CHECK(fabsf(world.tiltX) < .02f && fabsf(world.tiltY) < .02f);
  }
  // Actual USB-bottom native-axis poses: hanging at 90 degrees is neutral;
  {
    // User's real handheld hanging capture, in the canonical gesture frame:
    // a=(-9.42,-1.24,.30) m/s2, g=(.106,.007,-.005) rad/s. About 88 degrees.
    // Start flat, then keep turning about gravity: there is NO quiet gyro
    // interval. The previous reference selection stayed flat and looked down.
    WorldFollower world; world.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    float flat[3]={0,0,9.81f},quiet[3]={};
    for (uint32_t t=1;t<1000;t+=25) world.feed(flat,quiet,0,t);
    float hanging[3]={-9.42f,-1.24f,.30f}; rotateMotionXY(hanging,GAZE_IMU_ROTATION);
    float length=motion::len3(hanging),turn[3]={};
    for (int i=0;i<3;++i) turn[i]=hanging[i]/length*.30f;
    for (uint32_t t=1001;t<5000;t+=25) world.feed(hanging,turn,2.5f,t);
    CHECK(world.uprightReferenceActive() && fabsf(world.tiltY)<.02f);
    float handGyro[3]={.106f,.007f,-.005f}; rotateMotionXY(handGyro,GAZE_IMU_ROTATION);
    for (uint32_t t=5001;t<13000;t+=25) world.feed(hanging,handGyro,0,t);
    // This was a moving-hand reading, not a stationary calibration. Repeating
    // its ~0.1 rad/s heading rotation for eight seconds turns about 48 degrees.
    // Count that real slow turn rather than discard it with the old cutoff.
    CHECK(world.tiltX*HORIZONTAL_GAZE_SIGN>.98f && fabsf(world.tiltY)<.02f);
    for (uint32_t t=13001;t<16000;t+=25) world.feed(flat,quiet,0,t);
    CHECK(!world.uprightReferenceActive() && fabsf(world.tiltY)<.02f);
    // Rolling side-on should look sideways, never fabricate a vertical turn.
    float sideways[3]={-9.81f,0,0};
    for (uint32_t t=16001;t<19000;t+=25) world.feed(hanging,quiet,0,t);
    for (uint32_t t=19001;t<22000;t+=25) world.feed(sideways,quiet,0,t);
    CHECK(world.tiltX*HORIZONTAL_GAZE_SIGN>.98f && fabsf(world.tiltY)<.03f);
    // A shake-biased first sample cannot reverse later up/down tracking.
    WorldFollower transient; transient.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    float reversedFirst[3]={hanging[0],-hanging[1],hanging[2]};
    transient.feed(reversedFirst,quiet,0,1);
    for (uint32_t t=26;t<2000;t+=25) transient.feed(hanging,quiet,0,t);
    float up50[3]={0,9.81f*sinf(.6981317f),9.81f*cosf(.6981317f)};
    for (uint32_t t=2001;t<4000;t+=25) transient.feed(up50,quiet,0,t);
    CHECK(transient.tiltY<-.98f);
    WorldFollower flatRoll; flatRoll.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    for (uint32_t t=1;t<1000;t+=25) flatRoll.feed(flat,quiet,0,t);
    for (uint32_t elapsed=25;elapsed<=3000;elapsed+=25) {
      float angle=elapsed*.001f*.6981317f; // roll from table to 120 degrees side-on
      float a[3]={-9.81f*sinf(angle),0,9.81f*cosf(angle)},g[3]={0,.6981317f,0};
      flatRoll.feed(a,g,0,1001+elapsed);
      CHECK(fabsf(flatRoll.tiltY)<.02f);
    }
  }
  // Horizontal response: small tilts are visible within 150 ms,
  // at different sampling rates, without creating vertical movement.
  for (bool hanging : {false,true}) for (int sign : {-1,1}) for (uint32_t period : {10u,25u,50u}) {
    WorldFollower world; world.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    float neutral[3]={0,hanging ? 9.81f : 0,hanging ? 0 : 9.81f}, quiet[3]={};
    for (uint32_t t=1;t<=1001;t+=period) world.feed(neutral,quiet,0,t);
    const float angle=.17453293f; // 10 degrees
    float tilted[3]={sign*9.81f*sinf(angle),hanging ? 9.81f*cosf(angle) : 0,hanging ? 0 : 9.81f*cosf(angle)};
    for (uint32_t elapsed=period;elapsed<=150;elapsed+=period) world.feed(tilted,quiet,0,1001+elapsed);
    CHECK(fabsf(world.tiltX*sign-expectedHorizontalGaze(10))<.025f);
    CHECK(fabsf(world.tiltY)<.02f);
    for (uint32_t elapsed=period;elapsed<=300;elapsed+=period) world.feed(neutral,quiet,0,1151+elapsed);
    CHECK(fabsf(world.tiltX)<.02f && fabsf(world.tiltY)<.02f);
  }
  // The old halfway-down pose now gives a full downward gaze. An equal
  // upward lean retains its prior response in either neutral posture/mount.
  for (float flatSign : {-1.0f,1.0f}) for (bool hanging : {false,true}) for (uint32_t period : {10u,25u,50u}) {
    WorldFollower world; world.setFlatUpSign(flatSign);
    uint32_t t=1;
    float neutral[3]={0,hanging ? 9.81f : 0,hanging ? 0 : flatSign*9.81f},quiet[3]={};
    auto hold = [&](const float a[3],uint32_t duration) {
      for (uint32_t elapsed=0;elapsed<duration;elapsed+=period) { world.feed(a,quiet,0,t); t+=period; }
    };
    const float s=sinf(.43633231f)*9.81f,c=cosf(.43633231f)*9.81f; // 25 degrees
    float down[3]={0,hanging ? c : flatSign*s,hanging ? -flatSign*s : flatSign*c};
    float up[3]={0,hanging ? c : -flatSign*s,hanging ? flatSign*s : flatSign*c};
    hold(neutral,1000); hold(down,1000);
    CHECK(world.tiltY>.98f && fabsf(world.tiltX)<.02f);
    hold(neutral,1000); CHECK(fabsf(world.tiltY)<.02f);
    hold(up,1000); CHECK(world.tiltY<-.39f && world.tiltY>-.42f);
    hold(neutral,1000); CHECK(fabsf(world.tiltY)<.02f);
  }
  // Full upward gaze stays at 50 degrees; downward gaze now fills at 25.
  const int tablePose[3] = {0,0,-4096}, hangingPose[3] = {-4096,0,0};
  const int up50Pose[3] = {-2633,0,-3138}, down50Pose[3] = {-2633,0,3138};
  for (uint32_t period : {10u,25u,50u}) {
    WorldFollower world; world.setFlatUpSign(GAZE_FLAT_UP_SIGN);
    uint32_t t = 1;
    auto hold = [&](const int raw[3], uint32_t duration) {
      float a[3] = {raw[0]*(9.80665f/4096),-raw[1]*(9.80665f/4096),-raw[2]*(9.80665f/4096)}, zero[3] = {};
      rotateMotionXY(a,GAZE_IMU_ROTATION);
      for (uint32_t elapsed = 0; elapsed < duration; elapsed += period) {
        world.feed(a,zero,0,t); t += period;
      }
    };
    hold(tablePose,500); CHECK(fabsf(world.tiltY) < .02f);
    hold(hangingPose,1000); CHECK(fabsf(world.tiltY) < .02f);
    hold(up50Pose,60000); CHECK(world.tiltY < -.98f && fabsf(world.tiltX) < .02f);
    hold(hangingPose,1000); CHECK(fabsf(world.tiltY) < .02f);
    hold(down50Pose,60000); CHECK(world.tiltY > .98f && fabsf(world.tiltX) < .02f);
    hold(hangingPose,1000); CHECK(fabsf(world.tiltY) < .02f);
    const int left[3] = {-3712,-1731,0}, right[3] = {-3712,1731,0};
    hold(left,1000); CHECK(fabsf(world.tiltX-expectedHorizontalGaze(25))<.03f && fabsf(world.tiltY) < .02f);
    hold(right,1000); CHECK(fabsf(world.tiltX+expectedHorizontalGaze(25))<.03f && fabsf(world.tiltY) < .02f);
    hold(tablePose,1000); CHECK(fabsf(world.tiltX) < .02f && fabsf(world.tiltY) < .02f);
    hold(hangingPose,1000); CHECK(fabsf(world.tiltY) < .02f);
    // A sensor sampling gap (or waking while tilted) must not center a 50-degree lean.
    t += 1000; hold(up50Pose,1000); CHECK(world.tiltY < -.98f);
    t += 1000; hold(down50Pose,1000); CHECK(world.tiltY > .98f);
  }
  {
    WorldFollower world;
    const float upright[3] = {0,-9.81f,0}, flat[3] = {0,0,-9.81f}, right[3] = {-4.146f,0,-8.891f}, g[3] = {};
    for (uint32_t t = 1; t < 1000; t += 25) world.feed(upright,g,0,t);
    for (uint32_t t = 1001; t < 2500; t += 25) world.feed(flat,g,0,t);
    CHECK(fabsf(world.tiltX) < .05f && fabsf(world.tiltY) < .05f);
    for (uint32_t t = 2501; t < 3000; t += 25) world.feed(right,g,0,t);
    CHECK(fabsf(world.tiltX+expectedHorizontalGaze(25))<.03f); // upright wake does not break subsequent flat tilts
  }
  printf("Sleep cancellation, held touch, deliberate motion and USB-bottom gaze: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
