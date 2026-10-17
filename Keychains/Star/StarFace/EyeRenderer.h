#pragma once
#include <stdint.h>
#include "FaceConfig.h"

// Final geometry of one eye for one frame, in screen pixels. Everything is
// float, so shapes move and morph continuously in sub-pixel steps.
struct EyeFrame {
  float cx, cy;            // eye centre
  float rx, ry;            // the oval's half width / half height
  float tilt;              // rotation, radians: + lowers the inner (nose-side) end
  float bend;              // + both ends drop (a "^" arc), - they lift, px at the ends
  // Lids, in the eye's own frame (x toward the nose, y down, origin at the
  // centre): the upper lid hides everything above topA + topB*x + topC*x^2,
  // the lower lid everything below botA + botB*x + botC*x^2.
  float topA, topB, topC;
  float botA, botB, botC;
  float lidRound;          // px of rounding where a lid meets the outline
  float pupilX, pupilY;    // pupil centre relative to the eye centre, screen axes
  float pupilR;            // iris radius
  float coreR;             // black centre of the iris (0: none)
  float pupilSX, pupilSY;  // pupil scale: narrows when looking sideways (a round eyeball), squashes in a blink
  float glintX, glintY, glintR;    // main catch-light, relative to the eye centre (0 radius: none)
  float glint2X, glint2Y, glint2R; // small second catch-light
  float heart;             // 0..1 pupil -> heart
  float spiral;            // 0..1 pupil -> spiral
  float spiralPhase;       // spiral rotation, radians
  float cheekX, cheekY;    // blush under the eye: centre relative to the eye centre...
  float cheekRX, cheekRY;  // ...and half sizes (0: no blush)
  float cheekAlpha;        // fades the blush in and out
  bool rightEye;           // mirrors "toward the nose" for the right eye
};

// Sends the rectangle [x0, x1) x [y0, y1) of the framebuffer to the panel.
typedef void (*PushWindowFn)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t *fb);

// Byte-swapped RGB565 (the panel's SPI order) of a 24-bit colour.
constexpr uint16_t panel565(uint32_t rgb) {
  return uint16_t(((((rgb >> 19) & 0x1F) << 11) | (((rgb >> 10) & 0x3F) << 5) | ((rgb >> 3) & 0x1F)) << 8 |
                  ((((rgb >> 19) & 0x1F) << 11) | (((rgb >> 10) & 0x3F) << 5) | ((rgb >> 3) & 0x1F)) >> 8);
}
static constexpr uint16_t PIXEL_BACKGROUND = 0x0000;
static constexpr uint16_t PIXEL_EYE = panel565(EYE_COLOR);
static constexpr uint16_t PIXEL_PUPIL = panel565(PUPIL_COLOR);
// Always-on cue: little stars that fall from the pupil centre into its edge.
static constexpr int STARFALL_COUNT = 5;
static constexpr float STARFALL_LIGHT = .85f; // peak brightness, as a share of the eye colour
static constexpr uint16_t PIXEL_CHEEK = panel565(CHEEK_COLOR);

// Draws the eyes as smooth vector shapes on pure black. Each eye is an oval
// (tilted and bent as the expression asks) cut by two curved lids with rounded
// corners; the pupil is a circle that narrows when it looks to the side and
// can morph into a heart or a spinning spiral; two catch-lights sit on it.
// Every edge is anti-aliased from its exact signed distance: a pixel the edge
// passes through gets the matching blend of the two colours and black, so
// curves look continuous instead of stepped. Frames are composed off-screen
// and only the changed rectangle is pushed, so there is no flicker.
class EyeRenderer {
 public:
  void begin(uint16_t *framebuffer, PushWindowFn pushFn);
  void invalidate() { fullRedraw = true; }
  void setEyeColor(uint32_t rgb);
  uint32_t getEyeColor() const { return eyeColor; }
  void setBatteryDisplay(int percent, float morph, float swirl, bool charging = false, float phase = 0,
                         uint32_t displayColor = UINT32_MAX);
  uint32_t getBatteryColor() const { return batteryCharging ? BATTERY_CHARGING_COLOR : batteryColor; }
  bool batteryDisplayActive() const { return batteryMorph > 0; }
  void setFaceCharging(float amount, float phase, float stopPhase = -1) {
    faceCharge = amount; faceChargePhase = phase; faceChargeStop = stopPhase;
  }
  // clock: seconds, for the always-on starfall inside the pupils.
  void setAwakeMode(bool enabled, float cue, float clock = 0) { alwaysOn = enabled; modeCue = cue; sparkleClock = clock; }
  void draw(const EyeFrame eyes[2]);
  // Draws into the framebuffer without pushing (host previews, screenshots).
  void compose(const EyeFrame eyes[2]);

 private:
  struct Box { int x0, y0, x1, y1; };
  struct Bubble { float x, y, radius, pad; bool merge; };
  uint16_t *fb = nullptr;
  PushWindowFn push = nullptr;
  bool fullRedraw = true;
  uint32_t eyeColor = EYE_COLOR;
  uint16_t eyeShades[256] = {}; // cached black-to-eye shades for anti-aliasing and glow
  int batteryPercent = -1;
  float batteryMorph = 0, batterySwirl = 0;
  bool batteryCharging = false;
  uint32_t batteryColor = EYE_COLOR;
  float batteryChargePhase = 0;
  float faceCharge = 0, faceChargePhase = 0, faceChargeStop = -1;
  bool alwaysOn = false;
  float modeCue = 0, sparkleClock = 0;
  bool opaqueBatteryCached = false, cachedCharging = false;
  int cachedPercent = -2;
  int cachedBatteryFrame = -1;
  uint32_t cachedBatteryColor = 0;
  Box prev = {0, 0, -1, -1}, dirty = {0, 0, -1, -1};

  static Box bounds(const EyeFrame &e);
  static Box cheekBounds(const EyeFrame &e);
  void renderEye(const EyeFrame &e, const Box &clip, const Bubble *bubble = nullptr);
  int chargingBubbles(const EyeFrame &e, Bubble bubbles[CHARGE_BUBBLES]) const;
  static Box bubbleBounds(const Bubble &bubble);
  void renderBubble(const Bubble *stream, int count, int index, const Box &clip);
  void brighten(uint16_t &pixel, float alpha) const;
  void renderCheek(const EyeFrame &e, const Box &clip);
  void renderBattery(float opacity);
  void buildEyeShades();
  uint16_t eyeShade(float alpha) const {
    int index = int(alpha * 255 + .5f);
    return eyeShades[index < 0 ? 0 : index > 255 ? 255 : index];
  }
};
