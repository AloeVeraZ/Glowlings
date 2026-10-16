#include <stdio.h>
#include <math.h>
#include "InteractionPolicy.h"
#include "MotionGestures.h"
static int fails=0;
#define CHECK(x) do{if(!(x)){if(fails++<20)printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
static uint16_t fb[SCREEN_WIDTH*SCREEN_HEIGHT];
int main(){
 for(uint32_t began:{1000u,UINT32_MAX-1000u}){
  for(int gesture=0;gesture<5;++gesture){
   TouchTracker touch;AlwaysOnGesture mode;HoldToCenterGesture center;int toggles=0,requests=0;
   auto dispatch=[&](const TouchTracker::Result &r,uint32_t ms){
    bool consumed=mode.feed(r,began+ms);
    toggles+=mode.poll(touch.tracking(),began+ms);
    center.feed(r,began+ms,!consumed && !mode.reserved());
    requests+=center.poll(touch.tracking(),began+ms) || center.released;
   };
   auto event=[&](int x,int kind,uint32_t ms){dispatch(touch.feed(x,120,kind,0,true,began+ms),ms);};
   if(gesture==1 || gesture==2){event(120,0,0);event(120,1,80);}
   event(120,0,200);
   if(gesture==2){event(120,1,280);CHECK(toggles==0 && requests==0);continue;}
   for(uint32_t ms=220;ms<6000;ms+=20){
    if(gesture==3 && ms==600)event(160,2,ms);
    if(gesture==4 && ms>=700){dispatch(touch.poll(began+ms),ms);continue;}
    touch.confirmContact(began+ms);dispatch({},ms);
   }
   event(gesture==3?160:120,1,6000);
   if(gesture==0)CHECK(toggles==0 && requests==2); // first hold + release refresh
   if(gesture==1)CHECK(toggles==1 && requests==0); // mode toggle never centers
   if(gesture==3 || gesture==4)CHECK(toggles==0 && requests==0);
  }
  // Release exactly at the threshold works without an earlier due poll.
  HoldToCenterGesture exact;TouchTracker t;
  exact.feed(t.feed(120,120,0,0,true,began),began);
  auto r=t.feed(120,120,1,0,true,began+TOUCH_CENTER_PRESS_MS);
  CHECK(exact.feed(r,began+TOUCH_CENTER_PRESS_MS) && exact.released);
 }
 WorldFollower world;world.setFlatUpSign(1);world.beginWakeSession();
 float quiet[3]={},flat[3]={0,0,9.81f};uint32_t now=1000;
 for(int i=0;i<40;++i,now+=25)world.feed(flat,quiet,0,now);
 float lean[3]={9.81f*sinf(.4363323f),0,9.81f*cosf(.4363323f)};
 for(int i=0;i<40;++i,now+=25)world.feed(lean,quiet,0,now);
 CHECK(world.tiltX>.7f);
 float vertical=world.tiltY;
 world.requestHorizontalCenter();CHECK(!world.horizontalOriginReady() && world.tiltX==0);
 for(int i=0;i<80;++i,now+=25)world.feed(lean,quiet,0,now);
 CHECK(world.horizontalOriginReady() && fabsf(world.tiltX)<.001f && world.tiltY==vertical);
 EyeRenderer renderer;Eyes eyes;CreatureAnimator c;
 renderer.begin(fb,nullptr);eyes.begin(&renderer,1234,0);c.begin(&eyes,1234,0);
 c.setIdleActsAllowed(false);c.startWake(0,false);
 for(uint32_t ms=20;ms<=5000;ms+=20)c.update(.02f,ms);
 now=5000;c.setTilt(1,0);
 for(int i=0;i<50;++i){now+=20;c.update(.02f,now);}
 CHECK(eyes.frames()[0].pupilX>eyes.frames()[0].pupilR*.25f);
 c.setPointer(0,0,now);c.setPointerHeld(true,now);c.react(Mood::FOLLOWING,now,600);
 for(int i=0;i<100;++i){now+=20;c.update(.02f,now);}
 c.centerAfterHold(now);
 // Holding for longer than five seconds keeps the rest timer alive.
 for(int i=0;i<350;++i){now+=20;c.keepCenterHeld(now);c.setTilt(1,0);c.update(.02f,now);}
 CHECK(fabsf(eyes.frames()[0].pupilX)<.5f);
 uint32_t released=now;c.centerAfterHold(now);
 // Old pointer and sensor targets cannot restore the rightward gaze during
 // the requested five-second rest, even with idle gaze enabled.
 for(int i=0;i<249;++i){now+=20;c.setTilt(1,0);c.setMotionActivity(.3f,now);c.update(.02f,now);if(i>20)CHECK(fabsf(eyes.frames()[0].pupilX)<.5f);}
 CHECK(now-released==4980);
 for(int i=0;i<50;++i){now+=20;c.update(.02f,now);}
 CHECK(eyes.frames()[0].pupilX>eyes.frames()[0].pupilR*.25f); // normal motion resumes
 // New pointer input can interrupt the rest; critical sleep always wins.
 c.centerAfterHold(now);c.setPointer(-1,0,now);c.setPointerHeld(true,now);c.react(Mood::FOLLOWING,now,600);
 for(int i=0;i<40;++i){now+=20;c.setTilt(0,0);c.update(.02f,now);}
 CHECK(eyes.frames()[0].pupilX<-.2f*eyes.frames()[0].pupilR);
 c.startSleep(now,0);c.centerAfterHold(now);CHECK(c.asleep());
 printf("Single-hold center, mode separation, release persistence and five-second rest: %s (%d failures)\n",fails?"FAILED":"PASSED",fails);
 return fails?1:0;
}
