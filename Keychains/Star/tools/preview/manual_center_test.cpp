#include <stdio.h>
#include <math.h>
#include "InteractionPolicy.h"
#include "MotionGestures.h"
#include "gaze_expect.h"
#include "palette.h"
static int fails=0;
#define CHECK(x) do{if(!(x)){if(fails++<20)printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
static uint16_t fb[SCREEN_WIDTH*SCREEN_HEIGHT];
int main(){
 for(uint32_t began:{1000u,UINT32_MAX-1000u}){
  for(int gesture=0;gesture<8;++gesture){
   TouchTracker touch;AlwaysOnGesture mode;ScreenTapRun taps;int toggles=0,requests=0;
   auto dispatch=[&](const TouchTracker::Result &r,uint32_t ms){
    bool consumed=mode.feed(r,began+ms);
    toggles+=mode.poll(touch.tracking(),began+ms);
    if(!consumed && !mode.reserved() && r.kind==TouchTracker::TAP)
      requests+=taps.add(began+ms)<=2 && mode.doubleTapped;
   };
   auto event=[&](int x,int kind,uint32_t ms){dispatch(touch.feed(x,120,kind,0,true,began+ms),ms);};
   if(gesture==1 || gesture==2 || gesture>=5){event(120,0,0);event(120,1,80);}
   if(gesture==5){event(120,0,600);event(120,1,680);CHECK(requests==0 && toggles==0);continue;} // too slow
   if(gesture==6){event(160,0,200);event(160,1,280);CHECK(requests==0 && toggles==0);continue;} // too far apart
   event(120,0,200);
   if(gesture==2){event(120,1,280);CHECK(toggles==0 && requests==1);dispatch({},300);CHECK(requests==1);continue;}
   if(gesture==7){event(160,2,240);event(160,1,280);CHECK(requests==0 && toggles==0);continue;} // second press dragged
   for(uint32_t ms=220;ms<6000;ms+=20){
    if(gesture==3 && ms==600)event(160,2,ms);
    if(gesture==4 && ms>=700){dispatch(touch.poll(began+ms),ms);continue;}
    touch.confirmContact(began+ms);dispatch({},ms);
   }
   event(gesture==3?160:120,1,6000);
   if(gesture==0)CHECK(toggles==0 && requests==0); // holding no longer centers
   if(gesture==1)CHECK(toggles==1 && requests==0); // mode toggle never centers
   if(gesture==3 || gesture==4)CHECK(toggles==0 && requests==0);
  }
  for(uint32_t gap:{TOUCH_CENTER_GAP_MS,TOUCH_CENTER_GAP_MS+1}) {
   AlwaysOnGesture mode;TouchTracker touch;
   auto event=[&](int kind,uint32_t ms,bool allowed=true){mode.feed(touch.feed(120,120,kind,0,true,began+ms),began+ms,allowed);};
   event(0,0);event(1,80);event(0,80+gap);event(1,160+gap);
   CHECK(mode.doubleTapped==(gap==TOUCH_CENTER_GAP_MS));
   mode.feed({},began+161+gap);CHECK(!mode.doubleTapped); // one event, no retrigger
  }
  { // The latest allowed second press can still last a full short-tap interval.
   AlwaysOnGesture mode;TouchTracker touch;ScreenTapRun taps;
   mode.feed(touch.feed(120,120,0,0,true,began),began);
   mode.feed(touch.feed(120,120,1,0,true,began+80),began+80);taps.add(began+80);
   uint32_t second=began+80+TOUCH_CENTER_GAP_MS,release=second+TAP_MAX_MS;
   mode.feed(touch.feed(120,120,0,0,true,second),second);
   mode.feed(touch.feed(120,120,1,0,true,release),release);
   CHECK(mode.doubleTapped && taps.add(release)<=2);
  }
  { // Repeated taps still form one run: the third/fourth are anger, tenth is battery.
   AlwaysOnGesture mode;TouchTracker touch;ScreenTapRun taps;int requests=0;
   for(uint32_t tap=0;tap<10;++tap) {
    uint32_t ms=tap*200;
    mode.feed(touch.feed(120,120,0,0,true,began+ms),began+ms);
    mode.feed(touch.feed(120,120,1,0,true,began+ms+80),began+ms+80);
    requests+=taps.add(began+ms+80)==2 && mode.doubleTapped;
   }
   CHECK(requests==1 && taps.count==TOUCH_BATTERY_TAPS);
  }
  { // Confirmed completed taps still work when a press report was missed.
   AlwaysOnGesture mode;TouchTracker touch;
   mode.feed(touch.feed(120,120,1,5,true,began),began);
   mode.feed(touch.feed(120,120,1,5,true,began+200),began+200);
   CHECK(mode.doubleTapped);
   mode.feed({},began+201,false);CHECK(!mode.doubleTapped && !mode.reserved());
  }
 }
 // Exercise the actual fusion -> animator -> pupil path. The double tap must
 // change the origin, not suppress subsequent motion with a display timer.
 EyeRenderer renderer;Eyes eyes;CreatureAnimator c;
 renderer.begin(fb,nullptr);eyes.begin(&renderer,1234,0);c.begin(&eyes,1234,0);
 c.setIdleActsAllowed(false);c.startWake(0,false);
 for(uint32_t ms=20;ms<=5000;ms+=20)c.update(.02f,ms);
 WorldFollower world;world.setFlatUpSign(1);world.beginWakeSession();
 uint32_t now=5000;float quiet[3]={};
 auto pose=[&](float degrees,uint32_t duration){
   float r=degrees*.017453293f,a[3]={9.81f*sinf(r),0,9.81f*cosf(r)};
   for(uint32_t i=0;i<duration;i+=25){
     now+=25;world.feed(a,quiet,0,now);
     c.setTilt(world.tiltX,world.tiltY);c.setSwing(world.swingX,world.swingY);
     c.update(.025f,now);
   }
 };
 pose(0,500);pose(25,1000);
 CHECK(fabsf(world.tiltX-expectedHorizontalGaze(25))<.025f && eyes.frames()[0].pupilX*HORIZONTAL_GAZE_SIGN<-.25f*eyes.frames()[0].pupilR);
 c.setPointer(0,0,now);c.setPointerHeld(true,now);c.react(Mood::FOLLOWING,now,600);
 pose(25,2000);
 // Match the actual double-click path: preserve its existing surprised
 // reaction, then clear the finger target while adopting the motion zero.
 c.setPointer(.7f,0,now);CHECK(c.react(tapMood(2),now,1050));
 world.requestCenter(now);c.centerGaze(now);
 CHECK(c.mood()==Mood::SURPRISED);
 CHECK(world.horizontalOriginReady() && world.tiltX==0 && world.tiltY==0);
 pose(25,150);CHECK(fabsf(world.tiltX)<.001f);
 // The double-click choreography must not lock the fused motion target.
 pose(37,150);
 CHECK(world.tiltX*HORIZONTAL_GAZE_SIGN<.85f*expectedHorizontalGaze(12)*HORIZONTAL_GAZE_SIGN && c.mood()==Mood::SURPRISED);
 pose(13,300);CHECK(fabsf(world.tiltX-expectedHorizontalGaze(-12))<.025f);
 pose(25,1000);CHECK(c.mood()==Mood::IDLE && fabsf(world.tiltX)<.001f && fabsf(eyes.frames()[0].pupilX)<3);
 pose(37,150);CHECK(eyes.frames()[0].pupilX*HORIZONTAL_GAZE_SIGN<3*expectedHorizontalGaze(12)*HORIZONTAL_GAZE_SIGN);
 pose(13,300);CHECK(eyes.frames()[0].pupilX*HORIZONTAL_GAZE_SIGN>-3*expectedHorizontalGaze(12)*HORIZONTAL_GAZE_SIGN);
 pose(25,600);CHECK(fabsf(world.tiltX)<.001f);
 // The new zero persists after the cue, without a second reference reset.
 pose(40,300);CHECK(fabsf(world.tiltX-expectedHorizontalGaze(15))<.025f);
 for(int i=0;i<40;++i){now+=25;float a[3]={9.81f*sinf(.6981317f),0,9.81f*cosf(.6981317f)};world.feed(a,quiet,0,now);}
 CHECK(fabsf(world.tiltX-expectedHorizontalGaze(15))<.025f);
 // Up/down: the double-tap pose is dead ahead, whatever posture it is in, and
 // stays the zero when the lean crosses between the table and hanging postures.
 // Leaning screen-top away from the viewer is a rotation about X (gravity Y/Z).
 for(float zeroDeg:{0.0f,30.0f,45.0f,60.0f,90.0f,-30.0f}){
   WorldFollower w;w.setFlatUpSign(1);w.beginWakeSession();uint32_t t=20000;float g[3]={};
   auto lean=[&](float degrees,uint32_t duration){
     float r=degrees*.017453293f,a[3]={0,9.81f*sinf(r),9.81f*cosf(r)};
     for(uint32_t i=0;i<duration;i+=25){t+=25;w.feed(a,g,0,t);}
   };
   lean(zeroDeg,1500);
   w.requestCenter(t);CHECK(w.tiltY==0);
   lean(zeroDeg,600);CHECK(fabsf(w.tiltY)<.001f);
   // Small wobble inside the 8-degree neutral zone stays centered.
   lean(zeroDeg+6,400);CHECK(fabsf(w.tiltY)<.001f);
   lean(zeroDeg-6,400);CHECK(fabsf(w.tiltY)<.001f);
   // A clear lean either way from the new zero moves the eyes, in opposite
   // directions, and by the same amount it would from the flat zero.
   lean(zeroDeg+20,600);float plus=w.tiltY;
   lean(zeroDeg-20,600);float minus=w.tiltY;
   CHECK(plus*minus<0 && fabsf(plus)>.25f && fabsf(minus)>.15f);
   // Back at the zero pose: straight ahead again, including after crossing
   // into the other posture (60+20 and 45+20 are hanging, 30-20 is flat).
   lean(zeroDeg,800);CHECK(fabsf(w.tiltY)<.001f);
   // The next wake goes back to the posture's own neutral.
   w.beginWakeSession();lean(zeroDeg,1500);
   if(zeroDeg==0 || zeroDeg==90)CHECK(fabsf(w.tiltY)<.001f);
 }
 { // Same direction as before for a zero taken flat: leaning one way looks one way.
   WorldFollower flat,zeroed;flat.setFlatUpSign(1);zeroed.setFlatUpSign(1);
   flat.beginWakeSession();zeroed.beginWakeSession();uint32_t t=50000;float g[3]={};
   float a0[3]={0,0,9.81f};
   for(int i=0;i<60;++i){t+=25;flat.feed(a0,g,0,t);zeroed.feed(a0,g,0,t);}
   zeroed.requestCenter(t);
   float r=20*.017453293f,a[3]={0,9.81f*sinf(r),9.81f*cosf(r)};
   for(int i=0;i<40;++i){t+=25;flat.feed(a,g,0,t);zeroed.feed(a,g,0,t);}
   CHECK(fabsf(flat.tiltY-zeroed.tiltY)<.01f && fabsf(flat.tiltY)>.25f);
 }
 // Relative gyro heading is also reset, without learning a fake sensor bias.
 for(uint32_t start:{1000u,UINT32_MAX-1000u}){
   WorldFollower w;w.setFlatUpSign(1);w.beginWakeSession();uint32_t t=start;
   float a[3]={0,0,9.81f},g[3]={};
   for(int i=0;i<40;++i,t+=25)w.feed(a,g,0,t);
   g[2]=-.6f;for(int i=0;i<80;++i,t+=25)w.feed(a,g,0,t);
   CHECK(w.tiltX*HORIZONTAL_GAZE_SIGN>.98f);
   w.requestHorizontalCenter(t);CHECK(w.horizontalOriginReady() && w.tiltX==0);
   g[2]=-1.2f; // About ten degrees of new rotation in 150 ms.
   for(int i=0;i<6;++i,t+=25)w.feed(a,g,0,t);
   CHECK(w.tiltX*HORIZONTAL_GAZE_SIGN>.08f); // New motion is live in 150 ms; no quiet-capture wait.
   // A stale sample uses the safe stable-capture fallback instead.
   t+=1000;w.requestHorizontalCenter(t);CHECK(!w.horizontalOriginReady());g[2]=0;
   for(int i=0;i<40;++i,t+=25)w.feed(a,g,0,t);
   CHECK(w.horizontalOriginReady() && fabsf(w.tiltX)<.001f);
 }
 // A new touch still follows normally; critical sleep keeps priority.
 c.centerGaze(now);c.setPointer(-1,0,now);c.setPointerHeld(true,now);c.react(Mood::FOLLOWING,now,600);
 for(int i=0;i<40;++i){now+=20;c.setTilt(0,0);c.update(.02f,now);}
 CHECK(eyes.frames()[0].pupilX<-.2f*eyes.frames()[0].pupilR);
 c.startSleep(now,0);c.centerGaze(now);CHECK(c.asleep());
 for(uint32_t began:{1000u,UINT32_MAX-6000u})for(bool powered:{false,true}) {
   EyeRenderer r;Eyes e;CreatureAnimator anim;
   r.begin(fb,nullptr);e.begin(&r,1234,began);anim.begin(&e,1234,began);
   anim.setIdleActsAllowed(false);e.setIdle(false);e.setAutoBlink(false);
   anim.setBatteryLevel(88,powered);anim.startWake(began,false);
   uint32_t t=began;
   auto run=[&](uint32_t ms){for(uint32_t i=0;i<ms;i+=20){t+=20;anim.update(.02f,t);}};
   run(5000);CHECK(anim.showRecenter(t));CHECK(anim.mood()==Mood::RECENTER);
   float least=1;
   for(uint32_t i=0;i<RECENTER_ANIM_MS;i+=20) {
     t+=20;anim.update(.02f,t);e.draw();least=fminf(least,e.openness(0));
     for(uint16_t pixel:fb)CHECK(inPalette(pixel,r.getEyeColor()));
   }
   CHECK(least<.55f && e.openness(0)>.9f && anim.mood()==Mood::IDLE);
   // Automatic completion emits one cue; rejected/busy states do not queue it.
   WorldFollower automatic;automatic.setFlatUpSign(1);automatic.beginWakeSession();
   int cues=0;
   for(uint32_t ms=0;ms<18000;ms+=25) {
     t+=25;float angle=(ms<500?0:25)*.017453293f;
     float a[3]={9.81f*sinf(angle),0,9.81f*cosf(angle)},g[3]={};
     auto count=automatic.horizontalRecenterCount();automatic.feed(a,g,0,t);
     anim.setTilt(automatic.tiltX,automatic.tiltY);
     if(automatic.horizontalRecenterCount()!=count){CHECK(anim.showRecenter(t));++cues;}
     anim.update(.025f,t);
   }
   CHECK(cues==1 && anim.mood()==Mood::IDLE);
   anim.react(Mood::BOOP,t,900);CHECK(!anim.showRecenter(t));
   anim.showBattery(88,t);CHECK(!anim.showRecenter(t));
   anim.startSleep(t,0);CHECK(!anim.showRecenter(t));
 }
 printf("Double-tap center, live tracking, recenter cue, automatic completion and mode separation: %s (%d failures)\n",fails?"FAILED":"PASSED",fails);
 return fails?1:0;
}
