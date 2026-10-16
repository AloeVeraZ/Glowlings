#include <stdio.h>
#include <math.h>
#include <initializer_list>
#include <limits>
#include "MotionGestures.h"
static int fails=0;
#define CHECK(x) do {if(!(x)){if(fails++<20)printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
int main(){
  // Resting angles, both screen-up polarities, all configured XY mountings,
  // realistic read periods and a wake that crosses the millis rollover.
  for(bool hanging:{false,true})for(float sign:{-1.f,1.f})for(int mounting:{0,1,2,3})
  for(float origin:{-50.f,-12.f,0.f,17.f,45.f})for(uint32_t period:{10u,25u,50u,100u})
  for(uint32_t began:{1000u,UINT32_MAX-200u}){
    WorldFollower w;w.setFlatUpSign(sign);w.setHorizontalCenter(9,-7);
    uint32_t now=began;
    auto hold=[&](float degrees,uint32_t duration){
      float rad=degrees*.017453293f;
      float a[3]={9.81f*sinf(rad),hanging?9.81f*cosf(rad):0,hanging?0:sign*9.81f*cosf(rad)},g[3]={};
      rotateMotionXY(a,(4-mounting)&3); // Sensor-native mounting.
      for(uint32_t ms=0;ms<duration;ms+=period){
        float visual[3]={a[0],a[1],a[2]};rotateMotionXY(visual,mounting);
        w.feed(visual,g,0,now);now+=period;
      }
    };
    w.beginWakeSession();CHECK(!w.horizontalOriginReady());
    hold(origin,100);CHECK(!w.horizontalOriginReady() && fabsf(w.tiltX)<.001f);
    hold(origin,500);CHECK(w.horizontalOriginReady() && fabsf(w.tiltX)<.001f);
    float fixed=w.horizontalOriginDegrees();
    hold(origin+12,500);CHECK(w.tiltX>.27f && w.tiltX<.35f);
    hold(origin-12,500);CHECK(w.tiltX<-.27f && w.tiltX>-.35f);
    hold(origin,500);CHECK(fabsf(w.tiltX)<.001f);
    hold(origin+35,2000);CHECK(w.tiltX>.98f);
    CHECK(w.horizontalOriginDegrees()==fixed);
    now+=5000;hold(origin+35,500);CHECK(w.tiltX>.98f); // Stall isn't a new wake.
    hold(origin,500);CHECK(fabsf(w.tiltX)<.001f);
    // A subsequent awake interval gets a fresh pose, not the previous offset.
    w.beginWakeSession();hold(origin-8,600);
    CHECK(w.horizontalOriginReady() && fabsf(w.tiltX)<.001f);
    CHECK(fabsf(w.horizontalOriginDegrees()-fixed)>5);
  }
  {
    WorldFollower w;w.setFlatUpSign(1);w.beginWakeSession();
    float flat[3]={0,0,9.81f},g[3]={},bad[3]={0,0,20};uint32_t t=1000;
    for(;t<1100;t+=25)w.feed(flat,g,0,t);
    w.feed(bad,g,0,t);t+=25;
    for(uint32_t end=t+100;t<end;t+=25)w.feed(flat,g,0,t);
    CHECK(!w.horizontalOriginReady()); // Invalid sample broke the quiet window.
    g[2]=.3f;
    for(uint32_t end=t+1000;t<end;t+=25){w.feed(flat,g,0,t);CHECK(!w.horizontalOriginReady() && w.tiltX==0);}
    g[2]=0;
    for(uint32_t end=t+1000;t<end;t+=25){w.feed(flat,g,3,t);CHECK(!w.horizontalOriginReady());}
    for(uint32_t end=t+500;t<end;t+=25)w.feed(flat,g,0,t);
    CHECK(w.horizontalOriginReady() && fabsf(w.tiltX)<.001f);
    // Deliberate turns persist until the periodic interval and a steady hold.
    g[2]=-.5f;
    for(uint32_t end=t+1400;t<end;t+=25)w.feed(flat,g,0,t);
    g[2]=0;
    for(uint32_t end=t+2000;t<end;t+=25)w.feed(flat,g,0,t);
    CHECK(w.tiltX<-.98f);
    for(uint32_t end=t+60000;t<end;t+=25)w.feed(flat,g,0,t);
    CHECK(fabsf(w.tiltX)<.001f && w.horizontalRecenterCount()>=3);
    t+=5000;w.feed(flat,g,0,t);CHECK(fabsf(w.tiltX)<.001f);
    w.beginWakeSession();
    for(uint32_t end=t+500;t<end;t+=25)w.feed(flat,g,0,t);
    CHECK(w.horizontalOriginReady() && fabsf(w.tiltX)<.001f);
  }
  // A sideways/inverted wake still starts at center rather than at an edge.
  for(float origin:{-175.f,-95.f,95.f,175.f}){
    WorldFollower w;w.setFlatUpSign(1);w.beginWakeSession();
    float r=origin*.017453293f,a[3]={9.81f*sinf(r),9.81f*cosf(r),0},g[3]={};
    for(uint32_t t=1;t<1000;t+=25)w.feed(a,g,0,t);
    CHECK(w.horizontalOriginReady() && fabsf(w.tiltX)<.001f);
  }
  printf("Wake-relative horizontal origin, stability, mountings and session retention: %s (%d failures)\n",fails?"FAILED":"PASSED",fails);
  return fails?1:0;
}
