// Keep the per-pixel loop optimized for speed rather than code size.
#pragma GCC optimize("O3")
#include "EyeRenderer.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static constexpr int SW = SCREEN_WIDTH, SH = SCREEN_HEIGHT;

static constexpr float SPIRAL_TURNS = 2.4f;       // arms of the dizzy spiral across its radius
static constexpr float SPIRAL_HALF_WIDTH = 0.25f; // arm half width, in turns
static constexpr float TAU = 6.28318531f;
static constexpr float FAR = 1e6f;                // "nowhere near this edge"

// ---- Fast math for the per-pixel loop. Float division and sqrtf are slow
// library sequences on the ESP32, so the loop uses only these. ----
static inline float rsqrt(float x) {
  union { float f; uint32_t i; } u;
  u.f = x;
  u.i = 0x5f375a86u - (u.i >> 1);
  float y = u.f;
  y = y * (1.5f - .5f * x * y * y);
  return y * (1.5f - .5f * x * y * y);
}
static inline float fsqrt(float x) { return x > 1e-12f ? x * rsqrt(x) : 0.0f; }
static inline float fmax2(float a, float b) { return a > b ? a : b; }
static inline float fmin2(float a, float b) { return a < b ? a : b; }
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
// Edge coverage of a pixel from the signed distance at its centre.
static inline float cover(float d) {
  if (ANTI_ALIAS) return clamp01(.5f - d);
  return d < 0 ? 1.0f : 0.0f;
}
// atan2 to ~0.005 rad.
static inline float fastAtan2(float y, float x) {
  float ax = fabsf(x), ay = fabsf(y);
  float mx = fmax2(ax, ay), mn = fmin2(ax, ay);
  if (mx < 1e-6f) return 0.0f;
  float a = mn * rsqrt(mx * mx), s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0) r = 3.14159274f - r;
  return y < 0 ? -r : r;
}
// Smooth maximum: max(a, b) with the corner rounded over k px (k > 0).
static inline float smax(float a, float b, float k, float invK) {
  float h = fmax2(k - fabsf(a - b), 0.0f) * invK;
  return fmax2(a, b) + h * h * k * .25f;
}
// Heart, about one unit tall, point at y = 0 (Inigo Quilez's sdHeart).
static inline float sdHeart(float x, float y) {
  x = fabsf(x);
  if (y + x > 1.0f) {
    float ax = x - .25f, ay = y - .75f;
    return fsqrt(ax * ax + ay * ay) - .353553391f;
  }
  float ax = x, ay = y - 1.0f;
  float m = .5f * fmax2(x + y, 0.0f);
  float bx = x - m, by = y - m;
  float da = ax * ax + ay * ay, db = bx * bx + by * by;
  float d = fsqrt(da < db ? da : db);
  return x - y < 0 ? -d : d;
}

struct RGB { float r, g, b; };
static constexpr RGB rgbOf(uint32_t c) {
  return {float((c >> 16) & 255), float((c >> 8) & 255), float(c & 255)};
}
static inline void unpack565(uint16_t v, float &r, float &g, float &b) {
  uint16_t c = uint16_t((v << 8) | (v >> 8));
  r = ((c >> 11) & 31) * (255.0f / 31.0f);
  g = ((c >> 5) & 63) * (255.0f / 63.0f);
  b = (c & 31) * (255.0f / 31.0f);
}

// The glow around the eyes: how much of the eye colour shows d px outside
// the outline (a bright rim fading smoothly to nothing at GLOW_PX).
static constexpr float GLOW_PX = GLOW_SIZE * SCREEN_MIN_SIDE;
static float glowLut[65];
static void buildGlow() {
  for (int i = 0; i <= 64; ++i) {
    float d = i * (GLOW_PX / 64.0f);
    float fade = 1.0f - clamp01(d / GLOW_PX);
    float rim = expf(-d / (GLOW_PX * .18f));
    float bloom = expf(-d*d / (GLOW_PX*GLOW_PX*.30f));
    glowLut[i] = GLOW_STRENGTH * (.52f*rim + .48f*bloom) * fade * fade;
  }
}
static inline float glowAt(float d) {
  if (!GLOW || d >= GLOW_PX) return 0;
  return glowLut[int(fmax2(0.0f, d) * (64.0f / GLOW_PX))];
}
static constexpr float BUBBLE_MERGE_PX = 7.0f;          // gooey neck where a charging bubble joins the eye
static constexpr float BUBBLE_GLOW_PX = GLOW_PX / 3.0f; // bubbles draw glowAt(d * 3)

static inline uint16_t pack565(float r, float g, float b) {
  int ri = int(r * (31.0f / 255.0f) + .5f), gi = int(g * (63.0f / 255.0f) + .5f), bi = int(b * (31.0f / 255.0f) + .5f);
  ri = ri > 31 ? 31 : (ri < 0 ? 0 : ri);
  gi = gi > 63 ? 63 : (gi < 0 ? 0 : gi);
  bi = bi > 31 ? 31 : (bi < 0 ? 0 : bi);
  uint16_t c = uint16_t((ri << 11) | (gi << 5) | bi);
  return uint16_t((c << 8) | (c >> 8));
}

void EyeRenderer::buildEyeShades() {
  const RGB color = rgbOf(eyeColor);
  for (int i = 0; i < 256; ++i) {
    float alpha = i / 255.0f;
    eyeShades[i] = pack565(color.r * alpha, color.g * alpha, color.b * alpha);
  }
}

void EyeRenderer::setEyeColor(uint32_t rgb) {
  if (eyeColor == rgb) return;
  eyeColor = rgb;
  buildEyeShades();
}

static float roundRect(float x, float y, float hx, float hy, float radius) {
  float qx = fabsf(x) - hx + radius, qy = fabsf(y) - hy + radius;
  float ox = fmax2(qx, 0), oy = fmax2(qy, 0);
  return fsqrt(ox * ox + oy * oy) + fmin2(fmax2(qx, qy), 0) - radius;
}

static float smoothRange(float a, float b, float v) {
  float t = clamp01((v - a) / (b - a));
  return t * t * (3 - 2 * t);
}

static void overlayPixel(uint16_t *fb, int x, int y, RGB color, float alpha) {
  if (x < 0 || x >= SW || y < 0 || y >= SH || alpha <= 0) return;
  alpha = clamp01(alpha);
  float r, g, b;
  unpack565(fb[y * SW + x], r, g, b);
  fb[y * SW + x] = pack565(r + (color.r - r) * alpha,
                         g + (color.g - g) * alpha, b + (color.b - b) * alpha);
}

void EyeRenderer::setBatteryDisplay(int percent, float morph, float swirl, bool charging, float phase, uint32_t displayColor) {
  morph = clamp01(morph);
  uint32_t color = displayColor == UINT32_MAX ? eyeColor : displayColor;
  batteryPercent = percent;
  batteryMorph = morph;
  batterySwirl = swirl;
  batteryCharging = charging;
  batteryColor = color;
  batteryChargePhase = clamp01(phase);
}

// Horizontal span of a rounded rectangle; corner roots are calculated once
// per row rather than twice for every battery pixel.
static float roundedSpan(float y,float hx,float hy,float r) {
  float a=fabsf(y);if(a>hy+.5f) return -FAR;
  float corner=fmax2(0,a-hy+r);
  return hx-r+fsqrt(fmax2(0,r*r-corner*corner));
}

// The readout fills most of the round panel; its cap stays inside the edge.
static constexpr float BATTERY_ICON_SCALE = 1.25f;

void EyeRenderer::renderBattery(float opacity) {
  if (opacity <= 0) return;
  const float scale = SCREEN_MIN_SIDE / 240.0f * BATTERY_ICON_SCALE * (.975f + .025f * opacity);
  const float cx = SCREEN_CX - 2.5f * scale, cy = SCREEN_CY-10*scale;
  const RGB color = rgbOf(getBatteryColor());
  const int x0 = int(cx - 85 * scale), x1 = int(cx + 95 * scale);
  const int y0 = int(cy - 40 * scale), y1 = int(cy + 40 * scale);
  const float filledTo = cx - 77 * scale + 154 * scale * (batteryPercent < 0 ? 0 : batteryPercent / 100.0f);
  uint16_t shades[256];
  for(int i=0;i<256;++i) {float a=i/255.0f;shades[i]=pack565(color.r*a,color.g*a,color.b*a);}
  int animation=int(batteryChargePhase*16);if(animation>15) animation=15;
  float phase=animation/16.0f;
  for (int y = y0; y <= y1; ++y) {
    float outerSpan=roundedSpan(y-cy,82*scale,37*scale,10*scale);
    float innerSpan=roundedSpan(y-cy,77*scale,32*scale,6*scale);
    for (int x = x0; x <= x1; ++x) {
      float outer=cover(fabsf(x-cx)-outerSpan)*cover(fabsf(y-cy)-37*scale);
      float inner=cover(fabsf(x-cx)-innerSpan)*cover(fabsf(y-cy)-32*scale);
      float fill = batteryPercent >= 0 ? inner * cover(x - filledTo) : 0;
      if (batteryPercent == 0) fill = 0;
      float brightness = fmax2(0, outer - inner) + .12f * inner + .88f * fill;
      if (batteryCharging) {
        // A narrow highlight travels across the actual fill and empty track.
        float sweepX = cx + (-95 + 190 * phase) * scale;
        float sweep = clamp01(1 - fabsf(x - sweepX) / (16 * scale));
        brightness = fmin2(1, brightness + .55f * inner * sweep * sweep);
      }
      if(outer>0) {
        if(opacity*outer>=.999f) fb[y*SW+x]=shades[int(clamp01(brightness)*255+.5f)];
        else overlayPixel(fb,x,y,{color.r*brightness,color.g*brightness,color.b*brightness},opacity*outer);
      }
      float cap = cover(roundRect(x - (cx + 86.5f * scale), y - cy, 4.5f * scale, 10 * scale, 3 * scale));
      if (cap > 0) overlayPixel(fb, x, y, color, opacity * cap);
    }
  }

  if (batteryCharging) {
    // Pulsing lightning bolt below the bar, clear of the percentage text.
    const float points[6][2] = {{1,-11},{-7,2},{-1,2},{-3,11},{7,-3},{1,-3}};
    float pulse = .65f + .35f * (.5f - .5f * cosf(TAU * phase));
    for (int y = int(cy + 48 * scale); y <= int(cy + 77 * scale); ++y)
      for (int x = int(cx - 9 * scale); x <= int(cx + 9 * scale); ++x) {
        float px = (x - cx) / scale, py = (y - cy) / scale - 62;
        bool inside = false;
        for (int i = 0, j = 5; i < 6; j = i++)
          if ((points[i][1] > py) != (points[j][1] > py) &&
              px < (points[j][0] - points[i][0]) * (py - points[i][1]) /
                   (points[j][1] - points[i][1]) + points[i][0]) inside = !inside;
        if (inside) overlayPixel(fb, x, y, color, opacity * pulse);
      }
  }

  // Small rounded vector cells form clear digits; exact 0..100 percentages fit.
  static const uint8_t glyph[12][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}, {0,0,0,31,0,0,0}, {17,18,2,4,8,9,17}
  };
  char label[6];
  if (batteryPercent < 0) snprintf(label, sizeof(label), "--%%");
  else snprintf(label, sizeof(label), "%d%%", batteryPercent);
  const int chars = int(strlen(label));
  const float cell = 4.5f * scale, width = (chars * 6 - 1) * cell, height = 7 * cell;
  const float left = cx - width * .5f, top = cy - height * .5f;
  // A translucent dark backing keeps white text readable across the fill edge.
  for (int y = int(cy - 24 * scale); y <= int(cy + 24 * scale); ++y)
    for (int x = int(cx - width * .5f - 6 * scale); x <= int(cx + width * .5f + 6 * scale); ++x)
      overlayPixel(fb, x, y, {0,0,0}, .86f * opacity * cover(roundRect(x - cx, y - cy, width * .5f + 5 * scale, 23 * scale, 6 * scale)));
  for (int ch = 0; ch < chars; ++ch) {
    int index = label[ch] == '%' ? 11 : label[ch] == '-' ? 10 : label[ch] - '0';
    for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col) {
      if (!(glyph[index][row] & (1 << (4 - col)))) continue;
      float px = left + (ch * 6 + col + .5f) * cell, py = top + (row + .5f) * cell;
      for (int y = int(floorf(py - cell * .5f)); y <= int(ceilf(py + cell * .5f)); ++y)
        for (int x = int(floorf(px - cell * .5f)); x <= int(ceilf(px + cell * .5f)); ++x)
          overlayPixel(fb, x, y, {255,255,255}, opacity * cover(roundRect(x - px, y - py, cell * .5f, cell * .5f, .4f * scale)));
    }
  }
}

void EyeRenderer::begin(uint16_t *framebuffer, PushWindowFn pushFn) {
  fb = framebuffer;
  push = pushFn;
  fullRedraw = true;
  buildGlow();
  buildEyeShades();
}

EyeRenderer::Box EyeRenderer::bounds(const EyeFrame &e) {
  float c = cosf(e.tilt), s = sinf(e.tilt);
  float ry = e.ry + fabsf(e.bend);
  const float pad = 2.0f + (GLOW ? GLOW_PX : 0.0f);
  float ex = sqrtf(e.rx * e.rx * c * c + ry * ry * s * s) + pad;
  float ey = sqrtf(e.rx * e.rx * s * s + ry * ry * c * c) + pad;
  return {int(floorf(e.cx - ex)), int(floorf(e.cy - ey)), int(ceilf(e.cx + ex)), int(ceilf(e.cy + ey))};
}

void EyeRenderer::renderEye(const EyeFrame &e, const Box &clip, const Bubble *bubble) {
  Box b = bubble ? clip : bounds(e);
  int x0 = b.x0 > clip.x0 ? b.x0 : clip.x0, x1 = b.x1 < clip.x1 ? b.x1 : clip.x1;
  int y0 = b.y0 > clip.y0 ? b.y0 : clip.y0, y1 = b.y1 < clip.y1 ? b.y1 : clip.y1;
  if (x1 < x0 || y1 < y0 || e.rx < 1.0f || e.ry < 1.0f) return;

  const RGB EYE = rgbOf(eyeColor);
  static constexpr RGB PUP = rgbOf(PUPIL_COLOR);
  const float rx = e.rx, ry = e.ry;
  const float aperture = (fmin2(e.botA,ry)-fmax2(e.topA,-ry))/(ry*1.3f);
  const float glowGain = 1-.09f*faceCharge*smoothRange(.08f,.72f,aperture)*(.5f+.5f*sinf(faceChargePhase*TAU));
  const float iax = 1.0f / rx, iay = 1.0f / ry, iax2 = iax * iax, iay2 = iay * iay;
  // Near the oval's edge the distance is computed exactly; well inside or
  // outside it is not needed.
  const float glowPx = GLOW ? GLOW_PX : 0.0f;
  const float band = 1.6f / fmin2(rx, ry), bandOut = (1.6f + glowPx) / fmin2(rx, ry);
  const float qIn = (1.0f - band) * (1.0f - band), qOut = (1.0f + bandOut) * (1.0f + bandOut);
  const float cs = cosf(e.tilt), sn = sinf(e.tilt);
  const float mirror = e.rightEye ? -1.0f : 1.0f;  // x toward the nose is +
  const float bendK = e.bend * iax2;
  const float k = fmax2(.5f, e.lidRound), invK = 1.0f / k;
  // A lid farther than this (unnormalised) cannot affect a pixel.
  const float slopeT = fabsf(e.topB) + 2.0f * fabsf(e.topC) * rx, slopeB = fabsf(e.botB) + 2.0f * fabsf(e.botC) * rx;
  const float reachT = (k + 1.5f + glowPx) * fsqrt(1.0f + slopeT * slopeT);
  const float reachB = (k + 1.5f + glowPx) * fsqrt(1.0f + slopeB * slopeB);
  const bool flatTop = fabsf(e.topC)<1e-7f, flatBottom = fabsf(e.botC)<1e-7f;
  const float topNormal = rsqrt(1+e.topB*e.topB), bottomNormal = rsqrt(1+e.botB*e.botB);

  // Pupil (and its heart / spiral morphs) and catch-lights.
  const float pr = e.pupilR;
  const bool pupil = pr > .4f;
  const float heart = clamp01(e.heart), spiral = clamp01(e.spiral);
  const float isx = 1.0f / fmax2(.05f, e.pupilSX), isy = 1.0f / fmax2(.05f, e.pupilSY);
  const float pScale = fmin2(e.pupilSX, e.pupilSY);
  const float heartS = pr * 1.35f, invHeart = 1.0f / fmax2(.01f, heartS);
  const float spiralR = pr * 1.06f, invSpiralR = 1.0f / fmax2(.01f, spiralR);
  const float spiralPx = spiralR / SPIRAL_TURNS;
  const float spiralShift = e.spiralPhase * (1.0f / TAU);
  const float reach = (spiral > 0 ? spiralR : heart > 0 ? heartS : pr) * fmax2(e.pupilSX, e.pupilSY) + 2.0f;
  const float pcx = e.cx + e.pupilX, pcy = e.cy + e.pupilY;
  const float coreR = e.coreR;
  const float g1x = e.cx + e.glintX, g1y = e.cy + e.glintY, g1r = e.glintR;
  const float g2x = e.cx + e.glint2X, g2y = e.cy + e.glint2Y, g2r = e.glint2R;
  const uint16_t bodyPacked = eyeShade(.84f);

  for (int y = y0; y <= y1; ++y) {
    int rowX0=x0,rowX1=x1;
    if (SCREEN_IS_ROUND) {
      float sy=y-SCREEN_CY, remain=SCREEN_RADIUS*SCREEN_RADIUS-sy*sy;
      if(remain<0) continue;
      float half=fsqrt(remain);
      int left=int(ceilf(SCREEN_CX-half)),right=int(floorf(SCREEN_CX+half));
      if(left>rowX0) rowX0=left;
      if(right<rowX1) rowX1=right;
    }
    uint16_t *line = fb + y * SW;
    const float dy = y - e.cy;
    const float uy = y - pcy;
    const bool pupilRow = pupil && fabsf(uy) < reach;
    for (int x = rowX0; x <= rowX1; ++x) {
      const float dxm = (x - e.cx) * mirror;
      // Into the eye's frame: rotate, then undo the bend.
      const float xi = dxm * cs + dy * sn;
      const float xx = xi * xi;
      const float ly = dy * cs - dxm * sn - bendK * xx;
      // The oval.
      const float q = xx * iax2 + ly * ly * iay2;
      if (q >= qOut && !bubble) continue;              // outside: stays black
      float d;
      if (q >= qOut) {
        d = FAR;
      } else if (q <= qIn) {
        d = -FAR;
      } else {                                            // near the edge: distance to the ellipse
        float gg = xx * iax2 * iax2 + ly * ly * iay2 * iay2;
        d = .5f*(q-1.0f)*rsqrt(gg);
      }
      // Lids: curved lines, distance along their normals (only when close).
      float uT = e.topA + e.topB * xi + e.topC * xx - ly;
      float uB = ly - (e.botA + e.botB * xi + e.botC * xx);
      if (uT > -reachT) {
        float s1 = e.topB + 2.0f * e.topC * xi;
        d = smax(d, uT * (flatTop ? topNormal : rsqrt(1.0f + s1 * s1)), k, invK);
      }
      if (uB > -reachB) {
        float s2 = e.botB + 2.0f * e.botC * xi;
        d = smax(d, uB * (flatBottom ? bottomNormal : rsqrt(1.0f + s2 * s2)), k, invK);
      }
      const float eyeDistance = d;
      if (bubble) {
        float bx=x-bubble->x,by=y-bubble->y;
        float bubbleDistance=fsqrt(bx*bx+by*by)-bubble->radius;
        float merge=BUBBLE_MERGE_PX*SCREEN_MIN_SIDE/240.0f;
        d=-smax(-d,-bubbleDistance,merge,1/merge);
      }
      if (d >= .5f) {
        // Outside: the glow. (Brighter wins, so the two eyes' glows and an
        // eye under the other's glow never darken each other.)
        // Compact bubble halos end inside their tiny patch; the original eye
        // halo stays continuous across it. No whole-eye particle calculations.
        float gl = bubble ? fmax2(glowAt(eyeDistance),glowAt(d*3))*glowGain : glowAt(d)*glowGain;
        if (gl > .004f) {
          if (PUPIL_COLOR == 0 && !CHEEK_BLUSH) {
            // Both eyes use the same monotonic shade ramp; brighter wins.
            uint16_t shade = eyeShade(gl), old = line[x];
            if (uint16_t((shade << 8) | (shade >> 8)) > uint16_t((old << 8) | (old >> 8))) line[x] = shade;
          } else {
            float r0, g0, b0;
            unpack565(line[x], r0, g0, b0);
            line[x] = pack565(fmax2(r0, EYE.r * gl), fmax2(g0, EYE.g * gl), fmax2(b0, EYE.b * gl));
          }
        }
        continue;
      }
      const float aEye = cover(d);
      // A slightly translucent-looking body with a bright, narrow boundary.
      // This reuses the existing distance and shade table: no extra geometry,
      // image layers, or changes to the solid black iris.
      const float surface = d <= -1.6f ? .84f : .84f+.16f*smoothRange(-1.6f,-.35f,d);
      const uint16_t surfacePacked = d <= -1.6f ? bodyPacked : eyeShade(surface);
      // An edge pixel shows the glow behind the part the eye doesn't cover.
      const float rim = aEye < 1.0f ? glowAt(0) * glowGain * (1.0f - aEye) : 0.0f;

      float ux = x - pcx;
      if (!pupilRow || fabsf(ux) >= reach) {
        if (aEye >= 1.0f) { line[x] = surfacePacked; continue; }
        line[x] = eyeShade(surface*aEye + rim);
        continue;
      }
      // Pupil: a circle seen on a round eyeball (narrower toward the sides).
      float sx = ux * isx, sy = uy * isy;
      float dP = (fsqrt(sx * sx + sy * sy) - pr) * pScale;
      if (heart > 0) {
        float dH = sdHeart(ux * invHeart, -uy * invHeart + .55f) * heartS;
        dP += (dH - dP) * heart;
      }
      if (spiral > 0) {
        float len = fsqrt(ux * ux + uy * uy);
        float vv = len * invSpiralR * SPIRAL_TURNS - fastAtan2(uy, ux) * (1.0f / TAU) - spiralShift;
        float fr = vv - float(int(vv));
        if (fr < 0) fr += 1.0f;
        float dS = fmax2((fabsf(fr - .5f) - SPIRAL_HALF_WIDTH) * spiralPx, len - spiralR);
        dP += (dS - dP) * spiral;
      }
      float aP = cover(dP);
      // The iris's black centre (the background colour: depth without a new colour).
      float aC = 0;
      if (coreR > .4f && aP > 0) aC = cover((fsqrt(sx * sx + sy * sy) - coreR) * pScale);
      // Catch-lights, in the eye colour, only show on the pupil.
      if (aP > 0 && g1r > .3f) {
        float gx = x - g1x, gy = y - g1y;
        float aG = cover(fsqrt(gx * gx + gy * gy) - g1r);
        if (g2r > .3f) {
          float hx = x - g2x, hy = y - g2y;
          aG = fmax2(aG, cover(fsqrt(hx * hx + hy * hy) - g2r));
        }
        aP *= 1.0f - aG;
        aC *= 1.0f - aG;
      }
      if (aP <= 0 && aEye >= 1.0f) { line[x] = surfacePacked; continue; }
      if (aC >= 1.0f) { line[x] = PIXEL_BACKGROUND; continue; }
      if (aP >= 1.0f && aC <= 0 && aEye >= 1.0f) { line[x] = PIXEL_PUPIL; continue; }
      const float keep = aEye * (1.0f - aC);
      if (PUPIL_COLOR == 0) {
        line[x] = eyeShade(surface*(1.0f - aP) * keep + rim);
        continue;
      }
      float r = EYE.r*surface + (PUP.r - EYE.r*surface) * aP;
      float g = EYE.g*surface + (PUP.g - EYE.g*surface) * aP;
      float bl = EYE.b*surface + (PUP.b - EYE.b*surface) * aP;
      line[x] = pack565(r * keep + EYE.r * rim, g * keep + EYE.g * rim, bl * keep + EYE.b * rim);
    }
  }
}

EyeRenderer::Box EyeRenderer::cheekBounds(const EyeFrame &e) {
  if (!CHEEK_BLUSH || e.cheekRX < .5f || e.cheekAlpha <= 0) return {0, 0, -1, -1};
  float x = e.cx + e.cheekX, y = e.cy + e.cheekY;
  return {int(floorf(x - e.cheekRX - 2)), int(floorf(y - e.cheekRY - 2)),
          int(ceilf(x + e.cheekRX + 2)), int(ceilf(y + e.cheekRY + 2))};
}

// Rosy cheeks: soft-edged ovals under the eyes, drawn before the eyes.
void EyeRenderer::renderCheek(const EyeFrame &e, const Box &clip) {
  Box b = cheekBounds(e);
  int x0 = b.x0 > clip.x0 ? b.x0 : clip.x0, x1 = b.x1 < clip.x1 ? b.x1 : clip.x1;
  int y0 = b.y0 > clip.y0 ? b.y0 : clip.y0, y1 = b.y1 < clip.y1 ? b.y1 : clip.y1;
  if (x1 < x0 || y1 < y0) return;
  static constexpr RGB CHEEK = rgbOf(CHEEK_COLOR);
  const float cx = e.cx + e.cheekX, cy = e.cy + e.cheekY;
  const float iax = 1.0f / e.cheekRX, iay = 1.0f / e.cheekRY, iax2 = iax * iax, iay2 = iay * iay;
  const float alpha = clamp01(e.cheekAlpha);
  for (int y = y0; y <= y1; ++y) {
    uint16_t *line = fb + y * SW;
    const float ly = y - cy;
    for (int x = x0; x <= x1; ++x) {
      const float lx = x - cx;
      const float q = lx * lx * iax2 + ly * ly * iay2;
      if (q >= 1.3f) continue;
      float sq = fsqrt(q), gg = lx * lx * iax2 * iax2 + ly * ly * iay2 * iay2;
      float d = gg > 1e-9f ? (sq - 1.0f) * sq * rsqrt(gg) : -FAR;
      float a = cover(d) * alpha;
      if (a <= 0) continue;
      line[x] = a >= 1.0f ? PIXEL_CHEEK : pack565(CHEEK.r * a, CHEEK.g * a, CHEEK.b * a);
    }
  }
}

static inline void grow(int &x0, int &y0, int &x1, int &y1, int bx0, int by0, int bx1, int by1) {
  if (bx1 < bx0) return;
  x0 = bx0 < x0 ? bx0 : x0; y0 = by0 < y0 ? by0 : y0;
  x1 = bx1 > x1 ? bx1 : x1; y1 = by1 > y1 ? by1 : y1;
}

// Repeatable 0..1 noise: each bubble gets new size, speed and sway every trip.
static inline float bubbleNoise(uint32_t n) {
  n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; n ^= n >> 16;
  return (n & 0xFFFFFF) * (1.0f / 16777216.0f);
}

// Bubbles leave the USB-C port at the bottom edge, wander up on their own
// paths and pour into a point along the eye's lower edge. While far from the
// eye they are plain circles; only the last stretch pays for the full
// eye-distance blend that makes them melt into the outline.
int EyeRenderer::chargingBubbles(const EyeFrame &e,Bubble bubbles[CHARGE_BUBBLES]) const {
  if(faceCharge<=.001f || e.ry<1 || e.rx<1) return 0;
  float aperture=(fmin2(e.botA,e.ry)-fmax2(e.topA,-e.ry))/(1.3f*e.ry);
  float amount=faceCharge*smoothRange(.08f,.72f,aperture);
  if(amount<=.001f) return 0;
  const float scale=SCREEN_MIN_SIDE/240.0f,side=e.rightEye?1.0f:-1.0f,mirror=e.rightEye?-1.0f:1.0f;
  const float cs=cosf(e.tilt),sn=sinf(e.tilt),bendK=e.bend/(e.rx*e.rx);
  const float portX=SCREEN_CX,portY=SCREEN_CY+SCREEN_RADIUS-8*scale;
  int count=0;
  for(int i=0;i<CHARGE_BUBBLES;++i) {
    uint32_t seed=uint32_t(i*977+(e.rightEye?131071:0));
    float speed=.8f+.45f*bubbleNoise(seed);
    float trip=faceChargePhase*speed+float(i)/CHARGE_BUBBLES+(e.rightEye?.5f/CHARGE_BUBBLES:0);
    float cycle=floorf(trip),t=trip-cycle;
    uint32_t key=seed*2654435761u+uint32_t(int32_t(cycle))*40503u;
    float size=.65f+.7f*bubbleNoise(key),across=bubbleNoise(key+1)*2-1;
    float sway=(3+6*bubbleNoise(key+2))*scale,swayTurns=1.2f+1.3f*bubbleNoise(key+3);
    float launch=(1.5f+5*bubbleNoise(key+4))*scale;
    float endR=(3.0f+3.2f*size)*scale;
    // Where along the lower edge this bubble lands, in the eye's own frame.
    float xi=across*.55f*e.rx,edge=e.ry*fsqrt(fmax2(0,1-xi*xi/(e.rx*e.rx)));
    float lid=e.botA+e.botB*xi+e.botC*xi*xi;
    float ly=fmin2(edge,lid);
    float lyIn=ly-.8f*endR+bendK*xi*xi,lyEdge=ly+bendK*xi*xi;
    float dxm=xi*cs-lyIn*sn,dy=xi*sn+lyIn*cs;
    float targetX=e.cx+dxm*mirror,targetY=e.cy+dy;
    float edgeX=e.cx+(xi*cs-lyEdge*sn)*mirror,edgeY=e.cy+xi*sn+lyEdge*cs;
    float u=1-t,startX=portX+side*launch;
    float bendX=startX+(targetX-startX)*.35f+side*14*scale,bendY=targetY+38*scale;
    float x=u*u*startX+2*u*t*bendX+t*t*targetX;
    float y=u*u*portY+2*u*t*bendY+t*t*targetY;
    x+=sway*sinf(TAU*(swayTurns*t+bubbleNoise(key+5)))*sinf(3.14159265f*t);
    float radius=(1.2f*scale+(endR-1.2f*scale)*t*fsqrt(t))*amount*smoothRange(0,.08f,t);
    if(radius<=.25f) continue;
    x=targetX+(x-targetX)*amount;y=targetY+(y-targetY)*amount;
    float ex=x-edgeX,ey=y-edgeY;
    float reach=radius+(BUBBLE_MERGE_PX+3)*scale;
    bool merge=ex*ex+ey*ey<reach*reach;
    float pad=merge?(BUBBLE_MERGE_PX*scale+BUBBLE_GLOW_PX+2):BUBBLE_GLOW_PX+1.5f;
    bubbles[count++]={x,y,radius,pad,merge};
  }
  return count;
}

EyeRenderer::Box EyeRenderer::bubbleBounds(const Bubble &b) {
  float r=b.radius+b.pad;
  return {int(floorf(b.x-r)),int(floorf(b.y-r)),int(ceilf(b.x+r)),int(ceilf(b.y+r))};
}

// Brighter wins, so overlapping glows and bubbles never darken each other.
void EyeRenderer::brighten(uint16_t &pixel, float alpha) const {
  if (alpha <= .004f) return;
  if (PUPIL_COLOR == 0 && !CHEEK_BLUSH) {
    uint16_t shade = eyeShade(alpha);
    if (uint16_t((shade << 8) | (shade >> 8)) > uint16_t((pixel << 8) | (pixel >> 8))) pixel = shade;
    return;
  }
  const RGB EYE = rgbOf(eyeColor);
  float r, g, b;
  unpack565(pixel, r, g, b);
  pixel = pack565(fmax2(r, EYE.r * alpha), fmax2(g, EYE.g * alpha), fmax2(b, EYE.b * alpha));
}

// A free-floating bubble and its small halo, no eye geometry. Bubbles of the
// same stream melt together where they touch instead of overlapping rims.
void EyeRenderer::renderBubble(const Bubble *stream, int count, int index, const Box &clip) {
  const Bubble &bubble = stream[index];
  const float merge = 4.0f * SCREEN_MIN_SIDE / 240.0f, invMerge = 1.0f / merge;
  Bubble near[CHARGE_BUBBLES];
  int nearCount = 0;
  for (int i = 0; i < count; ++i) {
    float dx = stream[i].x - bubble.x, dy = stream[i].y - bubble.y;
    float reach = stream[i].radius + bubble.radius + bubble.pad + merge;
    if (dx * dx + dy * dy < reach * reach) near[nearCount++] = stream[i];
  }
  Box b = bubbleBounds(bubble);
  int x0 = b.x0 > clip.x0 ? b.x0 : clip.x0, x1 = b.x1 < clip.x1 ? b.x1 : clip.x1;
  int y0 = b.y0 > clip.y0 ? b.y0 : clip.y0, y1 = b.y1 < clip.y1 ? b.y1 : clip.y1;
  const float rim = glowAt(0);
  for (int y = y0; y <= y1; ++y) {
    int rowX0 = x0, rowX1 = x1;
    if (SCREEN_IS_ROUND) {
      float sy = y - SCREEN_CY, remain = SCREEN_RADIUS * SCREEN_RADIUS - sy * sy;
      if (remain < 0) continue;
      float half = fsqrt(remain);
      int left = int(ceilf(SCREEN_CX - half)), right = int(floorf(SCREEN_CX + half));
      if (left > rowX0) rowX0 = left;
      if (right < rowX1) rowX1 = right;
    }
    uint16_t *line = fb + y * SW;
    for (int x = rowX0; x <= rowX1; ++x) {
      float d = FAR;
      for (int i = 0; i < nearCount; ++i) {
        const float bx = x - near[i].x, by = y - near[i].y;
        d = -smax(-d, -(fsqrt(bx * bx + by * by) - near[i].radius), merge, invMerge);
      }
      if (d >= .5f) { brighten(line[x], glowAt(d * 3)); continue; }
      const float a = cover(d);
      // Same body and bright rim as the eyes, so a bubble reads as a drop of eye.
      const float surface = .84f + .16f * smoothRange(-1.6f, -.35f, d);
      brighten(line[x], surface * a + rim * (1 - a));
    }
  }
}

void EyeRenderer::compose(const EyeFrame sourceEyes[2]) {
  if (!fb) return;
  EyeFrame warped[2] = {sourceEyes[0], sourceEyes[1]};
  const EyeFrame *eyes = warped;
  const float scale = SCREEN_MIN_SIDE / 240.0f;
  const float barAlpha = smoothRange(.35f, 1.0f, batteryMorph);
  int batteryFrame=batteryCharging?int(batteryChargePhase*16):0;
  if(batteryFrame>15) batteryFrame=15;
  if (barAlpha >= 1 && opaqueBatteryCached && !fullRedraw && cachedPercent == batteryPercent &&
      cachedCharging == batteryCharging && cachedBatteryFrame==batteryFrame && cachedBatteryColor==getBatteryColor()) {
    dirty = {0,0,-1,-1};
    return; // the static readout needs no repeated render or LCD transmission
  }
  opaqueBatteryCached = barAlpha >= 1;
  cachedPercent = batteryPercent; cachedCharging = batteryCharging;
  cachedBatteryFrame=batteryFrame;cachedBatteryColor=getBatteryColor();
  if (batteryMorph > 0) {
    float cs = cosf(batterySwirl), sn = sinf(batterySwirl);
    for (int i = 0; i < 2; ++i) {
      EyeFrame &f = warped[i];
      float dx = f.cx - SCREEN_CX, dy = f.cy - SCREEN_CY;
      f.cx = SCREEN_CX + (dx * cs - dy * sn) * (1 - batteryMorph) + (i ? 30 : -30) * scale * batteryMorph;
      f.cy = SCREEN_CY + (dx * sn + dy * cs) * (1 - batteryMorph);
      f.tilt += (i ? -1 : 1) * batterySwirl;
      float shrinkY = (f.ry * (1 - batteryMorph) + 22 * scale * batteryMorph) / f.ry;
      f.rx = f.rx * (1 - batteryMorph) + 46 * scale * batteryMorph;
      f.ry *= shrinkY;
      f.topA *= shrinkY; f.topC *= shrinkY; f.botA *= shrinkY; f.botC *= shrinkY;
      f.bend *= shrinkY;
      float iris = 1 - smoothRange(.05f, .7f, batteryMorph);
      f.pupilR *= iris; f.coreR *= iris; f.glintR *= iris; f.glint2R *= iris;
      f.cheekAlpha *= 1 - batteryMorph;
    }
  }
  Box a = bounds(eyes[0]), b = bounds(eyes[1]);
  Bubble bubbles[2][CHARGE_BUBBLES];
  int bubbleCount[2]={chargingBubbles(eyes[0],bubbles[0]),chargingBubbles(eyes[1],bubbles[1])};
  Box cur = {a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0,
             a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
  for (int i = 0; i < 2; ++i) {
    Box c = cheekBounds(eyes[i]);
    grow(cur.x0, cur.y0, cur.x1, cur.y1, c.x0, c.y0, c.x1, c.y1);
    for(int j=0;j<bubbleCount[i];++j) {
      Box p=bubbleBounds(bubbles[i][j]);grow(cur.x0,cur.y0,cur.x1,cur.y1,p.x0,p.y0,p.x1,p.y1);
    }
  }
  if (batteryMorph > 0) {
    const float icon = scale * BATTERY_ICON_SCALE; // renderBattery's geometry, plus a pixel
    grow(cur.x0, cur.y0, cur.x1, cur.y1,
         int(SCREEN_CX - 89 * icon), int(SCREEN_CY - 52 * icon),
         int(SCREEN_CX + 94 * icon), int(SCREEN_CY + (batteryCharging ? 69 : 32) * icon));
  }
  cur.x0 = cur.x0 < 0 ? 0 : cur.x0;
  cur.y0 = cur.y0 < 0 ? 0 : cur.y0;
  cur.x1 = cur.x1 > SW - 1 ? SW - 1 : cur.x1;
  cur.y1 = cur.y1 > SH - 1 ? SH - 1 : cur.y1;
  Box d = cur;
  if (fullRedraw) d = {0, 0, SW - 1, SH - 1};
  else if (prev.x1 >= prev.x0) {
    d.x0 = d.x0 < prev.x0 ? d.x0 : prev.x0; d.y0 = d.y0 < prev.y0 ? d.y0 : prev.y0;
    d.x1 = d.x1 > prev.x1 ? d.x1 : prev.x1; d.y1 = d.y1 > prev.y1 ? d.y1 : prev.y1;
  }
  dirty = d;
  if (d.x1 < d.x0 || d.y1 < d.y0) return;
  // Off-screen: clear the window to pure black, draw both eyes, push once.
  for (int y = d.y0; y <= d.y1; ++y)
    memset(fb + y * SW + d.x0, 0, (d.x1 - d.x0 + 1) * sizeof(uint16_t));
  if (barAlpha < 1) {
    renderCheek(eyes[0], d);
    renderCheek(eyes[1], d);
    renderEye(eyes[0], d);
    renderEye(eyes[1], d);
    // Joining bubbles redraw their patch of eye; free ones only brighten.
    for(int i=0;i<2;++i) for(int j=0;j<bubbleCount[i];++j) {
      const Bubble &bubble=bubbles[i][j];
      if(!bubble.merge) continue;
      Box p=bubbleBounds(bubble);
      if(p.x0<d.x0) p.x0=d.x0;if(p.x1>d.x1) p.x1=d.x1;
      if(p.y0<d.y0) p.y0=d.y0;if(p.y1>d.y1) p.y1=d.y1;
      renderEye(eyes[i],p,&bubble);
    }
    for(int i=0;i<2;++i) for(int j=0;j<bubbleCount[i];++j)
      if(!bubbles[i][j].merge) renderBubble(bubbles[i],bubbleCount[i],j,d);
  }
  if (barAlpha > 0 && barAlpha < 1) {
    // Fade the flattened eye shapes into the unified bar in the same buffer.
    for (int y = d.y0; y <= d.y1; ++y) for (int x = d.x0; x <= d.x1; ++x) {
      float r, g, b;
      unpack565(fb[y * SW + x], r, g, b);
      fb[y * SW + x] = pack565(r * (1 - barAlpha), g * (1 - barAlpha), b * (1 - barAlpha));
    }
  }
  if (barAlpha > 0) renderBattery(barAlpha);
  prev = cur;
  fullRedraw = false;
}

void EyeRenderer::draw(const EyeFrame eyes[2]) {
  compose(eyes);
  if (push && dirty.x1 >= dirty.x0 && dirty.y1 >= dirty.y0)
    push(dirty.x0, dirty.y0, dirty.x1 + 1, dirty.y1 + 1, fb);
}
