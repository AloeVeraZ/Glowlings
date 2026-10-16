// A labeled catalog is assembled from these real firmware frames by catalog.py.
#include <stdio.h>
#include <string>
#include <vector>
#include <memory>
#include "CreatureAnimator.h"
#include "palette.h"
struct Scene {
 std::vector<uint16_t> fb;EyeRenderer r;Eyes eyes;CreatureAnimator c;
 uint32_t now=0;int group,id;bool switched=false;
 Scene(int g,int n):fb(SCREEN_WIDTH*SCREEN_HEIGHT),group(g),id(n) {
  previewRandomState()=0x9E3779B9u;
  r.begin(fb.data(),nullptr);eyes.begin(&r,1234,0);c.begin(&eyes,1234,0);
  eyes.setIdle(false);eyes.setAutoBlink(false);c.setIdleActsAllowed(false);
  c.startWake(now,false);run(5000);
  if(group==0) {eyes.setExpression(NEUTRAL);return;}
  if(group==1) {
   Mood m=Mood(id);
   if(m==Mood::SLEEPY)c.startSleep(now,0);
   else if(m==Mood::WAKE_UP){c.startSleep(now,0);run(5600);c.startWake(now,false);}
   else if(m==Mood::BATTERY){c.setBatteryLevel(73,true);run(2200);c.showBattery(73,now);}
   else if(m==Mood::ALWAYS_ON)c.setAlwaysOn(true,now);
   else if(m==Mood::AUTO_SLEEP){c.setAlwaysOn(true,now);run(2000);c.setAlwaysOn(false,now);}
   else if(m!=Mood::IDLE) {
    if(m==Mood::UPSIDE_DOWN)c.setUpsideDown(true,now);
    if(m==Mood::FOLLOWING)c.setPointerHeld(true,now);
    uint32_t duration=m==Mood::DIZZY?DIZZY_ANIM_MS:m==Mood::BOOP?900:m==Mood::WINK?760:
      m==Mood::STRETCH?1700:m==Mood::PEEK?1450:m==Mood::SNEEZE?1250:m==Mood::SASSY?1500:
      m==Mood::PLAYFUL || m==Mood::GIGGLE?1600:m==Mood::UPSIDE_DOWN || m==Mood::FOLLOWING?6200:2400;
    c.react(m,now,duration);
   } else eyes.setIdle(true);
  }
  if(group==3 && id>=7 && id<=10) {
   c.startSleep(now,.3f);run(SLEEP_SEQUENCE_MS+1000);
   int wanted=id-7;uint32_t chosen=1;
   for(uint32_t seed=1;seed<100000;++seed) {
    previewRandomState()=seed;float v=anim::frand();int kind=v<.3f?0:v<.6f?1:v<.82f?2:3;
    if(kind==wanted){chosen=seed;break;}
   }
   for(int ms=0;ms<30000;++ms) {
    previewRandomState()=chosen;run(20);
    if(eyes.openness(0)>.008f || eyes.openness(1)>.008f) break;
   }
  }
  if(group==3 && id==11){c.setPointerHeld(true,now);c.react(Mood::FOLLOWING,now,6200);}
  if(group==2) {
   if(id==9 || id==10){c.setBatteryLevel(73,true);run(3500);}
   if(id==10)c.showBattery(73,now);
   if(id>=12) {
    c.startSleep(now,0);run(5600);
    // Select each authored wake via the same RNG path as the device.
    int wanted=id-12;uint32_t chosen=1;
    for(uint32_t seed=1;seed<100000;++seed) {
     previewRandomState()=seed;float v=anim::frand();
     int variant=v<.45f?0:v<.65f?1:v<.8f?2:3;
     if(variant==wanted){chosen=seed;break;}
    }
    previewRandomState()=chosen;c.startWake(now,false);
   }
  }
 }
 void run(uint32_t ms){for(uint32_t k=0;k<ms;k+=20){now+=20;c.update(.020f,now);}}
 void tick(uint32_t elapsed) {
  if(group==0) {
   eyes.setExpression(elapsed<400?NEUTRAL:elapsed<4700?Expression(id):NEUTRAL);
   if(elapsed==2400)eyes.blink();
   now+=80;eyes.update(.080f,now);
   return;
  }
  if(group==3) {
   if(id<7 && (elapsed==800 || elapsed==3200)) eyes.blink(Blinker::Type(id));
   if(id==11 && elapsed>=2400 && !switched){c.setPointerHeld(false,now);c.huff(.7f,now);switched=true;}
  }
  if(group==1 && id==int(Mood::FOLLOWING))c.setPointer(.65f*sinf(elapsed*.002f),.4f*cosf(elapsed*.002f),now);
  if(group==1 && id==int(Mood::SWIPING)){c.setSwipe(1,0);c.setPointer(.9f,0,now);}
  if(group==2) {
   if(id==0 && elapsed==800)eyes.blink();
   if(id==1 && (elapsed==800 || elapsed==1040))eyes.blink();
   if(id==2 && elapsed==800)eyes.wink(0);
   if(id==3 && elapsed==800)eyes.wink(1);
   if(id==4){float a=elapsed*.00105f;c.setPointer(.9f*cosf(a),.8f*sinf(a),now);}
   if(id==5){c.setTilt(.8f*sinf(elapsed*.0014f),.7f*cosf(elapsed*.0014f));c.setSwing(.2f*sinf(elapsed*.003f),0);}
   if(id==6 || id==7)c.setCarried(true,id==7,elapsed%480==0,3);
   if(id==8)c.shake(.6f*(.5f+.5f*sinf(elapsed*.025f)));
   if(id==9 && elapsed==1440)c.setBatteryLevel(73,false);
   if(id==10 && elapsed==1440)c.setBatteryLevel(73,false);
   if(id==10 && elapsed==2720)c.setBatteryLevel(73,true);
   if(id==11)c.setBatteryLevel(elapsed<2000?50:elapsed<4000?10:3,false);
  }
  run(80);
 }
 void paint(std::vector<unsigned char>&rgb,int ox,int oy,int stride,int tile) {
  eyes.draw();
  for(int y=0;y<tile;++y)for(int x=0;x<tile;++x) {
   float sx=(x+.5f)*SCREEN_WIDTH/tile-.5f,sy=(y+.5f)*SCREEN_HEIGHT/tile-.5f;
   int ix=int(sx),iy=int(sy);float fx=sx-ix,fy=sy-iy;
   if(ix<0)ix=0;if(iy<0)iy=0;
   for(int channel=0;channel<3;++channel) {
    float value=0;
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
     int px=ix+dx,py=iy+dy;if(px>=SCREEN_WIDTH)px=SCREEN_WIDTH-1;if(py>=SCREEN_HEIGHT)py=SCREEN_HEIGHT-1;
     uint16_t v=fb[py*SCREEN_WIDTH+px];v=uint16_t((v<<8)|(v>>8));
     int n=channel==0?((v>>11)&31):channel==1?((v>>5)&63):(v&31),max=channel==1?63:31;
     value+=n*(255.0f/max)*(dx?fx:1-fx)*(dy?fy:1-fy);
    }
    rgb[((oy+y)*stride+ox+x)*3+channel]=static_cast<unsigned char>(value+.5f);
   }
  }
 }
};
const char* moods[]={"Idle / glances","Happy","Sad","Angry / huff","Dizzy / recovery","Falling asleep","Surprised","Anxious","Wake up","Battery reservoir","Boop","Petted / rocking","Swipe pull","Finger / hold anger","Confused","Shy","Cold shiver","Love hearts","Upside down","Curious","Yawn","Look around","Wink","Playful wiggle","Stretch","Peek","Giggle","Sneeze","Sassy eye-roll","Always-on ON","Always-on OFF"};
const char* effects[]={"Blink","Double blink","Left wink","Right wink","Gaze / saccades","Tilt / swing recoil","Walking / steps","Running / steps","Shake rattle","Charging / unplug","Liquid / cable change","Battery fatigue","Wake: classic","Wake: one eye","Wake: startled","Wake: sleepy"};
const char* variants[]={"Normal blink","Fast blink","Long blink","Half blink","Slow blink","Squeeze blink","Uneven blink","Nap: left peek","Nap: right peek","Nap: flutter","Nap: both peek","Release huff / sulk"};
int main(int argc,char**argv) {
 if(argc<2)return 1;
 const int tile=144,cols=4,rows=4,cellH=172,width=cols*tile,height=rows*cellH;
 for(int page=0;page<5;++page) {
  int group=page==0?0:page==3?2:page==4?3:1,start=page==2?16:0,count=group==0?15:group==2?16:group==3?12:page==1?16:15;
  std::vector<std::unique_ptr<Scene>> scenes;
  char path[1024];snprintf(path,sizeof(path),"%s/page%d.tsv",argv[1],page);FILE *meta=fopen(path,"w");
  for(int i=0;i<count;++i){int id=start+i;scenes.emplace_back(new Scene(group,id));fprintf(meta,"%d\t%s\n",i,group==0?expressionName(Expression(id)):group==1?moods[id]:group==2?effects[id]:variants[id]);}
  fclose(meta);
  for(int frame=0;frame<75;++frame) {
   std::vector<unsigned char> rgb(width*height*3,0);
   for(int i=0;i<count;++i) {scenes[i]->tick(frame*80);scenes[i]->paint(rgb,i%cols*tile,i/cols*cellH,width,tile);}
   snprintf(path,sizeof(path),"%s/page%d_%03d.ppm",argv[1],page,frame);FILE *out=fopen(path,"wb");if(!out){perror(path);return 2;}
   fprintf(out,"P6\n%d %d\n255\n",width,height);fwrite(rgb.data(),1,rgb.size(),out);fclose(out);
  }
 }
 return 0;
}
