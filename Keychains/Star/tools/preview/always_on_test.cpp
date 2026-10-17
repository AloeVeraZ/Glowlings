#include <stdio.h>
#include <vector>
#include "InteractionPolicy.h"
#include "ChargeFlow.h"
#include "palette.h"
#define CHECK(x) do { if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++fails;} } while(0)
static uint16_t fb[SCREEN_WIDTH*SCREEN_HEIGHT];
int main() {
 int fails=0;
 for(uint32_t began:{1000u,UINT32_MAX-500u}) {
  AlwaysOnGesture gesture;TouchTracker touch;
  auto feed=[&](int x,int y,int event,uint32_t ms) {
   auto r=touch.feed(x,y,event,0,true,began+ms);
   bool consumed=gesture.feed(r,began+ms);
   return consumed;
  };
  feed(120,120,0,0);CHECK(!feed(120,120,1,80));
  CHECK(feed(120,120,0,200));CHECK(!feed(120,120,1,280)); // ordinary double tap survives
  CHECK(!gesture.poll(touch.tracking(),began+1200));
  feed(120,120,0,1000); // within 420 ms? no: a single hold must not arm
  for(uint32_t ms=1000;ms<2200;ms+=40) {touch.confirmContact(began+ms);CHECK(!gesture.poll(touch.tracking(),began+ms));}
  feed(120,120,1,2200);
  feed(120,120,0,3000);feed(120,120,1,3080);CHECK(feed(121,120,0,3200));
  int toggles=0;
  for(uint32_t ms=3200;ms<7000;ms+=40) {touch.confirmContact(began+ms);toggles+=gesture.poll(touch.tracking(),began+ms);}
  CHECK(toggles==1 && gesture.reserved());CHECK(feed(121,120,1,7000));CHECK(!gesture.reserved());
  feed(120,120,0,8000);feed(120,120,1,8080);feed(120,120,0,8200);
  feed(160,120,2,8300);CHECK(!gesture.poll(true,began+9200));feed(160,120,1,9300); // drag cancels
  feed(120,120,0,10000);feed(120,120,1,10080);feed(120,120,0,10200);
  auto lost=touch.poll(began+10500);gesture.feed(lost,began+10500);CHECK(!gesture.poll(touch.tracking(),began+11200));
  feed(120,120,0,12000);feed(120,120,1,12080);feed(120,120,0,12200);
  CHECK(!gesture.poll(true,began+13040));toggles+=gesture.poll(true,began+13060);
  CHECK(toggles==2);CHECK(!gesture.poll(true,began+14000));CHECK(feed(120,120,1,14100));
  AwakeLimit limit;limit.start(began);limit.alwaysOn=true;
  for(uint32_t ms=0;ms<3600000;ms+=10000) CHECK(!limit.closingDue(began+ms));
  limit.alwaysOn=false;limit.interaction(began+3600000);
  CHECK(!limit.closingDue(began+3600000+IDLE_SLEEP_MS-SLEEP_SEQUENCE_MS-1));
  CHECK(limit.closingDue(began+3600000+IDLE_SLEEP_MS-SLEEP_SEQUENCE_MS));
  CHECK(BatteryState::mustRemainAsleep(2,3.4f,false)); // mode cannot change the critical gate
 }
 for(float cut:{.01f,.25f,.7f,1.1f,9.4f}) {
  int previous=2*CHARGE_BUBBLES;
  for(float phase=cut;phase<=cut+1.4f;phase+=.01f) {
   int active=0;
   for(int eye=0;eye<2;++eye) for(int i=0;i<CHARGE_BUBBLES;++i) active+=ChargeFlow::launchedBefore(phase,cut,i,eye);
   CHECK(active<=previous);previous=active;
   if(phase>=ChargeFlow::finishPhase(cut)) CHECK(active==0);
  }
  CHECK(previous==0);
 }
 EyeRenderer renderer;Eyes eyes;CreatureAnimator c;
 renderer.begin(fb,nullptr);eyes.begin(&renderer,1234,0);c.begin(&eyes,1234,0);
 c.setIdleActsAllowed(false);eyes.setIdle(false);eyes.setAutoBlink(false);
 uint32_t now=0;
 auto run=[&](uint32_t duration){for(uint32_t ms=0;ms<duration;ms+=20){now+=20;c.update(.020f,now);}};
 c.startWake(now,false);run(5000);c.setBatteryLevel(100,true);run(3000);
 CHECK(renderer.getEyeColor()==BATTERY_CHARGING_COLOR);
 c.setBatteryLevel(100,false);run(20);CHECK(c.finishingCharge());
 run(100);CHECK(renderer.getEyeColor()==BATTERY_CHARGING_COLOR);
 run(4200);CHECK(!c.finishingCharge());run(3000);CHECK(renderer.getEyeColor()==EYE_COLOR);
 eyes.draw();for(int y=205;y<SCREEN_HEIGHT;++y) for(int x=0;x<SCREEN_WIDTH;++x) CHECK(fb[y*SCREEN_WIDTH+x]==0);
 c.setAlwaysOn(true,now);CHECK(c.alwaysOnEnabled() && c.mood()==Mood::ALWAYS_ON);run(2000);eyes.draw();
 // Toggling the renderer on the same geometry changes only black pupil pixels.
 renderer.setAwakeMode(false,0);renderer.compose(eyes.frames());
 std::vector<uint16_t> unmarked(fb,fb+SCREEN_WIDTH*SCREEN_HEIGHT);
 renderer.setAwakeMode(true,0);renderer.compose(eyes.frames());
 int marks[2]={0,0},changed=0;
 for(int y=0;y<SCREEN_HEIGHT;++y) for(int x=0;x<SCREEN_WIDTH;++x) {
   int pixel=y*SCREEN_WIDTH+x;if(fb[pixel]==unmarked[pixel])continue;
   ++changed;CHECK(unmarked[pixel]==PIXEL_BACKGROUND);
   bool inside=false;
   for(int i=0;i<2;++i) {
     const EyeFrame &f=eyes.frames()[i];
     float dx=(x-f.cx-f.pupilX)/f.pupilSX,dy=(y-f.cy-f.pupilY)/f.pupilSY;
     if(dx*dx+dy*dy<f.pupilR*f.pupilR){inside=true;++marks[i];} // starfall stays inside the black pupil
   }
   CHECK(inside);CHECK(y<205);
 }
 CHECK(changed>12 && marks[0]>6 && marks[1]>6);
 // A shut eye, heart pupil or dizzy spiral must be identical to mode-off.
 EyeFrame hidden[2]={eyes.frames()[0],eyes.frames()[1]};
 for(int variant=0;variant<3;++variant) {
   for(int i=0;i<2;++i) {
     hidden[i]=eyes.frames()[i];
     if(variant==0) {hidden[i].topA=hidden[i].botA=0;hidden[i].topB=hidden[i].botB=hidden[i].topC=hidden[i].botC=0;}
     if(variant==1)hidden[i].heart=1;
     if(variant==2)hidden[i].spiral=1;
   }
   renderer.setAwakeMode(false,0);renderer.compose(hidden);
   std::vector<uint16_t> baseline(fb,fb+SCREEN_WIDTH*SCREEN_HEIGHT);
   renderer.setAwakeMode(true,0);renderer.compose(hidden);
   CHECK(memcmp(fb,baseline.data(),sizeof(fb))==0);
 }
 renderer.setAwakeMode(true,0);renderer.compose(eyes.frames());
 for(uint16_t px:fb) CHECK(inPalette(px,renderer.getEyeColor()));
 c.setAlwaysOn(false,now);CHECK(!c.alwaysOnEnabled() && c.mood()==Mood::AUTO_SLEEP);run(2000);eyes.draw();
 for(int y=205;y<225;++y) for(int x=90;x<150;++x) CHECK(fb[y*SCREEN_WIDTH+x]==0);
 c.setAlwaysOn(true,now);c.startSleep(now,0);run(SLEEP_SEQUENCE_MS+100);CHECK(c.asleep() && c.sleepFinished(now));
 printf("Always-on gesture, critical priority and charge finish: %s (%d failures)\n",fails?"FAILED":"PASSED",fails);
 return fails?1:0;
}
