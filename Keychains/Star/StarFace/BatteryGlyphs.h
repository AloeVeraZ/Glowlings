#pragma once
#include <math.h>
#include <stdint.h>

// Rounded monoline figures on a common 26 x 40 design grid. Rasterized once,
// then bilinearly sampled at the same sub-pixel scale as the battery outline.
// This avoids enlarging a pixel font and keeps the charging redraw inexpensive.
namespace BatteryGlyphs {
static constexpr int WIDTH=26, HEIGHT=40, COUNT=12;
static uint8_t coverage[COUNT][WIDTH*HEIGHT];
static bool ready=false;
static float px,py;
static int glyph;
static void move(float x,float y) { px=x;py=y; }
static void line(float x,float y) {
  float dx=x-px,dy=y-py,inv=1/(dx*dx+dy*dy+.00001f);
  int x0=int(floorf(fminf(px,x)-3)),x1=int(ceilf(fmaxf(px,x)+3));
  int y0=int(floorf(fminf(py,y)-3)),y1=int(ceilf(fmaxf(py,y)+3));
  for(int iy=y0;iy<=y1;++iy) for(int ix=x0;ix<=x1;++ix) {
    if(ix<0 || ix>=WIDTH || iy<0 || iy>=HEIGHT) continue;
    float t=fminf(1,fmaxf(0,((ix-px)*dx+(iy-py)*dy)*inv));
    float ax=ix-px-t*dx,ay=iy-py-t*dy;
    float a=fminf(1,fmaxf(0,3.0f-sqrtf(ax*ax+ay*ay)));
    uint8_t value=uint8_t(a*255+.5f),&pixel=coverage[glyph][iy*WIDTH+ix];
    if(value>pixel) pixel=value;
  }
  px=x;py=y;
}
static void curve(float cx,float cy,float x,float y) {
  float x0=px,y0=py;
  for(int i=1;i<=10;++i) {float t=i*.1f,u=1-t;line(u*u*x0+2*u*t*cx+t*t*x,u*u*y0+2*u*t*cy+t*t*y);}
}
static void build() {
  if(ready) return;
  glyph=0;move(13,4);curve(5,4,5,20);curve(5,35,13,35);curve(21,35,21,20);curve(21,4,13,4);
  glyph=1;move(7,10);line(14,4);line(14,35);move(7,35);line(21,35);
  glyph=2;move(5,10);curve(6,4,13,4);curve(21,4,21,11);curve(21,16,13,23);line(5,35);line(21,35);
  glyph=3;move(5,6);curve(21,0,21,12);curve(21,19,12,19);curve(22,19,21,28);curve(20,39,5,33);
  glyph=4;move(18,35);line(18,4);line(4,25);line(23,25);
  glyph=5;move(21,4);line(6,4);line(5,19);curve(23,14,21,28);curve(20,39,5,33);
  glyph=6;move(20,5);curve(4,0,5,23);curve(5,35,13,35);curve(22,35,21,27);curve(21,18,13,18);curve(5,18,5,26);
  glyph=7;move(5,4);line(22,4);line(10,35);
  glyph=8;move(13,19);curve(5,18,5,11);curve(5,4,13,4);curve(21,4,21,11);curve(21,18,13,19);curve(4,20,5,28);curve(5,35,13,35);curve(21,35,21,28);curve(22,20,13,19);
  glyph=9;move(6,34);curve(22,39,21,16);curve(21,4,13,4);curve(4,4,5,12);curve(5,21,13,21);curve(21,21,21,13);
  glyph=10;move(6,20);line(21,20);
  glyph=11;move(5,35);line(21,4);
  move(8,6);curve(4,6,4,10);curve(4,14,8,14);curve(12,14,12,10);curve(12,6,8,6);
  move(18,25);curve(14,25,14,29);curve(14,33,18,33);curve(22,33,22,29);curve(22,25,18,25);
  ready=true;
}
static float sample(int index,float x,float y) {
  int ix=int(floorf(x)),iy=int(floorf(y));
  if(ix<0 || ix>=WIDTH-1 || iy<0 || iy>=HEIGHT-1) return 0;
  float fx=x-ix,fy=y-iy;
  const uint8_t *p=coverage[index]+iy*WIDTH+ix;
  return ((p[0]*(1-fx)+p[1]*fx)*(1-fy)+(p[WIDTH]*(1-fx)+p[WIDTH+1]*fx)*fy)/255.0f;
}
}
