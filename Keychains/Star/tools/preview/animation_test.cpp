#include <stdio.h>
#include <math.h>
#include "CreatureAnimator.h"
#include "MotionGestures.h"
#include "palette.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); ++fails; } } while (0)
static uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];

int main() {
  int fails = 0;
  // Look changes land quickly and settle, with the head following the pupils.
  GazeController gaze;
  gaze.setSaccades(false); gaze.lookAt(.8f, -.5f);
  for (uint32_t now = 16; now <= 160; now += 16) gaze.update(.016f, now, 0);
  CHECK(fabsf(gaze.pupilX(0) - .8f) < .08f);
  CHECK(fabsf(gaze.pupilY(0) + .5f) < .08f);
  CHECK(gaze.travelX(0) > .45f);

  { // Environmental context emits edges, never a looping show from a held state.
    EnvironmentObserver observer;
    int rest=0,other=0;
    for (uint32_t now=1;now<120001;now+=25) {
      auto event=observer.feed(0,now-1,false,false,0,now);
      if (event==EnvironmentObserver::LONG_REST) ++rest;
      else if (event!=EnvironmentObserver::NONE) ++other;
    }
    CHECK(rest==1 && other==0);
    observer=EnvironmentObserver();
    int moved=0,settled=0;
    for (uint32_t now=1;now<14001;now+=25) {
      float motion=now>=1001 && now<7001 ? .7f : 0;
      auto event=observer.feed(motion,0,false,false,0,now);
      moved+=event==EnvironmentObserver::MOVED; settled+=event==EnvironmentObserver::SETTLED;
    }
    CHECK(moved==1 && settled==1);
    int walk=0,stop=0,held=0,charge=0;
    for (uint32_t now=14001;now<30001;now+=25) {
      bool walking=now<20001,attending=now>=24001;
      auto event=observer.feed(.1f,0,walking,attending,now>=26001?1:0,now);
      walk+=event==EnvironmentObserver::WALK_STARTED; stop+=event==EnvironmentObserver::WALK_STOPPED;
      held+=event==EnvironmentObserver::HELD_UP; charge+=event==EnvironmentObserver::CHARGE_DROPPED;
    }
    CHECK(walk==1 && stop==1 && held==1 && charge==1);
    observer=EnvironmentObserver();
    for (uint32_t now=1;now<10001;now+=25)
      CHECK(observer.feed((now/25)%2?.3f:.1f,0,false,false,0,now)==EnvironmentObserver::NONE);
  }
  { // A quiet room has glances and blinks, not repeated full mood performances.
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb,nullptr); eyes.begin(&renderer,1234,0); creature.begin(&eyes,1234,0);
    int scenes=0; Mood previous=Mood::IDLE;
    for (uint32_t now=25;now<=120000;now+=25) {
      creature.setStillFor(now); creature.update(.025f,now);
      Mood mood=creature.mood();
      if (mood!=previous && mood!=Mood::IDLE) {++scenes; CHECK(mood==Mood::LOOK_AROUND);}
      previous=mood;
    }
    CHECK(scenes==1 && creature.mood()==Mood::IDLE);
    creature.setStillFor(0); creature.setCarried(true,false,false,3); creature.update(.025f,120025);
    CHECK(creature.mood()==Mood::LOOK_AROUND);
    for (uint32_t now=120050;now<=126000;now+=25) {creature.setCarried(true,false,false,3);creature.update(.025f,now);}
    CHECK(creature.mood()==Mood::IDLE); // sustained carrying does not replay the scene
    creature.setCarried(false,false,false,0); creature.update(.025f,126025);
    CHECK(creature.mood()==Mood::CURIOUS);
  }

  { // Looking grows taller while respecting the pair's shared screen space.
    EyeRenderer renderer; Eyes eyes; renderer.begin(fb,nullptr); eyes.begin(&renderer,1234,0);
    eyes.setIdle(false); eyes.setAutoBlink(false); eyes.setSaccades(false); eyes.lookCenter();
    uint32_t now=0;
    auto step = [&](uint32_t ms) {
      for (uint32_t t=0;t<ms;t+=16) {now+=16;eyes.update(.016f,now);}
    };
    auto area = [&]() {return eyes.frames()[0].rx*eyes.frames()[0].ry + eyes.frames()[1].rx*eyes.frames()[1].ry;};
    step(1600); float restingArea=area();
    float restingHeight=eyes.frames()[0].ry+eyes.frames()[1].ry;
    eyes.lookAt(.85f,-.3f); step(700);
    CHECK(eyes.frames()[0].ry+eyes.frames()[1].ry>restingHeight*1.08f);
    CHECK(area()>restingArea*.95f); // the side boundary and required eye gap limit lateral growth
    for (int direction=0;direction<8;++direction) {
      float angle=direction*anim::TAU_F/8;
      eyes.lookAt(cosf(angle),sinf(angle)); step(400);
      for (int i=0;i<2;++i) {
        const auto &f=eyes.frames()[i];
        for (int sample=0;sample<64;++sample) {
          float a=sample*anim::TAU_F/64, x=f.rx*cosf(a), y=f.ry*sinf(a)+f.bend*cosf(a)*cosf(a);
          float dx=(x*cosf(f.tilt)-y*sinf(f.tilt))*(f.rightEye?-1:1), dy=x*sinf(f.tilt)+y*cosf(f.tilt);
          float sx=f.cx+dx-SCREEN_CX, sy=f.cy+dy-SCREEN_CY;
          CHECK(sx*sx+sy*sy < SCREEN_RADIUS*SCREEN_RADIUS); // eye shapes remain inside the real round display
        }
      }
    }
    eyes.lookCenter(); eyes.setPresence(0); step(1600);
    CHECK(fabsf(area()-restingArea)<restingArea*.02f);
  }

  float firstTilt = 0;
  for (int period : {10,20,50}) {
    WorldFollower world;
    const float flat[3] = {0,0,-9.81f}, tilted[3] = {-4.146f,0,-8.891f}, quiet[3] = {};
    for (int t = 1; t <= 201; t += period) world.feed(flat, quiet, 0, t);
    for (int elapsed = period; elapsed <= 150; elapsed += period) world.feed(tilted, quiet, 0, 201 + elapsed);
    CHECK(world.tiltX < -.68f); // obvious direction change visible within 150 ms
    if (period == 10) firstTilt = world.tiltX;
    else CHECK(fabsf(world.tiltX - firstTilt) < .05f); // independent of display frame rate
    CHECK(world.motionLevel == 0); // a quiet orientation does not imply continuing movement
  }

  for (Mood mood : {Mood::WINK, Mood::PLAYFUL, Mood::STRETCH,Mood::PEEK,Mood::GIGGLE,Mood::SNEEZE,Mood::SASSY}) {
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, 0);
    creature.begin(&eyes, 1234, 0); creature.setIdleActsAllowed(false);
    for (uint32_t t = 16; t <= 1000; t += 16) creature.update(.016f, t);
    CHECK(creature.react(mood, 1000, 1700));
    for (uint32_t t = 1016; t <= 2800; t += 16) {
      creature.update(.016f, t); eyes.draw();
      for (int i = 0; i < 2; ++i) {
        CHECK(isfinite(eyes.frames()[i].cx) && isfinite(eyes.frames()[i].topA));
        CHECK(eyes.frames()[i].rx > 5 && eyes.frames()[i].ry > 5);
      }
      for (uint16_t pixel : fb) if (!inPalette(pixel, renderer.getEyeColor())) { CHECK(false); break; }
    }
    CHECK(creature.mood() == Mood::IDLE); // a spontaneous act always gives control back
  }

  { // A level resting screen leaves room for spontaneous glances.
    EyeRenderer renderer; Eyes eyes; CreatureAnimator creature;
    renderer.begin(fb, nullptr); eyes.begin(&renderer, 1234, 0);
    creature.begin(&eyes, 1234, 0); creature.setIdleActsAllowed(false); eyes.setAutoBlink(false);
    creature.setTilt(0, 0);
    float lo = 1000, hi = -1000;
    for (uint32_t t = 16; t <= 6000; t += 16) {
      creature.update(.016f, t);
      if (t > 1000) { lo = fminf(lo, eyes.frames()[0].pupilX); hi = fmaxf(hi, eyes.frames()[0].pupilX); }
    }
    CHECK(hi - lo > 3);
    // A deliberate turn immediately takes visual priority over idle wandering.
    for (uint32_t t = 6016; t <= 6208; t += 16) {
      creature.setTilt(-.7f, -.1f); creature.setMotionActivity(.8f, t); creature.update(.016f, t);
    }
    CHECK(eyes.frames()[0].pupilX < -2);
    creature.setMotionActivity(0, 6224);
    CHECK(creature.react(Mood::CURIOUS, 6224, 1700));
    for (uint32_t t = 6240; t <= 6448; t += 16) {
      creature.setTilt(.8f, 0); creature.setMotionActivity(1, t); creature.update(.016f, t);
    }
    CHECK(eyes.frames()[0].pupilX > 2); // a turn also redirects a spontaneous curious look
  }
  { // A joyful charging pose preserves open irises, while only its outer surface flows.
    EyeRenderer renderer; Eyes eyes;
    renderer.begin(fb,nullptr);renderer.setEyeColor(BATTERY_CHARGING_COLOR);
    eyes.begin(&renderer,1234,0);eyes.setIdle(false);eyes.setAutoBlink(false);eyes.setSaccades(false);
    for(uint32_t t=16;t<=2400;t+=16) { eyes.lookCenter();eyes.update(.016f,t); }
    EyeFrame neutral[2]={eyes.frames()[0],eyes.frames()[1]};
    eyes.setChargingJoy(1);
    for(uint32_t t=2416;t<=6400;t+=16) { eyes.lookCenter();eyes.update(.016f,t); }
    EyeFrame joyful[2]={eyes.frames()[0],eyes.frames()[1]};
    CHECK(joyful[0].botA<neutral[0].botA-10); // lifted smiling lower lids
    CHECK(joyful[0].botC>neutral[0].botC);
    CHECK(joyful[0].pupilR>neutral[0].pupilR*.94f); // no disappearing happy-preset iris
    uint16_t baseline[SCREEN_WIDTH*SCREEN_HEIGHT];
    renderer.setFaceCharging(0,0);renderer.compose(joyful);
    for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT;++i) baseline[i]=fb[i];
    int changed=0;
    for(int phase=0;phase<24;++phase) {
      renderer.setFaceCharging(1,phase/24.0f);renderer.compose(joyful);
      for(int y=0;y<SCREEN_HEIGHT;++y) for(int x=0;x<SCREEN_WIDTH;++x) {
        int i=y*SCREEN_WIDTH+x;changed+=fb[i]!=baseline[i];
        CHECK(inPalette(fb[i],BATTERY_CHARGING_COLOR));
        float dx=x-SCREEN_CX,dy=y-SCREEN_CY;
        if(dx*dx+dy*dy>SCREEN_RADIUS*SCREEN_RADIUS) CHECK(fb[i]==0);
        for(const EyeFrame &e:joyful) {
          float px=(x-e.cx-e.pupilX)/e.pupilSX,py=(y-e.cy-e.pupilY)/e.pupilSY;
          if(px*px+py*py<e.pupilR*e.pupilR*.60f) CHECK(fb[i]==baseline[i]);
        }
      }
    }
    CHECK(changed>0); // a gentle halo breath, without moving either contour or iris
    for(EyeFrame &e:joyful) { e.topA=e.botA=0;e.topB=e.botB=e.topC=e.botC=0; }
    renderer.setFaceCharging(0,0);renderer.compose(joyful);
    for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT;++i) baseline[i]=fb[i];
    for(int phase=0;phase<24;++phase) {
      renderer.setFaceCharging(1,phase/24.0f);renderer.setFaceCharging(1,phase/24.0f);renderer.compose(joyful);
      for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT;++i) CHECK(fb[i]==baseline[i]);
    }
  }
  { // A slower display frame must not slow the gaze spring's physical time.
    float reference=0;
    for(uint32_t interval:{16u,40u,80u,120u}) {
      EyeRenderer r;Eyes eyes;r.begin(fb,nullptr);eyes.begin(&r,1234,0);
      eyes.setIdle(false);eyes.setAutoBlink(false);eyes.setSaccades(false);
      for(uint32_t ms=16;ms<=2400;ms+=16) {eyes.lookCenter();eyes.update(.016f,ms);}
      eyes.lookAt(.9f,0);
      for(uint32_t elapsed=interval;elapsed<=240;elapsed+=interval)
        eyes.update(interval*.001f,2400+elapsed);
      if(interval==16) reference=eyes.frames()[0].pupilX;
      else CHECK(fabsf(eyes.frames()[0].pupilX-reference)<2.0f);
    }
  }
  printf("Animation responsiveness, spontaneous gestures and orientation: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
