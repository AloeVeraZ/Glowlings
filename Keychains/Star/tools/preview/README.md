# Star desktop preview

Render and test the real `Eyes`, `EyeRenderer` and `CreatureAnimator` sources
without flashing the board. The small Arduino shim lives in `stubs/`.

[Star](../../README.md) · [Developer guide](../../docs/DEVELOPMENT.md) ·
[Animation inventory](../../ANIMATIONS.md)

## Build and export frames

Requirements: a C++17 compiler (`g++` in the scripts), Python 3, Pillow, and a
POSIX shell for the `.sh` scripts. Commands below start in this directory.

```sh
sh build.sh
mkdir -p ../../../../build/star-preview/expressions
./preview sheet ../../../../build/star-preview/expressions
python -m pip install pillow
python sheet.py ../../../../build/star-preview/expressions.png ../../../../build/star-preview/expressions/*.ppm --cols 4
```

For an individual scene, create an output directory and select a strip:

```sh
mkdir -p ../../../../build/star-preview/charging
./preview strip ../../../../build/star-preview/charging charging
```

Other strips include `blink`, `morph`, `wake`, `sleep`, `shake`, `rattle`,
`hold`, `surprised`, `angry`, `gaze`, `wink`, `playful`, `stretch`, `peek`,
`giggle`, `sneeze`, `sassy`, `boop`, `environment`, `recenter`,
`charging-blink`, `joyful-charge`, `fluid` and `battery`.
`iphone-battery` is a legacy alias for `battery`.
`make_previews.py FRAMES` refreshes the older static expression previews.

## Regenerate the GIF catalog

From this directory:

```sh
mkdir -p ../../../../build/star-preview
g++ -std=gnu++17 -O2 -Wall -Istubs -I../../StarFace animation_catalog.cpp ../../StarFace/EyeRenderer.cpp ../../StarFace/Eyes.cpp ../../StarFace/Expressions.cpp ../../StarFace/CreatureAnimator.cpp -o ../../../../build/star-preview/animation_catalog
mkdir -p ../../../../build/star-preview/catalog
../../../../build/star-preview/animation_catalog ../../../../build/star-preview/catalog
python catalog.py ../../../../build/star-preview/catalog ../../docs/animations
```

The five sheets cover expressions, 32 behavior states, effects/wake variants,
and blink/optional-nap variants. These categories overlap. The exporter labels
the scenes and checks decoded sample frames against the renderer output.

To refresh one page, pass its zero-based index to the C++ exporter and
`--page INDEX` to `catalog.py`. Page 2 contains behavior states 17–32, including
the always-on and automatic recenter cues.

Build `highlights.cpp` with the same sources for the focused demonstrations:

```sh
mkdir -p ../../../../build/star-preview/highlights
g++ -std=gnu++17 -O2 -Wall -Istubs -I../../StarFace highlights.cpp ../../StarFace/EyeRenderer.cpp ../../StarFace/Eyes.cpp ../../StarFace/Expressions.cpp ../../StarFace/CreatureAnimator.cpp -o ../../../../build/star-preview/star_highlights
../../../../build/star-preview/star_highlights ../../../../build/star-preview/highlights
python highlights.py ../../../../build/star-preview/highlights ../../docs/animations
```

These produce `07-always-on-starfall.gif`, `08-double-tap-zero.gif` and
`09-battery-full.gif`. The standalone recenter cue can be refreshed from
`preview strip OUTPUT recenter`; its sheet version is also on catalog page 2.

## Tests

```sh
sh run_tests.sh
```

The script builds and runs **17** test programs. Its soak build requires
AddressSanitizer and UndefinedBehaviorSanitizer support in the selected compiler;
a platform without those runtimes needs a separate build and must report that
limitation. An ordinary soak pass does not establish a sanitizer pass.

| Test | Main coverage |
| --- | --- |
| `shake_test` | Awake shake/twist detection, knocks and the legacy generic wake baseline |
| `left_right_wake_test` | Production directional wake, supplied traces, cadence, carrying rejection and timer wrap |
| `motion_test` | Knocks, tilt flicks, rocking, footsteps, motion direction and false-trigger scenarios |
| `periodic_gaze_test` | 15-second refresh interval, two-second steady gate, one-second blend and interruption |
| `wake_origin_test` | Stable wake capture, moving-hand fallback, mounting variants, gaps and session retention |
| `horizontal_tracking_test` | Dead zone, partial/full travel, holds and returns in flat/hanging postures |
| `orientation_loop_test` | Relative heading through closed orientation loops |
| `touch_test` | Finger reports, final-flick swipes, holds and missing lift reports |
| `touch_irq_test` | Real CST816S driver, pending interrupts and touch confirmation |
| `imu_test` | Real QMI8658 driver, initialization, raw decoding, timestamps, sample lock and failed reads |
| `manual_center_test` | Double-tap zero on both axes, posture crossing, live tracking, stale fallback and mode separation |
| `always_on_test` | Tap/hold recognition, pupil starfall, mode cues, critical priority and unplug finish |
| `battery_test` | Gauge persistence, connection inference, presentation threshold, liquid readout and sleep deadlines |
| `animation_test` | Responsiveness, authored gestures, environment transitions and charging cleanup |
| `motion_sleep_test` | Touch during closing, held contact, activity policy and USB-bottom gaze |
| `gaze_layout_test` | Eye contours, round-panel containment, held heading and minimum pair gap |
| `soak_test` | Three simulated hours of randomized animation/input and per-frame palette/geometry checks |

Some render tests accept an output directory for inspection frames; see their
`main()` arguments. `gaze_expect.h` computes expected partial travel from the
configured dead zone/range, while direction and geometry have separate checks.
The retired generic wake detector lives in `legacy_shake_wake.h`, outside firmware.

## What these checks establish

The renderer checks palette, aperture cleanup and layout on actual firmware
frames. Desktop elapsed time does not measure ESP32 speed. Set `LOG_FPS` in
`StarFace.ino` to measure the board; the normal loop is capped at 20 FPS.

Voltage replays test the implemented heuristic, not true USB presence, charge
current or full-charge detection. Physical carry/wake and cable tests remain
separate. See the [dated reports](../../docs/README.md#dated-reports).

[On-device footage](../../docs/media/README.md) is separate from the generated
GIF catalog and should be labeled accordingly.
