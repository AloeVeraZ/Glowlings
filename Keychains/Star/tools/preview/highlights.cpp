// Real firmware frames for the newest behaviors, assembled into GIFs by
// highlights.py:
//   starfall   - always-on stars falling out of the pupils (with a 2x close-up)
//   double-tap - a tilted pose, a double tap that makes it the new zero, then
//                turns tracked from there (sensor fusion included)
//   battery    - 95-100% reads as a full 100%; 94% still reads 94
// Writes OUTPUT/<name>_NNN.ppm, OUTPUT/<name>.tsv (tile labels) and
// OUTPUT/<name>_captions.txt (one caption per frame).
#include <stdio.h>
#include <string>
#include <vector>
#include <memory>
#include "CreatureAnimator.h"
#include "InteractionPolicy.h"
#include "MotionGestures.h"
#include "palette.h"

static constexpr int FRAMES = 75, STEP_MS = 80, LABEL_H = 28;

struct Scene {
  std::vector<uint16_t> fb;
  EyeRenderer r; Eyes eyes; CreatureAnimator c;
  uint32_t now = 0;
  Scene() : fb(SCREEN_WIDTH * SCREEN_HEIGHT) {
    previewRandomState() = 0x9E3779B9u;
    r.begin(fb.data(), nullptr); eyes.begin(&r, 1234, 0); c.begin(&eyes, 1234, 0);
    eyes.setIdle(false); eyes.setAutoBlink(false); c.setIdleActsAllowed(false);
    c.startWake(now, false); run(5000);
  }
  void run(uint32_t ms) { for (uint32_t k = 0; k < ms; k += 20) { now += 20; c.update(.020f, now); } }
  // Copies a (src) square of the frame, scaled to tile px, into rgb at (ox, oy).
  void paint(std::vector<unsigned char> &rgb, int stride, int ox, int oy, int tile,
             float sx0 = 0, float sy0 = 0, float span = SCREEN_WIDTH) {
    eyes.draw();
    for (int y = 0; y < tile; ++y) for (int x = 0; x < tile; ++x) {
      int ix = int(sx0 + (x + .5f) * span / tile), iy = int(sy0 + (y + .5f) * span / tile);
      unsigned char *p = &rgb[((oy + y) * stride + ox + x) * 3];
      if (ix < 0 || iy < 0 || ix >= SCREEN_WIDTH || iy >= SCREEN_HEIGHT) { p[0] = p[1] = p[2] = 0; continue; }
      uint16_t v = fb[iy * SCREEN_WIDTH + ix]; v = uint16_t((v << 8) | (v >> 8));
      p[0] = (unsigned char)(((v >> 11) & 31) * 255 / 31);
      p[1] = (unsigned char)(((v >> 5) & 63) * 255 / 63);
      p[2] = (unsigned char)((v & 31) * 255 / 31);
    }
  }
};

static void writeFrame(const std::string &dir, const char *name, int frame,
                       const std::vector<unsigned char> &rgb, int width, int height) {
  char path[1024]; snprintf(path, sizeof(path), "%s/%s_%03d.ppm", dir.c_str(), name, frame);
  FILE *out = fopen(path, "wb"); if (!out) { perror(path); exit(2); }
  fprintf(out, "P6\n%d %d\n255\n", width, height); fwrite(rgb.data(), 1, rgb.size(), out); fclose(out);
}
static FILE *openText(const std::string &dir, const char *name, const char *suffix) {
  char path[1024]; snprintf(path, sizeof(path), "%s/%s%s", dir.c_str(), name, suffix);
  FILE *f = fopen(path, "w"); if (!f) { perror(path); exit(2); }
  return f;
}

static void starfall(const std::string &dir) {
  Scene s; s.c.setAlwaysOn(true, s.now); s.run(2600);
  const int tile = 240, width = tile * 2, height = tile + LABEL_H;
  FILE *meta = openText(dir, "starfall", ".tsv");
  fprintf(meta, "0\t0\t%d\tAlways-on face\n%d\t0\t%d\tClose-up of the left pupil (2x)\n", tile, tile, tile);
  fclose(meta);
  FILE *captions = openText(dir, "starfall", "_captions.txt");
  for (int f = 0; f < FRAMES; ++f) {
    s.run(STEP_MS);
    std::vector<unsigned char> rgb(width * height * 3, 0);
    s.paint(rgb, width, 0, 0, tile);
    const EyeFrame &e = s.eyes.frames()[0];
    float cx = e.cx + e.pupilX, cy = e.cy + e.pupilY;
    s.paint(rgb, width, tile, 0, tile, cx - 60, cy - 60, 120);
    writeFrame(dir, "starfall", f, rgb, width, height);
    fprintf(captions, "Stars fall from the middle of each black pupil and fade where the coloured eye starts\n");
  }
  fclose(captions);
}

static void doubleTap(const std::string &dir) {
  Scene s;
  WorldFollower world; world.setFlatUpSign(GAZE_FLAT_UP_SIGN); world.beginWakeSession();
  // Held in the hand facing you (gravity mostly along screen Y), leaning
  // h degrees sideways and v degrees back.
  auto pose = [&](float h, float v, uint32_t ms) {
    float hr = h * .017453293f, vr = v * .017453293f, g[3] = {};
    float a[3] = {9.81f * sinf(hr), 9.81f * cosf(hr) * cosf(vr), 9.81f * cosf(hr) * sinf(vr)};
    for (uint32_t k = 0; k < ms; k += 20) {
      s.now += 20; world.feed(a, g, 0, s.now);
      s.c.setTilt(world.tiltX, world.tiltY); s.c.setSwing(world.swingX, world.swingY);
      s.c.update(.020f, s.now);
    }
  };
  pose(0, 10, 1200); // the wake pose is the left/right zero
  const int tile = 240, width = tile, height = tile + LABEL_H;
  FILE *meta = openText(dir, "double-tap", ".tsv");
  fprintf(meta, "0\t0\t%d\tReal sensor fusion and firmware\n", tile);
  fclose(meta);
  FILE *captions = openText(dir, "double-tap", "_captions.txt");
  for (int f = 0; f < FRAMES; ++f) {
    uint32_t t = f * STEP_MS;
    const char *caption;
    if (t < 1600) { pose(22, 45, STEP_MS); caption = "Held at an angle: he looks off to the side"; }
    else {
      if (t == 1600) { // the firmware's double-tap path
        s.c.react(tapMood(2), s.now, 1050);
        world.requestCenter(s.now); s.c.centerGaze(s.now);
      }
      float turn = t < 2800 ? 0 : 28 * sinf((t - 2800) * .0021f);
      pose(22 + turn, 45, STEP_MS);
      caption = t < 2800 ? "Double-tap: this pose is now dead ahead (left/right and up/down)"
                         : "Turn left or right: he follows from the new zero";
    }
    std::vector<unsigned char> rgb(width * height * 3, 0);
    s.paint(rgb, width, 0, 0, tile);
    writeFrame(dir, "double-tap", f, rgb, width, height);
    fprintf(captions, "%s\n", caption);
  }
  fclose(captions);
}

static void battery(const std::string &dir) {
  const int levels[3] = {100, 96, 94}, tile = 240, width = tile * 3, height = tile + LABEL_H;
  std::vector<std::unique_ptr<Scene>> scenes;
  FILE *meta = openText(dir, "battery", ".tsv");
  for (int i = 0; i < 3; ++i) {
    scenes.emplace_back(new Scene());
    Scene &s = *scenes.back();
    s.c.setBatteryLevel(levels[i]); s.run(2000);
    fprintf(meta, "%d\t0\t%d\tBattery at %d%%\n", i * tile, tile, levels[i]);
  }
  fclose(meta);
  FILE *captions = openText(dir, "battery", "_captions.txt");
  for (int f = 0; f < FRAMES; ++f) {
    std::vector<unsigned char> rgb(width * height * 3, 0);
    for (int i = 0; i < 3; ++i) {
      Scene &s = *scenes[i];
      if (f * STEP_MS == 400) s.c.showBattery(levels[i], s.now);
      s.run(STEP_MS);
      s.paint(rgb, width, i * tile, 0, tile);
    }
    writeFrame(dir, "battery", f, rgb, width, height);
    fprintf(captions, "Ten taps: from 95%% up the readout shows a full 100%%\n");
  }
  fclose(captions);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: highlights OUTPUT\n"); return 1; }
  starfall(argv[1]);
  doubleTap(argv[1]);
  battery(argv[1]);
  return 0;
}
