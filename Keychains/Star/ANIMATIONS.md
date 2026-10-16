# Star animation inventory

These are layers and behavior states, not separate GIF assets on the ESP32. The firmware draws all frames procedurally. Several states share the same expression or blink; do not add the counts together as unique animations.

The five embedded [GIF sheets in the README](README.md#animation-gallery) preview the full catalog. The GIFs are desktop exports, not assets stored on the ESP32.

## 15 base expressions

Neutral, happy, sad, angry, focused, surprised, sleepy, squint, curious, confused, suspicious, worried, annoyed, love (heart pupils), dizzy (spiral pupils).

## 31 behavior states

| # | Behavior | What is shown |
|---|---|---|
| 1 | Idle | Breathing, drift, small glances and micro-expressions |
| 2 | Happy | Crescent eyes with gentle bounce |
| 3 | Sad | Sloped lids and lowered eyes |
| 4 | Angry | Anticipation, glare, squints, huff and cooling sulk |
| 5 | Dizzy | Spiral tumble and overshooting recovery |
| 6 | Falling asleep | Droop, long blink, closing and fade |
| 7 | Surprised | Squash, pop open and double take |
| 8 | Anxious | Worried eyes and quick darts |
| 9 | Wake up | Classic, one-eye, startled or sleepy choreography |
| 10 | Battery | Eyes ease into a liquid reservoir and return |
| 11 | Boop | Near-eye flinch, rebound, wink and optional giggle |
| 12 | Petted | Smiling eyes and gentle rocking |
| 13 | Swiping | Pull/stretch in each swipe direction |
| 14 | Following | Finger tracking; prolonged contact builds anger |
| 15 | Confused | Uneven lids and sideways/upward look |
| 16 | Shy | Lowered, asymmetric worried eyes |
| 17 | Shiver | Cold squint with tremor |
| 18 | Loved | Heart pupils |
| 19 | Upside down | Worried to angry as the hold continues |
| 20 | Curious | One eye opens and looks to the side |
| 21 | Yawn | Heavy lids, squeeze and slow reopening |
| 22 | Look around | Left, right, up and back to the viewer |
| 23 | Wink | One-eye closure and follow-through |
| 24 | Playful | Wiggle, squash/stretch and wink |
| 25 | Stretch | Tall eyes, upward look and release blink |
| 26 | Peek | One eye opens before its partner |
| 27 | Giggle | Repeated happy bounces and recovery blink |
| 28 | Sneeze | Wind-up, open, squeeze and confused recovery |
| 29 | Sassy | Raised lid, eye roll and wink |
| 30 | Always-on enabled | Squeeze, pop open, alternating winks and subtle pupil sparkles |
| 31 | Automatic sleep restored | Settling blink and fading closed-eye cue |

## Blink and motion layers

Seven blink types are available: normal, fast, long, half, slow, squeeze and uneven. Automatic blinking uses all except the explicitly requested squeeze type. Double blinks and left/right winks combine these types. Other continuous layers include gaze/saccades and tremor, tilt tracking, swing/inertia recoil, walking/running step bobs, shake rattle, battery fatigue at each 10% step, the charging smile/halo and green drops.

Charging drops use a shared deterministic launch schedule. On unplug, no new cycles are emitted; each existing drop travels into the current eye edge and dissolves. The eyes stay green until the final scheduled arrival, then fade to the normal charge-dependent hue. Replug resumes emission. Blinks, winks and critical sleep still hide the stream with the actual aperture.

The battery reservoir fills vertically to the measured estimate. Its meniscus sloshes while charging and small bubbles rise inside the liquid. It never animates a false increase in percentage. Heavier rounded numbers sit below the reservoir; the former moving highlight stripe and text badge are removed.

## Optional nap-only variants

Left-eye peek, right-eye peek, dream flutter, both-eye peek. These exist for the screen-on nap policy. The current default uses deep sleep, so these do not run after the display switches off. They appear in the supplemental catalog as available variants.

## Always-on shortcut

While on the face, tap once, then press again within 420 ms and hold for at least 850 ms. A hold toggles only once until release. Repeat the gesture to return to automatic sleep. Ordinary double taps, single holds, drags and lost contact do not toggle. The mode disables inactivity sleep and drowsiness, while normal motion/touch reactions continue. Critical low battery still forces shutdown. Restarting the device restores automatic sleep.

## Recreate the GIF catalog

Build `tools/preview/animation_catalog.cpp` against the same sources and stubs as `preview.cpp`, run it with an existing output directory, then run `catalog.py FRAMES OUTPUT`. Five labeled sheets cover expressions, all behavior states, motion/charging effects, and blink/nap variants. All images come from the actual firmware renderer. The GIF exporter checks decoded sample frames against its source frames.

To refresh one sheet, pass a zero-based page index to the renderer and exporter:
`animation_catalog FRAMES 2`, then `python catalog.py FRAMES GIF_DIR --page 2`.
Page 2 contains the always-on pupil cues. Gaze effects in the catalog show the
rendering response; sensor-origin capture is tested separately by `wake_origin_test`.
