#include <stdio.h>
#include <math.h>
#include <initializer_list>
#include "MotionGestures.h"
static int fails=0;
#define CHECK(x) do{if(!(x)){if(fails++<20)printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
struct Fixture {
 WorldFollower w;uint32_t now;uint32_t period;bool hanging;float sign;
 Fixture(uint32_t start=1000,uint32_t step=25,bool upright=false,float pole=1):now(start),period(step),hanging(upright),sign(pole){w.setFlatUpSign(sign);w.beginWakeSession();hold(0,500);CHECK(w.horizontalOriginReady());}
 void one(float angle,float yaw=0,bool allowed=true,float shake=0,float magnitude=9.81f){
  float t=angle*.017453293f,a[3]={magnitude*sinf(t),hanging?magnitude*cosf(t):0,hanging?0:sign*magnitude*cosf(t)};
  float g[3]={0,hanging?yaw:0,hanging?0:yaw};w.feed(a,g,shake,now,nullptr,allowed);now+=period;
 }
 void hold(float angle,uint32_t ms,bool allowed=true,float yaw=0,float shake=0,float magnitude=9.81f){for(uint32_t i=0;i<ms;i+=period)one(angle,yaw,allowed,shake,magnitude);}
};
int main(){
 for(bool hanging:{false,true})for(float pole:{-1.f,1.f})for(uint32_t period:{10u,25u,50u,100u})for(uint32_t began:{1000u,UINT32_MAX-10000u}){
  Fixture f(began,period,hanging,pole);f.hold(25,500);f.hold(25,10000);
  CHECK(f.w.tiltX>.7f && f.w.horizontalRecenterCount()==0);
  float last=f.w.tiltX,maxStep=0;
  for(uint32_t i=0;i<6500;i+=period){f.one(25);maxStep=fmaxf(maxStep,fabsf(f.w.tiltX-last));last=f.w.tiltX;}
  CHECK(f.w.horizontalRecenterCount()==1 && fabsf(f.w.tiltX)<.005f);
  CHECK(maxStep<.2f); // Reference blends; no discontinuous eye jump.
  f.hold(-10,600);CHECK(f.w.tiltX<-.98f);
  f.hold(25,600);CHECK(fabsf(f.w.tiltX)<.005f);
  f.hold(25,40000);CHECK(f.w.horizontalRecenterCount()>=3);
  f.w.beginWakeSession();CHECK(f.w.horizontalRecenterCount()==0);
 }
 // A due refresh waits for quiet: touch/transport/closing gates, deliberate
 // rotation and shaking must each block the reset, even beyond the interval.
 for(int blocked=0;blocked<3;++blocked){
  Fixture f;f.hold(25,500);
  f.hold(25,40000,blocked!=0,blocked==1?.5f:0,blocked==2?3:0);
  CHECK(f.w.horizontalRecenterCount()==0);
  f.hold(25,1000);CHECK(f.w.horizontalRecenterCount()==0);
  f.hold(25,3000);CHECK(f.w.horizontalRecenterCount()==1 && fabsf(f.w.tiltX)<.01f);
 }
 // Invalid accelerometer input and missing reads cannot count as a steady hold.
 for(bool missing:{false,true}){
  Fixture f;f.hold(25,20000,false);f.hold(25,1500);
  if(missing){f.now+=5000;f.one(25);}else f.one(25,0,true,0,20);
  f.hold(25,1500);CHECK(f.w.horizontalRecenterCount()==0);
  f.hold(25,2000);CHECK(f.w.horizontalRecenterCount()==1);
 }
 // Starting to turn during a blend stops it immediately, without completing
 // or rolling back the partially adopted origin.
 {
  Fixture f;f.hold(25,12000);
  for(int i=0;i<1000 && fabsf(f.w.horizontalOriginDegrees())<.1f;++i)f.one(25);
  float partial=f.w.horizontalOriginDegrees();CHECK(fabsf(partial)>.1f);
  f.hold(25,500,true,.5f);
  CHECK(f.w.horizontalRecenterCount()==0 && f.w.horizontalOriginDegrees()==partial);
  f.hold(25,20000,false);CHECK(f.w.horizontalRecenterCount()==0);
  f.hold(25,4000);CHECK(f.w.horizontalRecenterCount()==1);
 }
 printf("Periodic gaze recenter, smoothness, motion gates, missing reads and timer wrap: %s (%d failures)\n",fails?"FAILED":"PASSED",fails);
 return fails?1:0;
}
