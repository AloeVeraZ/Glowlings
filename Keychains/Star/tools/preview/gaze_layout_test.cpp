#include <stdio.h>
#include <math.h>
#include <string>
#include "CreatureAnimator.h"
#include "MotionGestures.h"

static int fails=0;
#define CHECK(x) do { if (!(x)) { if (fails++<20) printf("FAIL line %d: %s\n",__LINE__,#x); } } while (0)
static uint16_t fb[SCREEN_WIDTH*SCREEN_HEIGHT];

// Sample the drawn contours independently of the layout's analytic bounds.
static float checkPair(const EyeFrame *f) {
  float dx=f[1].cx-f[0].cx,dy=f[1].cy-f[0].cy,d=sqrtf(dx*dx+dy*dy);
  CHECK(d>1);
  float ux=dx/d,uy=dy/d,rightmost=-1e6f,leftmost=1e6f;
  for (int eye=0;eye<2;++eye) for (int k=0;k<128;++k) {
    float a=k*anim::TAU_F/128,c=cosf(a),x=f[eye].rx*c;
    float y=f[eye].ry*sinf(a)+f[eye].bend*c*c;
    float sx=f[eye].cx+(x*cosf(f[eye].tilt)-y*sinf(f[eye].tilt))*(eye ? -1 : 1);
    float sy=f[eye].cy+x*sinf(f[eye].tilt)+y*cosf(f[eye].tilt);
    float projection=sx*ux+sy*uy;
    if (eye==0) rightmost=fmaxf(rightmost,projection); else leftmost=fminf(leftmost,projection);
    CHECK((sx-SCREEN_CX)*(sx-SCREEN_CX)+(sy-SCREEN_CY)*(sy-SCREEN_CY)<SCREEN_RADIUS*SCREEN_RADIUS);
  }
  float gap=leftmost-rightmost;
  CHECK(gap>=MIN_EYE_GAP*SCREEN_MIN_SIDE-.03f);
  return gap;
}

static void shot(const std::string &path,EyeRenderer &renderer,Eyes &eyes) {
  renderer.compose(eyes.frames());
  FILE *out=fopen(path.c_str(),"wb"); if (!out) {perror(path.c_str());++fails;return;}
  fprintf(out,"P6\n%d %d\n255\n",SCREEN_WIDTH,SCREEN_HEIGHT);
  for (uint16_t pixel:fb) {
    uint16_t c=(pixel<<8)|(pixel>>8);
    unsigned char rgb[3]={static_cast<unsigned char>(((c>>11)&31)*255/31),
                         static_cast<unsigned char>(((c>>5)&63)*255/63),
                         static_cast<unsigned char>((c&31)*255/31)};
    fwrite(rgb,1,3,out);
  }
  fclose(out);
}

int main(int argc,char **argv) {
  // Heading rotation has no accelerometer signature. A large turn must stay
  // at the edge while held, then retrace correctly when physically returned.
  for (bool hanging : {false,true}) for (int sign : {-1,1}) for (uint32_t period : {10u,25u,50u}) {
    WorldFollower world; world.setFlatUpSign(1); world.beginWakeSession();
    uint32_t now=1;
    float a[3]={0,hanging?9.81f:0,hanging?0:9.81f},zero[3]={};
    auto hold=[&](uint32_t ms,const float g[3]) {
      for (uint32_t elapsed=0;elapsed<ms;elapsed+=period) {world.feed(a,g,0,now);now+=period;}
    };
    hold(1000,zero);
    float turn[3]={0,hanging?-sign*.52359878f:0,hanging?0:-sign*.52359878f}; // 30 deg/s
    hold(4000,turn); // 120 degrees out, well past full eye travel
    CHECK(world.tiltX*sign<-.98f && fabsf(world.tiltY)<.02f);
    hold(20000,zero); CHECK(world.tiltX*sign<-.98f); // no automatic recenter while held
    now+=500; hold(100,zero); CHECK(world.tiltX*sign<-.98f); // a missed-read interval cannot reset the turn
    for (float &v:turn) v=-v;
    hold(2500,turn); // still 45 degrees out after returning 75 degrees
    CHECK(world.tiltX*sign<-.98f);
    hold(1500,turn); hold(700,zero);
    CHECK(fabsf(world.tiltX)<.02f && fabsf(world.tiltY)<.02f);
  }
  // A tilt through the side-on position keeps looking the same way. Gyro and
  // acceleration agree throughout the excursion and its reverse path.
  for (bool hanging : {false,true}) for (int sign : {-1,1}) {
    WorldFollower world; world.setFlatUpSign(1); world.beginWakeSession(); uint32_t now=1;
    float zero[3]={},neutral[3]={0,hanging?9.81f:0,hanging?0:9.81f};
    for (;now<1001;now+=25) world.feed(neutral,zero,0,now);
    for (int leg=0;leg<2;++leg) for (int step=1;step<=240;++step) {
      float degrees=leg ? 140-step*(140.0f/240) : step*(140.0f/240);
      float theta=degrees*.017453293f,omega=(leg?-1:1)*sign*(140.0f/6)*.017453293f;
      float a[3]={sign*9.81f*sinf(theta),hanging?9.81f*cosf(theta):0,hanging?0:9.81f*cosf(theta)};
      float g[3]={0,hanging?0:-omega,hanging?omega:0};
      world.feed(a,g,0,now);now+=25;
      if (degrees>40) CHECK(world.tiltX*sign>.95f);
      CHECK(fabsf(world.tiltY)<.02f);
    }
    for (int i=0;i<40;++i) {world.feed(neutral,zero,0,now);now+=25;}
    CHECK(fabsf(world.tiltX)<.02f);
  }

  EyeRenderer renderer; Eyes eyes; renderer.begin(fb,nullptr);eyes.begin(&renderer,1234,0);
  eyes.setIdle(false);eyes.setAutoBlink(false);eyes.setSaccades(false);eyes.setPresence(1);
  uint32_t now=0;float smallest=1e6f;
  // All authored expressions, full gaze, eye growth, rotation, and travel
  // between opposite corners: enforce separation at every animation frame.
  for (int e=0;e<EXPRESSION_COUNT;++e) {
    eyes.setExpression(Expression(e),1.3f);
    for (int direction=0;direction<9;++direction) {
      float x=direction%3-1,y=direction/3-1;
      eyes.lookAt(x,y,1.6f);eyes.setFaceRoll((direction%3-1)*.3f);
      for (int frame=0;frame<55;++frame) {
        now+=25;eyes.update(.025f,now);
        smallest=fminf(smallest,checkPair(eyes.frames()));
      }
    }
  }
  eyes.setFaceRoll(0);
  if (argc>1) for (Expression e : {NEUTRAL,CURIOUS,SURPRISED}) {
    eyes.setExpression(e);
    for (int direction=0;direction<5;++direction) {
      const float x[5]={-1,0,1,-1,1},y[5]={0,0,0,1,1};
      eyes.lookAt(x[direction],y[direction]);
      for (int i=0;i<60;++i) {now+=25;eyes.update(.025f,now);}
      shot(std::string(argv[1])+"/"+expressionName(e)+"_"+std::to_string(direction)+".ppm",renderer,eyes);
    }
  }
  printf("Extreme motion, held heading, return path and paired eye layout: %s (%d failures), minimum gap %.2f px\n",fails?"FAILED":"PASSED",fails,smallest);
  return fails?1:0;
}
