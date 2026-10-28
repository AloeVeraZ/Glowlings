# Developing Star

[Star overview and upload instructions](../README.md) ·
[Preview tools and tests](../tools/preview/README.md) ·
[Animation inventory](../ANIMATIONS.md)

This guide describes the firmware brought into `main` on October 17, 2026.
The dated reports in this folder record earlier investigations and may describe
behavior that has since changed. Constants and implementation live in
[FaceConfig.h](../StarFace/FaceConfig.h) and [StarFace.ino](../StarFace/StarFace.ino).

## Hardware

Target: **Waveshare ESP32-S3-Touch-LCD-1.28**, with a 240 × 240 GC9A01A LCD,
CST816S touch controller and QMI8658 IMU. Board drivers are included in the sketch.

| Connection | GPIO |
| --- | --- |
| LCD DC / CS / clock / MOSI / MISO / reset | 8 / 9 / 10 / 11 / 12 / 14 |
| Backlight | 2 |
| I²C SDA / SCL | 6 / 7 |
| Touch reset / interrupt | 13 / 5 |
| QMI8658 INT2 | 3 |
| Battery ADC | 1 |

The drawing and gesture mappings assume the USB-C port is at the bottom.
`GAZE_IMU_ROTATION = 3` maps the sensor into the visual frame. `IMU_ROTATION`
separately preserves the recorded wake-gesture frame. Replacing the IMU requires
a compatible driver and explicit axis configuration; session zeroing does not
discover arbitrary sensor wiring.

## Architecture

| Source in `StarFace/` | Responsibility |
| --- | --- |
| `StarFace.ino` | Board startup, sensor task, input dispatch, battery persistence, LCD and sleep handshakes |
| `CreatureAnimator.h/.cpp` | Behavior states, authored reactions, charging pose, battery readout, wake and closing |
| `Eyes.h/.cpp` | Expression, gaze, blink and physics layers; safe placement on the round panel |
| `EyeShape.h`, `Expressions.h/.cpp` | Eye geometry, springs and the 15 expression bases |
| `EyeRenderer.h/.cpp` | RGB565 drawing, glow, pupil shapes, charging bubbles and battery reservoir |
| `Blinker.h`, `GazeController.h`, `IdleBehavior.h` | Blink timing, saccades, small glances and micro-expressions |
| `EnvironmentBehavior.h` | Debounced movement, pickup, carrying, settling and battery events |
| `MotionGestures.h`, `MotionHeading.h` | Fusion, tilt tracking, heading, centering, knocks, flicks and rocking |
| `LeftRightWake.h`, `ShakeDetector.h` | Directional sleep-wake verification and awake shake/twist detection |
| `TouchTracker.h`, `InteractionPolicy.h` | Finger reports, final-flick swipes, tap runs, mode gesture and awake deadlines |
| `BatteryState.h`, `ChargeFlow.h`, `BatteryGlyphs.h` | Voltage/gauge state, drop scheduling and cached percentage glyphs |
| `DEV_Config`, `LCD_1in28`, `CST816S`, `QMI8658` | Board interfaces; original driver notices are retained |

The hardware IMU task samples about every 3 ms independently of display time.
Failed/incomplete I²C transfers and repeated sensor timestamps are rejected.
Synchronized QMI8658 reads use sample locking and the AHB clock-gating command;
normal clock gating is restored before wake-on-motion/deep sleep. Gyro bias is
calibrated during a still cold-boot interval and retained across deep sleep.

## Gaze and centering

### Wake reference

Each real wake calls `WorldFollower::beginWakeSession()` and resets the manual
vertical zero. A still pose establishes the horizontal reference from at least
three valid readings spanning 150 ms. If the hand is moving, a valid, non-shaking
fused pose can establish it after the 300 ms maximum-wait interval. Invalid,
freefall or shaking readings cannot establish the reference. Until capture,
horizontal gaze stays centered.

Touch renewals, always-on toggles and brief sensor gaps do not start a new wake
session. Default up/down tracking uses flat or hanging gravity references until
a manual zero is requested.

### Manual zero

A short double tap near the same location calls `WorldFollower::requestCenter()`.
It adopts the current pose on **both axes**, immediately when the fused sample
is valid and no older than 250 ms. Otherwise it requests a fresh capture. It
preserves gyro calibration, clears the old finger target and retains the normal
double-click surprised reaction.

The vertical zero continues across flat/hanging posture changes until the next
wake. The horizontal reference remains subject to the periodic policy below.
A single held press and a dragged hold do not recenter. The second press of a
tap-then-hold is reserved for always-on; it does not also perform a manual zero.

### Periodic horizontal refresh

After a 15-second interval, a two-second steady hold can begin a one-second
reference blend. Motion, walking, interaction, closing or invalid reads defer it;
movement cancels an in-progress blend. An idle completion plays the 460 ms
`RECENTER` squeeze/pop/settle cue. Protected reactions and sleep retain priority.
This automatic refresh changes horizontal centering only.

The horizontal neutral zone is 4°, with full travel at 40° and 35 ms smoothing.
Vertical tracking uses an 8° neutral zone and 65 ms smoothing, reaching full
upward gaze at 50° and downward gaze at 25°. Heading retains the full relative
turn even when the visible gaze saturates. This is relative tracking, not a compass.

## Interaction and power

The [controls table](../README.md#controls) is the user-facing reference.
`AlwaysOnGesture` distinguishes short double taps from a tap followed by an
850 ms hold; the second press must start within 420 ms of the first release.
It toggles once per hold. Screen tap runs, not case knocks, trigger the protected
five-tap anger pause and ten-tap battery display.

`AwakeLimit` grants 30 seconds on wake. Confirmed touchscreen contact sets the
deadline to 15 seconds from that contact; motion and animation do not renew it.
Closing runs during the last 5.4 seconds. Confirmed touch can cancel closing,
including during the LCD/wake-on-motion handshake. Face-down shutdown is disabled.

Normal sleep turns off the LCD/backlight and enters deep sleep, with active-low
GPIO3 and GPIO5 wake inputs. An IMU interrupt first wakes the CPU with the display
dark; `LeftRightWakeCheck` must confirm the directional gesture before the face
opens. A pending confirmed touch takes priority during startup.

Directional confirmation uses a 350 ms sustained window, four strokes on both
required axes and a maximum 190 ms reversal gap. AY acceleration must dominate;
GZ rotation must dominate GY. The standard synthetic 3.5 Hz trace is recognized
in about 390 ms, excluding CPU, sensor, LCD and animation startup. This does not
establish universal walking rejection: supplied recordings lack timestamps and
assembly-level carrying tests remain necessary.

Always-on disables inactivity sleep and drowsiness. After 20 seconds without
touch or handling, it uses a 100 ms frame interval and a 0.65 backlight scale.
Touch/pickup restores active rendering; restart restores automatic sleep.
Critical low battery takes priority over all these interactions.

## Battery and charging

`BatterySupply` infers connection changes from voltage trends. Awake sampling
runs at 100 ms on the face, through closing and during the readout. Charging
classification and percentage estimation are separate; reconnecting does not
reset the percentage.

The estimator holds through 20-second connection/load transitions. Accepted
observations then correct the display by at most one percentage point per ten
seconds, with a one-point noise deadband. Battery-only upward correction waits
for a minute of settled observations. Charging voltage alone has a 99% target
cap because it cannot prove charge termination.

**Presentation rounds estimates of 95% and above to 100%.**
`BATTERY_SHOWS_FULL_PERCENT` affects the readout label and fill, not the stored
estimate or critical-voltage checks. Unknown charge shows `--%`. Readout entry
and exit each take 280 ms within a 4.2-second sequence.

The gauge, filters, connection and transition state are retained through deep
sleep and saved in one versioned NVS record. Immediate valid voltage drives the
3% critical-sleep and 8% recovery thresholds independently of the smoothed display.
Critical sleep disables gesture wake and checks charge every 60 seconds.

Charging eyes use a joyful green pose and scheduled drops from the USB-C edge.
On inferred unplug, no new cycles launch; existing drops finish before the
green fades. Blinks and winks hide the stream with the actual eye aperture.
The reservoir color changes on unplug confirmation without waiting for the
face's finish animation.

GPIO1 measures VBAT; charger status and USB presence are not independently
available. Steady-voltage startup and near-full unplug detection remain
ambiguous. The [October 15 report](charging-test-2026-10-15.md) records a physical
failure and why lowering the sag threshold caused false disconnects in replay.

During a USB upload, an independently verified host connection can be initialized
with the exact serial line `USB_POWER=1`. This checkpoints connection state while
preserving the gauge; it does not solve autonomous connection detection.
USB insertion can cancel visible closing, but is not a configured deep-sleep
wake source. Green eyes and a displayed 100% do not prove full charge.

## Rendering and performance

Eyes, curved lids, hearts and spirals are procedural distance-field shapes.
Springs blend shape parameters; pupils lead the larger eye movement. Compact
bright rims and soft halos share the current eye hue. Catch-lights are disabled
by default. Charging drops only evaluate the eye-distance blend when joining.

The single reused RGB565 framebuffer is composed off-screen. Changed rows are
transmitted as a DMA stream in chunks of at most 32 KB. The active frame interval
is 50 ms (**20 FPS cap**); always-on rest uses 100 ms. Static opaque battery
readouts skip redraw/transfer; charging readouts use cached liquid steps.

Earlier uncapped scene benchmarks recorded 22.1 FPS at rest and 18.7 FPS charging
including LCD transfer. Those isolate scene cost and are not a promise for the
current capped loop with input handling. Use `LOG_FPS` to measure the actual board.

Geometry tests check the rotated eye contours, circular screen bounds and a
10.8 px minimum inter-eye gap. Ordinary rendering uses blends of black, the eye
hue and pupil color, plus optional blush; the battery readout additionally permits
white figures. Desktop rendering checks cannot measure ESP32 throughput.

## Tuning

Most settings are in [FaceConfig.h](../StarFace/FaceConfig.h).

| Area | Main settings |
| --- | --- |
| Palette and halo | `EYE_COLOR`, `PUPIL_COLOR`, `GLOW`, `GLOW_STRENGTH`, `GLOW_SIZE`, `ANTI_ALIAS`, `CHEEK_BLUSH` |
| Layout | `EYE_WIDTH`, `EYE_HEIGHT`, `EYE_SPACING`, `MIN_EYE_GAP`, `EYE_OFFSET_Y`, `SAFE_MARGIN` |
| Pupil style | `PUPIL_SIZE`, `PUPIL_CORE`, `GLINT_SIZE`, `PUPIL_LIFE` |
| Movement and expressions | `ANIMATION_SPEED`, `EXPRESSION_BLEND_SPEED`, `EXPRESSION_INTENSITY`, `GAZE_SPEED`, `SQUASH_STRETCH`, `IDLE_LIVELINESS` |
| Gaze mapping | `GAZE_IMU_ROTATION`, `IMU_ROTATION`, `HORIZONTAL_GAZE_SIGN`, `HORIZONTAL_GAZE_FULL_DEG`, `HORIZONTAL_GAZE_NEUTRAL_DEG`, `HORIZONTAL_GAZE_SMOOTH_S`, `VERTICAL_GAZE_FULL_DEG`, `DOWNWARD_GAZE_FULL_DEG` |
| Recenter policy | `WAKE_ORIGIN_MAX_WAIT_MS`, `HORIZONTAL_RECENTER_INTERVAL_MS`, `HORIZONTAL_RECENTER_STILL_MS`, `HORIZONTAL_RECENTER_BLEND_MS`, `HORIZONTAL_RECENTER_GYRO_RAD_S` |
| Blinking | `BLINK_CLOSE_MS`, `BLINK_OPEN_MS`, `BLINK_MIN_MS`, `BLINK_MAX_MS`, `DOUBLE_BLINK_CHANCE` |
| Touch | `TOUCH_ENABLED`, `TOUCH_STUCK_MS`, `TAP_MAX_MS`, `TAP_MAX_MOVE_PX`, `TOUCH_CENTER_GAP_MS`, `SWIPE_MIN_PX`, `SWIPE_MIN_SPEED`, `HOLD_ANGER_START_MS` |
| Awake gestures | `KNOCK_MS2`, `FLICK_START_RAD_S`, `ROCK_SWINGS`, `UPSIDE_DOWN_MS`, `SHAKE_DIZZY_HOLD_MS`, `SHAKE_DIZZY_STRENGTH`, `TWIST_MAKES_DIZZY`, `SPIN_MAKES_DIZZY` |
| Directional wake | `DIRECTIONAL_WAKE_HOLD_MS`, `WAKE_DIRECTION_SWINGS`, `WAKE_REVERSAL_MIN_MS`, `WAKE_REVERSAL_MAX_MS`, `WAKE_AY_*`, `WAKE_GZ_*` |
| Sleep and resting mode | `AUTO_DEEP_SLEEP`, `START_ASLEEP`, `WAKE_AWAKE_MS`, `IDLE_SLEEP_MS`, `ALWAYS_ON_REST_MS`, `ALWAYS_ON_REST_FRAME_US`, `ALWAYS_ON_REST_BACKLIGHT` |
| Battery presentation/protection | `BATTERY_SHOWS_FULL_PERCENT`, `CRITICAL_BATTERY_PERCENT`, `BATTERY_RECOVER_PERCENT`, `BATTERY_RECHECK_S` |

`StarFace.ino` holds the hardware-facing `WOM_THRESHOLD_MG`, `FRAME_US`,
`LOG_FPS`, `LOG_SHAKE` and `RAW_IMU_SERIAL_ONLY`. Change motion-wake thresholds
against recorded traces and then verify on hardware. The older generic wake
checker lives only in `tools/preview/legacy_shake_wake.h` for regression comparison.

## Driving the eyes in code

```cpp
eyes.setExpression(HAPPY);
eyes.setExpression(ANGRY, 0.5f);
eyes.setExpressionMix(FOCUSED, ANGRY, 0.7f);
eyes.lookAt(0.6f, -0.2f);     // normalized x right, y down
eyes.lookAtPoint(200, 120);   // screen pixels
eyes.blink();
eyes.wink(0);
eyes.lookAround();
eyes.setIdle(false);
eyes.setAutoBlink(false);
```

The behavior layer can replace an expression when a new event occurs. With
idle behavior enabled, a one-off `lookAt()` yields after 2.5 seconds; update it
each frame for continuous tracking. `showBatteryLevel(percent)` in the sketch
dispatches the readout through `CreatureAnimator`.

## Diagnostics and validation

Serial runs at **115200 baud**. `LOG_SHAKE` reports motion events and strength;
directional-wake logs include AY/GZ energy shares and the GZ-to-GY ratio.
`RAW_IMU_SERIAL_ONLY = true` enables native signed `AX,AY,AZ,GX,GY,GZ` counts at
most every 40 ms. ROM boot output can precede sketch logs.

[The desktop suite](../tools/preview/README.md#tests) covers sensor reads,
gestures, centering, timing, battery and geometry. Run it after changing the
firmware. Check physical direction, touch during closing, pants/bag carrying,
quiet always-on, and plugged/unplugged behavior separately on the board.

The October 16 [review](review-2026-10-16.md) and
[charging report](charging-test-2026-10-15.md) are historical evidence. Their
build sizes, pass counts and upload statements belong to those recorded runs.
