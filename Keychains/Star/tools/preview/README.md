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

`./run_tests.sh` runs four checks on the same sources: `motion_test` plays knocks (simulated at the sensor's real 1 kHz rate and filter), tilt flicks, rocking and twists through the touch-free gestures, against walking, running and shaking; `touch_test` plays
touch-controller report streams (taps, flicks, long holds that wander, lost
lift reports) through the sketch's finger tracker; `shake_test` feeds
simulated shakes, wrist flicks, walking, running and knocks through the
sketch's shake and twist detectors (awake, and the wake-from-sleep check) at
the slow sample rates the render loop allows; `soak_test` drives the animator,
eye system and renderer with hours of random input under address and undefined-behaviour
sanitizers.

Frames show exactly what the panel receives. Every frame is checked against the palette: each pixel must be a blend of black, `EYE_COLOR` and `PUPIL_COLOR` (anti-aliased edges and the glow), or the optional blush on black; the preview and the soak test fail if any other colour appears. `motion_test` also checks the world-following directions (tilt gaze, level roll, swing, spin, toss) and footstep detection (walking and running, but not shaking, rocking, twisting or knocks).
Timing on a PC says nothing about ESP32 speed; set `LOG_FPS` in `StarFace.ino`
to measure frame rate on the board.

Battery and power behavior is covered by `battery_test.cpp`: charge stages and recovery hysteresis, critical sleep recovery, the initial 30-second window and renewable 15-second inactivity deadlines (including several minutes of interaction, interrupted closing, and millis wrap), single-touch wake (including a tap released before CPU startup), held-touch/I2C-error handling, triple/four-tap anger ending, and gyro-only upright pan. The restored testing-branch wake check accepts sustained shaking on any axis and rejects walking/running traces (including 20-second continuous checks), short/paused shakes, single knocks and gyro-only swinging. One screen press wakes the current firmware; the old generic shake checker is retained only as a detector regression. The shake tests recognize at the same fast sampling interval as the sketch and deliver events at varied render rates.
# Directional motion wake checks

Live-readout regressions toggle unplug/replug within one battery animation, confirm connection changes on the 100 ms poll cadence, reject extra samples within a poll, and inspect actual framebuffer colors/bolt cleanup while the eyes are still fading green. They also verify the original readout deadline and run across timer wrap. The face and closing sequence now share that 100 ms poll cadence. Voltage calibration and percentage rate limits remain in place.

Charging regressions in `battery_test` preserve the measured sub-4.45 V rise, two-sample classification, duplicate spam reads, isolated spikes and ADC noise. The gauge scenarios reproduce the 22-to-80% unplug/replug bug, confirm progressive charging without immediate jumps, exercise 87/88 target jitter, preserve complete snapshots through sleep/reset, reject corrupt/NaN snapshots, limit full charging targets and verify immediate low-voltage protection despite a high displayed percentage. Scenarios run across timer wrap. A steady full battery alone cannot assert charging. Charging frames at 21%, 100% and unknown charge verify green eyes, the green sweep/bolt animation, circular screen bounds, unplug cleanup and return to ordinary eyes. Pass an output directory to save `charging-*.ppm` frames. Detection and percentage remain voltage-based approximations; the checks do not establish precise state-of-charge accuracy.

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

`animation_test.cpp` checks gaze settling, tilt response within 150 ms at several sample intervals, new wink/playful/stretch gestures ending normally, and exploratory gaze during a quiet hold. The soak includes the new gestures and motion attention. The preview supports `strip OUTDIR wink`, `playful`, and `stretch` as well as the existing sequences. The battery swirl now makes a full inward turn in 650 ms, holds its readout, then reverses out; total time is 4.2 seconds.

`motion_sleep_test.cpp` covers cancellation at and beyond the closing deadline (including millis wrap), stationary touch confirmed for two minutes without coordinate IRQs, sustained turn activity, slow tilt activity, all four USB-bottom tilt directions for either flat-up sensor sign, flat gyro-only left/right turns, and an upright-to-flat transition. Held tilts retain their actual direction instead of drifting into a new arbitrary normal.

The product polish adds `strip OUTDIR peek`, `giggle`, `sneeze`, `sassy`, and `boop`. These render the real per-eye squash/stretch accents and brighter rim/halo. The sanitizer soak includes every new performance. Power checks now enforce untouched wake = 30 seconds and exact 15-second renewal for confirmed touch only; continuous gyro activity, self-animation and invalid/unconfirmed touch reports do not extend the deadline. The sensor-axis and upright hanging gaze checks remain in place.

`strip OUTDIR environment` renders a sequence of simulated quiet, movement, carrying, settling and upright sensor contexts through the actual behavior code. `animation_test` checks one-shot context transitions, jitter rejection and absence of a looping quiet-room mood playlist, as well as increased gaze footprint, settling back to rest, and the curved outline staying on the panel in eight directions. The soak checks 64 points on each actual rotated/bent ellipse for screen containment.

`gaze_layout_test.cpp` tests full-range sideways motion, held heading, return paths and short missed-read gaps in table/hanging poses. It samples the real eye outlines across every expression and gaze direction to require both screen containment and a 10.8 px gap. Pass an output directory to render representative left/centre/right and downward-corner frames as PPM files. The three-hour soak now checks pair separation on every frame as well.

The charging update adds `strip OUTDIR charging` for a preview of the real bottom-to-eyes particle streams. Battery regressions cover plug-in at the start, middle and end of closing, a finished closing deadline, inactive sleep rejection, timer wrap, 35 mV near-full first-plug and replug detection, small isolated step rejection, isolated high ADC spikes, final 98-to-99% progress, particle animation/palette/circular bounds and unplug cleanup. Hardware charge termination remains unavailable, so 99% while charging is an estimate rather than a full-charge assertion.

Unified charging: `strip OUTDIR charging-blink` renders a 150-frame sequence with an authored blink and one-eye wink. The orbiting stars merge into the eyes through the same signed-distance field and disappear with the real aperture. Regression checks require exact framebuffer equality with charging disabled when both lids are closed, and no charging residue beside the closed eye during a wink, across 24 animation phases. Supply tests cover slow 60 mV charger taper, recent-observation restore across sleep, V3 snapshot migration and a real unplug after wake. These are inference regressions, not proof of USB presence at a steady-voltage cold boot.

Joyful/liquid styling: `strip OUTDIR joyful-charge` renders 300 frames of the persistent open-iris charging smile with a blink and wink; `strip OUTDIR fluid` renders 150 frames of the softer purple resting flow. These use the real firmware renderer. `animation_test` checks a lifted curved lower lid without iris disappearance, changing outer pixels with stable iris interiors over 24 phases, round-panel containment, palette consistency and exact closed-lid cleanup with both liquid flow and charging enabled. Charging joy is a pose layer and yields to direct reactions and sleep; there is no new timed gesture playlist.

Current responsive style supersedes the fluid/mote experiments above: smooth motion uses the existing springs and clean rounded contours for all expressions. Charging has a quiet halo breath and a softer joyful pose; glassy interior shading shares the existing distance and shade palette. There are no per-pixel harmonic distortions or independent particles. `strip OUTDIR iphone-battery` renders the current icon at unknown, empty, low, half, near-full and full percentages, both plugged and unplugged. Battery regressions cover cached readout/SPI suppression, invalidation after percentage or connection changes, cleanup on exit, 28 mV unplug/replug, noise rejection and timer wrap. Rendering tests cover stable iris interiors, panel clipping and exact closed-lid cleanup. Firmware scene benchmarks include actual LCD transfer time; static battery hold skips redraw rather than claiming an artificial high display FPS.
