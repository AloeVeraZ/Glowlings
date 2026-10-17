# Desktop preview

Builds the real `Eyes`, `EyeRenderer` and `CreatureAnimator` sources from the sketch on a
desktop (with a tiny Arduino shim in `stubs/`) and writes frames, so eye shapes
and animation timing can be checked without flashing the board.

```sh
./build.sh                      # needs g++
./preview sheet frames          # one settled frame per expression -> frames/*.ppm
./preview strip frames shake    # timed sequence: blink, morph, wake, sleep, shake, rattle, hold, surprised, angry, gaze
pip install pillow
python3 sheet.py out.png frames/*.ppm --cols 4   # contact sheet, cropped to the round panel
python3 make_previews.py frames                  # refresh the images used in the main README
```

`./run_tests.sh` runs detector, input, power, rendering and soak checks on the same sources: `motion_test` plays knocks (simulated at the sensor's real 1 kHz rate and filter), tilt flicks, rocking and twists through the touch-free gestures, against walking, running and shaking; `touch_test` plays
touch-controller report streams (taps, flicks, long holds that wander, lost
lift reports) through the sketch's finger tracker; `shake_test` feeds
simulated shakes, wrist flicks, walking, running and knocks through the
sketch's shake and twist detectors (awake, and the wake-from-sleep check) at
the slow sample rates the render loop allows; `soak_test` drives the animator,
eye system and renderer with hours of random input under address and undefined-behaviour
sanitizers.

`imu_test.cpp` compiles the real QMI8658 driver against an I2C stub and checks
normal/wake-on-motion register settings, signed raw sample decoding, timestamps,
temperature, sample locking and incomplete/failed transfers. The retired generic
shake-wake detector and its old thresholds live in `legacy_shake_wake.h` beside
the comparison test; production wake detection uses `LeftRightWake.h`.

Frames show exactly what the panel receives. Every frame is checked against the palette: each pixel must be a blend of black, `EYE_COLOR` and `PUPIL_COLOR` (anti-aliased edges and the glow), or the optional blush on black; the preview and the soak test fail if any other colour appears. `motion_test` also checks the world-following directions (tilt gaze, level roll, swing, spin, toss) and footstep detection (walking and running, but not shaking, rocking, twisting or knocks).
Timing on a PC says nothing about ESP32 speed; set `LOG_FPS` in `StarFace.ino`
to measure frame rate on the board.

Horizontal gaze reaches full travel at 40 degrees, with the existing 4-degree
neutral zone. `gaze_expect.h` computes partial-travel expectations from those
config values; physical direction, vertical tracking and response timing remain
separate checks. `horizontal_tracking_test` checks both sides of the neutral zone
and partial/full travel in flat and hanging poses. Saturation fixtures use turns
past the configured full-travel angle. Wake and manual-center tests use the same
expectations, including movement immediately after a manual zero.

Battery and power behavior is covered by `battery_test.cpp`: charge stages and recovery hysteresis, critical sleep recovery, the initial 30-second window and renewable 15-second inactivity deadlines (including several minutes of interaction, interrupted closing, and millis wrap), single-touch wake (including a tap released before CPU startup), held-touch/I2C-error handling, triple/four-tap anger ending, and gyro-only upright pan. The restored testing-branch wake check accepts sustained shaking on any axis and rejects walking/running traces (including 20-second continuous checks), short/paused shakes, single knocks and gyro-only swinging. One screen press wakes the current firmware; the old generic shake checker is retained only as a detector regression. The shake tests recognize at the same fast sampling interval as the sketch and deliver events at varied render rates.
# Directional motion wake checks

Live-readout regressions toggle unplug/replug within one battery animation, confirm connection changes on the 100 ms poll cadence, reject extra samples within a poll, and inspect actual framebuffer colors/bolt cleanup while the eyes are still fading green. They also verify the original readout deadline and run across timer wrap. The face and closing sequence now share that 100 ms poll cadence. Voltage calibration and percentage rate limits remain in place.

Charging regressions in `battery_test` preserve the measured sub-4.45 V rise, two-sample classification, duplicate spam reads, isolated spikes and ADC noise. The gauge scenarios reproduce the 22-to-80% unplug/replug bug, confirm progressive charging without immediate jumps, exercise 87/88 target jitter, preserve complete snapshots through sleep/reset, reject corrupt/NaN snapshots, limit full charging targets and verify immediate low-voltage protection despite a high displayed percentage. Scenarios run across timer wrap. A steady full battery alone cannot assert charging. Charging frames at 21%, 100% and unknown charge verify green eyes, the liquid meniscus/bubble animation, circular screen bounds, unplug cleanup and return to ordinary eyes. Pass an output directory to save `charging-*.ppm` frames. Detection and percentage remain voltage-based approximations; the checks do not establish precise state-of-charge accuracy.

`left_right_wake_test.cpp` exercises the firmware's current `LeftRightWakeCheck`.
It preserves the user's deliberate left/right and carried forward/back records,
replays them at several assumed intervals/starting points, and checks sustained
wake, short/paused attempts, sample gaps and wrong-axis movement. The supplied
records have no timestamps; repeated replay is a classification regression,
not evidence of physical gesture duration or universal walking rejection.
The directional checker uses a 350 ms confirmation window with four strokes
on each required axis and a maximum 190 ms gap between reversals. Cadence tests
accept brisk 3-5 Hz movement with timing jitter and reject strong sideways
0.7-2.5 Hz pendulum swings, including walking/running bounce, small sensor noise
and short separated attempts. The tests also cover timer wrap and reject the
recorded carried motion at every recording offset. A late sample is checked
for stale reversals before its new stroke can refresh the timestamp.
The generic wake checks in `shake_test.cpp` retain the older testing baseline.

Battery checks also cover the continuous purple/yellow/red gradient, heavier lids at each 10% drop, recovery hysteresis, and ten taps passing through the protected five-tap anger pause. Readout frames exercise exact percentages, unknown readings, the swirl entry/exit, interrupted shutdown, and removal of white digits afterward. `battery_test OUTPUT_DIR` saves the color poses, battery readouts, and the complete 73% swirl sequence for visual inspection. Percentage text adds white only while the readout is active; ordinary frames still use their current eye color and black.

`animation_test.cpp` checks gaze settling, tilt response within 150 ms at several sample intervals, new wink/playful/stretch gestures ending normally, and exploratory gaze during a quiet hold. The soak includes the new gestures and motion attention. The preview supports `strip OUTDIR wink`, `playful`, and `stretch` as well as the existing sequences. The battery eases into its readout over 280 ms and back out over 280 ms; total time is 4.2 seconds.

`motion_sleep_test.cpp` covers cancellation at and beyond the closing deadline (including millis wrap), stationary touch confirmed for two minutes without coordinate IRQs, sustained turn activity, slow tilt activity, all four USB-bottom tilt directions for either flat-up sensor sign, flat gyro-only left/right turns, and an upright-to-flat transition. Held tilts retain their actual direction instead of drifting into a new arbitrary normal.

The product polish adds `strip OUTDIR peek`, `giggle`, `sneeze`, `sassy`, and `boop`. These render the real per-eye squash/stretch accents and brighter rim/halo. The sanitizer soak includes every new performance. Power checks now enforce untouched wake = 30 seconds and exact 15-second renewal for confirmed touch only; continuous gyro activity, self-animation and invalid/unconfirmed touch reports do not extend the deadline. The sensor-axis and upright hanging gaze checks remain in place.

`strip OUTDIR environment` renders a sequence of simulated quiet, movement, carrying, settling and upright sensor contexts through the actual behavior code. `animation_test` checks one-shot context transitions, jitter rejection and absence of a looping quiet-room mood playlist, as well as increased gaze footprint, settling back to rest, and the curved outline staying on the panel in eight directions. The soak checks 64 points on each actual rotated/bent ellipse for screen containment.

`gaze_layout_test.cpp` tests full-range sideways motion, held heading, return paths and short missed-read gaps in table/hanging poses. It samples the real eye outlines across every expression and gaze direction to require both screen containment and a 10.8 px gap. Pass an output directory to render representative left/centre/right and downward-corner frames as PPM files. The three-hour soak now checks pair separation on every frame as well.

The charging update adds `strip OUTDIR charging` for a preview of the real bottom-to-eyes particle streams. Battery regressions cover plug-in at the start, middle and end of closing, a finished closing deadline, inactive sleep rejection, timer wrap, 35 mV near-full first-plug and replug detection, small isolated step rejection, isolated high ADC spikes, final 98-to-99% progress, particle animation/palette/circular bounds and unplug cleanup. Hardware charge termination remains unavailable, so 99% while charging is an estimate rather than a full-charge assertion.

Unified charging: `strip OUTDIR charging-blink` renders a 150-frame sequence with an authored blink and one-eye wink. The green drops merge into the eyes through the same signed-distance field and disappear with the real aperture. Regression checks require exact framebuffer equality with charging disabled when both lids are closed, and no charging residue beside the closed eye during a wink, across 24 animation phases. Supply tests cover slow 60 mV charger taper, recent-observation restore across sleep, V3 snapshot migration and a real unplug after wake. These are inference regressions, not proof of USB presence at a steady-voltage cold boot.

Joyful/liquid styling: `strip OUTDIR joyful-charge` renders 300 frames of the persistent open-iris charging smile with a blink and wink; `strip OUTDIR fluid` renders 150 frames of the softer purple resting flow. These use the real firmware renderer. `animation_test` checks a lifted curved lower lid without iris disappearance, changing outer pixels with stable iris interiors over 24 phases, round-panel containment, palette consistency and exact closed-lid cleanup with both liquid flow and charging enabled. Charging joy is a pose layer and yields to direct reactions and sleep; there is no new timed gesture playlist.

Current responsive style uses the existing springs and rounded eye contours for every expression. Charging keeps the joyful pose and green bubbles from the bottom USB-C edge; free drops use cheap circle patches, and only joining drops evaluate the eye distance field. Six staggered drops keep the stream light; the joining blend shrinks with each drop. Blinks and winks hide the stream before the lids finish closing, avoiding small teeth along nearly closed eyes. There are no full-field harmonic distortions.

`strip OUTDIR battery` renders unknown, low, full and intermediate percentages in both charging states, including every digit. The reservoir fills vertically to the measured estimate; the liquid meniscus and small rising bubbles animate while charging. Heavier rounded figures sit underneath, without the previous text badge, sweep stripe or bolt. The figures are generated once from quadratic curves into a coverage atlas and sampled at sub-pixel positions. Charging redraws on 16 cached animation steps; static holds skip rendering and LCD transfer. The old `iphone-battery` preview name remains an alias.

Supply regressions include repeated replug after cell relaxation, a stale high gauge baseline, isolated low and high ADC outliers, the observed slow connected-board rise (~35 mV in 80 s), and timer wrap. Fast cable edges use a latched recent baseline, guarded by two preceding samples; a slow positive trend is retained for up to two minutes instead of erased every ten seconds. Detection still requires a real voltage change: stable VBAT cannot establish USB presence, charging current, or charge termination.

## Liquid reservoir, unplug finish and always-on

`always_on_test.cpp` tests tap/hold recognition, repeated toggles, normal double taps, long single holds, drags, lost contact, timer wrap, an hour with inactivity sleep disabled, automatic sleep after switching off, and critical shutdown priority. It also checks pupil-only mode marks, exact closed-lid cleanup and no interference with heart/spiral pupils and the charge finish scheduler: active drops only decrease after unplug and all finish before the color fades. Battery regressions verify the new reservoir palette, capacity persistence and immediate live-readout color changes; near-closed blink checks remain exact.

`animation_catalog.cpp OUTPUT` emits five sheets of real firmware frames. Build it like `preview.cpp`, create OUTPUT first, then run `python catalog.py OUTPUT GIF_DIR`. The exporter labels all scenes, optimizes the GIFs and verifies decoded sample frames. The fifth sheet includes optional nap-only peeks and the manually callable squeeze blink, which are labeled as available variants in `../../ANIMATIONS.md`.

`highlights.cpp OUTPUT` renders the newest behaviors (always-on starfall with a pupil close-up, the double-tap zero through the real sensor fusion, and the 95-100% battery readout). Build it like `animation_catalog.cpp`, then run `python highlights.py OUTPUT GIF_DIR` to write `07-always-on-starfall.gif`, `08-double-tap-zero.gif` and `09-battery-full.gif`.

## Wake-relative horizontal center

`wake_origin_test.cpp` exercises the production `WorldFollower::beginWakeSession`
path. A stable gravity window (at least 150 ms and three readings) establishes
neutral; noisy, moving, shaking or invalid samples cannot finish capture. The
640-case matrix covers resting angles, both gravity polarities, flat/hanging
poses, all four configured XY mounting rotations, 10–100 ms reads and timer wrap.
Additional cases check a second wake at another angle, holds, long sensor gaps,
heading turns, and sideways/inverted resting origins. Existing orientation-loop,
gaze-layout, motion/sleep, always-on and battery checks remain in the suite.

Axis rotations are explicitly configured in these tests and in the board driver.
Session zeroing compensates resting angle, not unknown hardware axis wiring or
absolute compass heading. Physical behavior on a replacement board still needs
validation with its driver and axis configuration.

`periodic_gaze_test.cpp` covers the 15-second recenter interval, two-second quiet
hold and one-second smooth reference blend, including timer rollover and
10–100 ms sensor reads. It verifies repeated refreshes, movement/interaction
blocking, cancellation during a blend, invalid samples and sensor gaps. Initial
wake capture still uses the shorter 150 ms window. References ease independently
of the orientation integrator and eye springs; up/down behavior is unchanged.
Raw held-heading layout fixtures explicitly disable the periodic policy so they
can continue testing geometry and return paths beyond the idle-refresh interval.

## Double-tap horizontal zero and recenter cue

`manual_center_test.cpp` reproduces a rightward motion origin, a double tap,
and setting the new zero. It exercises the real fusion-to-animator-to-pupil
path: the fused left/right target responds within 150 ms of zeroing, and the
normal double-click reaction finishes at that new zero. It checks return tracking, relative gyro turns,
vertical-reference preservation, and stale-input fallback, including timer wrap.

Gesture streams distinguish a double tap from the tap-then-hold mode toggle,
including the 420 ms boundary, long holds, drags, distant taps, missed press reports
and timer wrap. Recenter fires once on the second tap; subsequent taps still
reach the existing angry and battery actions.
There is no five-second display lock. Resting gaze uses the rebased motion target
instead of reviving a stale sideways glance. New touch works normally; critical
sleep keeps priority.

Manual recentering keeps the existing double-click surprised reaction and clears
the old finger target. The 460 ms `RECENTER` cue squeezes both eyes, pops them open,
then settles after an idle automatic recenter completion; protected reactions and
sleep retain priority. Checks cover the retained double-click reaction, zero
persistence, cue timing, palette and automatic completion. `preview strip OUTDIR recenter`
exports frames from the actual renderer. The revised gesture and cue still need
a physical check on Star.
