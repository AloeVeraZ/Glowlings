#pragma once
#include <stdint.h>

// ============================================================================
//  Star Face tuning: everything you are likely to want to change is here.
//  Eye sizes are fractions of the screen; times are ms; motion is m/s^2.
// ============================================================================

// ---- Serial diagnostics ------------------------------------------------------
// Temporary measurement build: 115200 baud, labeled AX,AY,AZ,GX,GY,GZ rows of
// native signed accel/gyro register counts. Keeps the face awake for capture;
// critical battery shutdown still applies. Set false to restore normal sleep.
static constexpr bool RAW_IMU_SERIAL_ONLY = false;
static constexpr uint32_t RAW_IMU_PRINT_MS = 40; // at most 25 rows per second

// ============================================================================
//  EYES: the parameters you are most likely to want to change.
// ============================================================================

// ---- Colours ----------------------------------------------------------------
// Just purple and black by default: the eye is a glowing EYE_COLOR, the iris
// is PUPIL_COLOR (black, or any second colour you like), and the
// catch-lights are disabled for a simple graphic look. Edges are anti-aliased and the glow fades out (each such pixel is a
// blend of these two colours and black, nothing else), which is what makes
// the shapes look like smooth glowing vector art instead of pixel steps.
// 24-bit 0xRRGGBB values (shown as the nearest RGB565 colour on the panel).
static constexpr uint32_t EYE_COLOR = 0xB04DFF;     // brighter violet; battery still fades through yellow to red
static constexpr uint32_t BATTERY_YELLOW_COLOR = 0xFFD43B;
static constexpr uint32_t BATTERY_RED_COLOR = 0xFF3038;
static constexpr uint32_t BATTERY_CHARGING_COLOR = 0x38ED86;
static constexpr int CHARGE_BUBBLES = 3;            // per eye: six staggered drops keep the ESP32 responsive
static constexpr uint32_t PUPIL_COLOR = 0x000000;   // the big irises, hearts and spirals: black (purple and black only)
static constexpr bool GLOW = true;                  // a soft halo of EYE_COLOR around the eyes
static constexpr float GLOW_STRENGTH = 0.95f;       // halo brightness at the eye's edge
static constexpr float GLOW_SIZE = 0.075f;          // compact bright edge plus a wider soft purple halo
static constexpr bool ANTI_ALIAS = true;            // smooth vector edges (false: hard pixel edges)
// Rosy cheeks under the eyes: a small blush at rest that grows when it is
// happy, petted or in love, and fades when it is sad or cross (the one extra
// colour; set CHEEK_BLUSH = false for strictly two colours).
static constexpr bool CHEEK_BLUSH = false;
static constexpr uint32_t CHEEK_COLOR = 0xFF7EB0;

// ---- Eye size and layout, as fractions of the screen -------------------------
// Everything scales with the display: sizes are fractions of the smaller
// screen side, positions are relative to the screen centre.
static constexpr float EYE_WIDTH = 0.43f;         // width of one oval eye at rest
static constexpr float EYE_HEIGHT = 0.52f;        // larger resting silhouette, with room for animated growth
static constexpr float EYE_SPACING = 0.49f;       // centre-to-centre distance between the eyes
static constexpr float MIN_EYE_GAP = .045f;      // clear space between the eye outlines, fraction of screen
static constexpr float EYE_OFFSET_Y = 0.03f;      // whole face up (-) or down (+): a little low reads younger, cuter
static constexpr float PUPIL_SIZE = 0.72f;        // iris radius, as a fraction of the eye's half width (big = friendly)
static constexpr float PUPIL_CORE = 0.0f;         // black centre of the iris, as a fraction of it (0 = none, two colours only)
static constexpr float GLINT_SIZE = 0.0f;         // solid black pupils: no catch-light dots
static constexpr float PUPIL_LIFE = 1.0f;         // pupil dilation and fixation tremor: 0 off .. 1.5 lively

// ---- Movement limits ---------------------------------------------------------
static constexpr float MAX_PUPIL_MOVE = 0.85f;    // 0..1: how far an iris may roam inside its eye
static constexpr float MAX_EYE_MOVE_X = 0.105f;   // the eye shapes travel after the pupils
static constexpr float MAX_EYE_MOVE_Y = 0.085f;
static constexpr float MAX_EXPRESSION_EXPANSION = 1.18f; // horizontal cap keeps the two eyes distinct
static constexpr float MAX_EYE_HEIGHT_EXPANSION = 1.50f;
static constexpr float ACTIVE_EYE_WIDTH_GROW = .04f;
static constexpr float ACTIVE_EYE_HEIGHT_GROW = .30f; // use more of the round panel while looking/reacting

// ---- Animation -----------------------------------------------------------------
static constexpr float ANIMATION_SPEED = 1.20f;  // quicker springs, while scripted time stays frame-independent
static constexpr float EXPRESSION_INTENSITY = 1.10f; // stronger expressions, with the same eye design
static constexpr float EXPRESSION_BLEND_SPEED = 1.20f;
static constexpr uint16_t BLINK_CLOSE_MS = 55;
static constexpr uint16_t BLINK_OPEN_MS = 140;
static constexpr uint32_t BLINK_MIN_MS = 2200;    // blink frequency: time between blinks
static constexpr uint32_t BLINK_MAX_MS = 6500;
static constexpr float DOUBLE_BLINK_CHANCE = 0.18f;
static constexpr float GAZE_SPEED = 1.35f;
static constexpr float IDLE_LIVELINESS = 1.05f;  // unhurried observation between real events
static constexpr float SQUASH_STRETCH = 1.50f;
static constexpr float ANGER_COOLDOWN_S = 4.0f;   // lids stay a little grumpy this long after anger

// ---- Display ------------------------------------------------------------------
static constexpr int SCREEN_WIDTH = 240;
static constexpr int SCREEN_HEIGHT = 240;
// A round panel hides its corners: the eyes are then kept inside a circle.
static constexpr bool SCREEN_IS_ROUND = true;
static constexpr float SAFE_MARGIN = 0.04f;       // keep the eyes this far (of screen) from the edge
// The eyes sit level with the board: their line is parallel to the edge with
// the USB-C port. If the enclosure holds the board turned, turn the face to
// match here (degrees, clockwise).
static constexpr float FACE_ANGLE_DEG = 0.0f;

// ---- Shake & motion sensitivity (gravity-free acceleration, m/s^2; 9.8 = 1 g) --
// Lower SHAKE_DIZZY_STRENGTH / SHAKE_DIZZY_HOLD_MS if shaking feels too hard,
// raise them if walking or bumps set it off. Set LOG_SHAKE in StarFace.ino to
// print each stroke's strength over serial while you tune.
static constexpr float SHAKE_STROKE_MS2 = 11.0f;  // one hard stroke: the eyes slosh, two back and forth startle it
static constexpr uint32_t SHAKE_GAP_MS = 500;     // longest pause between strokes of one shake
static constexpr float SHAKE_NOISE_MS2 = 3.0f;    // motion below this never rattles the eyes
static constexpr float SHAKE_FULL_MS2 = 7.0f;     // average shake strength for a full-strength rattle
// The dizzy spell needs a real shake kept up: back and forth, at least this
// strong on average, for SHAKE_DIZZY_HOLD_MS without stopping (dips shorter
// than SHAKE_DIZZY_DROPOUT_MS are forgiven; a longer pause starts over).
static constexpr float SHAKE_DIZZY_STRENGTH = 2.5f;  // moderate shakes reach the dizzy reaction
static constexpr uint32_t SHAKE_DIZZY_HOLD_MS = 1000; // about one second of recognized shaking
static constexpr uint32_t SHAKE_DIZZY_DROPOUT_MS = 500; // tolerate uneven strokes and brief pauses
static constexpr uint8_t SHAKE_DIZZY_SWINGS = 4;     // four alternating strokes, rather than six
// Asleep it is much harder to wake: it takes a steady shake, kept up for
// SHAKE_WAKE_HOLD_MS (short dips under SHAKE_WAKE_DROPOUT_MS are forgiven).
static constexpr float SHAKE_WAKE_STRENGTH = 3.5f;   // average shake strength to count as shaking
static constexpr float SHAKE_WAKE_STROKE_MS2 = 11.0f; // one stroke while waking it
static constexpr uint32_t SHAKE_WAKE_HOLD_MS = 1500; // 25% shorter than the previous two seconds
static constexpr uint32_t SHAKE_WAKE_DROPOUT_MS = 350;
static constexpr float SHAKE_GYRO_RAD_S = 3.0f;   // while asleep, rotation faster than this counts toward the wake shake
static constexpr uint8_t SHAKE_WAKE_SWINGS = 6; // actual alternating strong strokes, not footsteps
// Recorded intended left/right wake: AY acceleration and GZ rotation dominate.
// Recorded carried forward/back movement: GY rotation dominates instead.
static constexpr float WAKE_AY_ENERGY_SHARE = .50f;
static constexpr float WAKE_GZ_ENERGY_SHARE = .42f;
static constexpr float WAKE_GZ_OVER_GY = 1.35f;
static constexpr float WAKE_AY_RMS_MS2 = 3.8f;
static constexpr float WAKE_GZ_RMS_RAD_S = 2.5f;
static constexpr float WAKE_AY_STROKE_MS2 = 7.0f;
static constexpr float WAKE_GZ_REVERSAL_RAD_S = 1.8f;
// A short, brisk directional gesture wakes sooner without admitting the slow
// pendulum swing of a star hanging from a bag/pants. Legacy ShakeWakeCheck
// keeps its separate SHAKE_WAKE_* thresholds; only LeftRightWake uses these.
static constexpr uint32_t DIRECTIONAL_WAKE_HOLD_MS = 350;
static constexpr uint32_t WAKE_DIRECTION_SETTLE_MS = 80;
static constexpr uint32_t WAKE_REVERSAL_MIN_MS = 40; // debounce impact ringing
static constexpr uint32_t WAKE_REVERSAL_MAX_MS = 190; // >~2.6 Hz back-and-forth
static constexpr uint8_t WAKE_DIRECTION_SWINGS = 4;
// Turning, twisting or spinning it never makes it dizzy unless these are on:
// awake, only a hard, rapid back-and-forth shake does.
static constexpr bool TWIST_MAKES_DIZZY = false;  // three quick twists back and forth
static constexpr bool SPIN_MAKES_DIZZY = false;   // spinning it around on the spot (SPIN_TURNS_FOR_DIZZY)
static constexpr float BUMP_MS2 = 9.0f;           // a single knock: recoil and a startled look
static constexpr float PICKUP_MS2 = 2.2f;         // picked up / moved: ends a nap

// ---- Following the world (motion sensor) --------------------------------------
// Horizontal gaze looks outward, opposite the previous low-side response; the
// face rolls to stay level when the star is turned. A held tilt retains its
// direction. Acceleration and angular velocity must use the same right-handed
// rotation before fusion; a gyro X turn tilts around X, rather than along X.
static constexpr uint8_t IMU_ROTATION = 0;
// USB at bottom: reported left/right tilts drove vertical gaze with rotation 2.
// A further quarter turn aligns canonical Y with display X and -X with Y.
// Apply this to every visual motion cue; recorded shake axes stay independent.
static constexpr uint8_t GAZE_IMU_ROTATION = 3;
// Measured on this user's board: screen-up gravity is positive in the
// canonical gesture Z frame. This is mounting geometry, not a changing pose.
static constexpr float GAZE_FLAT_UP_SIGN = 1.0f;
static constexpr float VERTICAL_GAZE_FULL_DEG = 50.0f; // full upward look relative to the resting pose
static constexpr float DOWNWARD_GAZE_FULL_DEG = 25.0f; // full downward look at the previous halfway point
static constexpr float HORIZONTAL_GAZE_SIGN = -1.0f; // invert left/right gaze after fusion, not sensor axes
static constexpr float HORIZONTAL_GAZE_FULL_DEG = 30.0f;
static constexpr float HORIZONTAL_GAZE_NEUTRAL_DEG = 4.0f;
// Enclosure/viewing trim: the measured normal hanging pose leans about 5-6
// degrees left in the visual gravity frame. Keep the table reference separate.
static constexpr float HORIZONTAL_TABLE_CENTER_DEG = 0.0f;
static constexpr float HORIZONTAL_HANGING_CENTER_DEG = 5.0f;
// Canonical-frame stationary gyro offset measured on this board (3000 fresh
// samples). Horizontal tracking uses this ONLY if startup calibration fails;
// vertical fusion and gesture/wake detectors keep their existing inputs.
static constexpr float HORIZONTAL_GYRO_BIAS_RAD_S[3] = {.0598092f,-.0189190f,-.0059847f};
static constexpr float HORIZONTAL_GAZE_SMOOTH_S = .035f;
static constexpr uint32_t HORIZONTAL_RECENTER_INTERVAL_MS = 15000; // periodic origin refresh, only while steady
static constexpr uint32_t HORIZONTAL_RECENTER_STILL_MS = 2000;
static constexpr uint32_t HORIZONTAL_RECENTER_BLEND_MS = 1000;     // ease into the new center
static constexpr float HORIZONTAL_RECENTER_GYRO_RAD_S = .04f;

static constexpr float GAZE_NEUTRAL_ZONE_DEG = 8.0f; // absorbs small hand/enclosure lean around centre
static constexpr uint32_t GAZE_POSE_SETTLE_MS = 120; // pose consistency, not a motion-free interval
static constexpr bool FACE_STAYS_LEVEL = false;   // true: roll the face to stay level with the ground
                                                  // (false: the eyes stay square to the board and its USB port)
static constexpr float FACE_ROLL_MAX = 0.75f;     // radians (~43 degrees), so upside down still reads as upside down
static constexpr float SWING_GAZE = 0.08f;        // softer eye reflex; deliberate turns still track
static constexpr float HORIZONTAL_SWING_GAZE = .12f;
static constexpr float SPIN_TURNS_FOR_DIZZY = 1.5f; // spinning it around on the spot this many turns: dizzy
static constexpr uint32_t FREEFALL_MS = 90;       // weightless this long (tossed or dropped): startled
static constexpr float STEP_MS2 = 1.2f;           // a footstep bounce at least this strong (walking with it)
static constexpr bool WALKING_KEEPS_AWAKE = false; // steps animate it while awake, without resetting sleep
static constexpr float HOT_C = 38.0f;             // the IMU chip warmer than this (pocket, hand, sun): lazy and yawny

// ---- Motion gestures (everything also works without the touch screen) -------
// Knock on the case 1/2/3/4 times = tap the screen 1/2/3/4 times; tip it one
// way and straight back = swipe that way; rock it gently = pet it; hold it
// upside down = hold a finger on it (it gets angrier the longer it lasts);
// lay it face down = put it to sleep.
static constexpr float KNOCK_MS2 = 3.0f;          // a knock is a jolt at least this sharp (lower = more sensitive)
static constexpr float KNOCK_QUIET_MS2 = 1.5f;    // ...on a star moving less than this just before
static constexpr uint32_t KNOCK_QUIET_BEFORE_MS = 250; // ...for this long (walking never is)
static constexpr float KNOCK_MAX_SPIN_RAD_S = 1.5f; // a swing is not a knock
static constexpr uint32_t KNOCK_RING_MS = 110;    // a knock must settle within this
static constexpr uint32_t KNOCK_GAP_MS = 450;     // longest pause between knocks of one run
static constexpr float FLICK_START_RAD_S = 2.0f;  // a flick starts with a turn at least this quick...
static constexpr float FLICK_MIN_TILT_MS2 = 3.3f; // ...tips it at least ~20 degrees...
static constexpr float FLICK_RETURN_MS2 = 2.0f;   // ...and comes back to within ~12 degrees...
static constexpr uint32_t FLICK_MAX_MS = 800;     // ...within this time
static constexpr uint32_t ROCK_MIN_HALF_MS = 360; // one rocking swing takes this long or more
static constexpr uint32_t ROCK_MAX_HALF_MS = 1400;
static constexpr uint8_t ROCK_SWINGS = 3;         // swings in a row before it feels petted
static constexpr float ROCK_MAX_BOUNCE_MS2 = 1.6f; // jostling above this (walking) is not rocking
static constexpr uint32_t UPSIDE_DOWN_MS = 500;   // held upside down this long annoys it
static constexpr uint32_t FACE_DOWN_SLEEP_MS = 0; // orientation never forces shutdown during interaction

// ---- Touch -------------------------------------------------------------------------
// While a finger is down the eyes follow it. Releasing after a quick flick is
// a swipe in the flick's direction; holding on makes it more and more angry.
static constexpr uint32_t TAP_MAX_MS = 350;       // a tap is shorter than this...
static constexpr float TAP_MAX_MOVE_PX = 22;      // ...and moves less than this
static constexpr float SWIPE_MIN_PX = 35;         // a swipe flicks at least this far...
static constexpr float SWIPE_MIN_SPEED = 250;     // ...at least this fast (px/s) at the end
static constexpr uint32_t SWIPE_MAX_SHORT_MS = 700; // or: a press this short that moved far
static constexpr uint32_t HOLD_ANGER_START_MS = 1200; // holding longer than this annoys it...
static constexpr uint32_t HOLD_ANGER_FULL_MS = 4500;  // ...and by this long it is furious
static constexpr uint32_t TOUCH_RELEASE_TIMEOUT_MS = 250; // no report this long = finger lifted
// Set TOUCH_ENABLED = false if the screen sits behind a cover that blocks
// touch: the touch chip is then ignored and only motion wakes it from sleep.
static constexpr bool TOUCH_ENABLED = true;
// Zero allows a held finger to keep renewing sleep. Set a nonzero limit to
// ignore a continuously reported press after that many milliseconds.
static constexpr uint32_t TOUCH_STUCK_MS = 0;
static constexpr uint32_t TOUCH_TAP_RUN_GAP_MS = 650;
static constexpr uint32_t TOUCH_ANGER_PAUSE_MS = 3500;
static constexpr uint8_t TOUCH_BATTERY_TAPS = 10;

// ---- Sleep & power ---------------------------------------------------------------
// After IDLE_SLEEP_MS without confirmed touchscreen contact the eyes close and fade to
// black, then the screen and ESP32 power down (deep sleep). A touch or
// sustained shaking wakes it. Every touch restarts 15 seconds; visual motion
// reactions never extend the initial 30-second untouched wake window.
// Set AUTO_DEEP_SLEEP = false to keep the screen on instead: it then naps
// with dimmed, closed eyes after IDLE_NAP_MS.
static constexpr bool AUTO_DEEP_SLEEP = true;
static constexpr uint32_t IDLE_SLEEP_MS = 15000;  // power-save timeout when AUTO_DEEP_SLEEP
static constexpr uint32_t WAKE_AWAKE_MS = 30000; // initial wake window; interaction can extend it
static constexpr uint8_t TOUCH_WAKE_TAPS = 1;    // the first touchscreen press wakes it
static constexpr uint32_t TOUCH_WAKE_WINDOW_MS = 3000;
static constexpr uint32_t IDLE_NAP_MS = 45000;    // dim-nap timeout when !AUTO_DEEP_SLEEP
static constexpr uint8_t BACKLIGHT_PERCENT = 70;
static constexpr uint8_t NAP_BACKLIGHT_PERCENT = 14;
static constexpr bool START_ASLEEP = true;        // power-on/reset leaves the display dark
static constexpr uint32_t BATTERY_CHECK_MS = 10000;
static constexpr uint32_t POWER_CHECK_MS = 1000; // notice USB insertion/removal while awake
static constexpr uint32_t BATTERY_LIVE_POWER_CHECK_MS = 100; // face, closing sequence and battery readout
// VBAT trend inference: the charger status signal is not connected to a GPIO.
static constexpr float CHARGE_RISE_VOLTS = .08f;
static constexpr float CHARGE_TREND_VOLTS = .025f;
static constexpr float CHARGE_FALL_VOLTS = .04f;        // abrupt unplug step: two raw polls
static constexpr float CHARGE_SMALL_FALL_VOLTS = .015f; // near-full unplug: smoothed sag below the charging peak
static constexpr uint32_t CHARGE_SMOOTH_MS = 1000;      // smoothing for the near-full unplug check
static constexpr uint32_t CHARGE_SAG_CONFIRM_MS = 1000; // the sag must hold this long
// A plugged-in cell holds or gains voltage. The charging reference may only
// creep down this fast, so an unplugged cell's slow relaxation can't drag it along.
static constexpr float CHARGE_REF_SAG_VOLTS_PER_S = .00003f;
static constexpr uint32_t CHARGE_TREND_MS = 10000;
static constexpr uint32_t CHARGE_TREND_MAX_MS = 120000; // preserve a slow charging rise across short windows
static constexpr uint32_t BATTERY_TRANSITION_MS = 20000; // hold gauge through cable/load transients
static constexpr uint32_t BATTERY_RELAX_MS = 60000; // wait before upward battery-only correction
static constexpr uint32_t BATTERY_PERCENT_STEP_MS = 10000; // maximum observed estimate change: 1%/10 s
static constexpr int BATTERY_PERCENT_DEADBAND = 1;
static constexpr uint32_t BATTERY_RECHECK_S = 60; // critical sleep: check for charging once a minute
static constexpr int CRITICAL_BATTERY_PERCENT = 3;
static constexpr int BATTERY_RECOVER_PERCENT = 8; // avoids repeatedly waking near empty

// ---- Derived (do not edit) -------------------------------------------------------
static constexpr float SCREEN_MIN_SIDE = float(SCREEN_WIDTH < SCREEN_HEIGHT ? SCREEN_WIDTH : SCREEN_HEIGHT);
static constexpr float SCREEN_CX = (SCREEN_WIDTH - 1) * 0.5f;    // pixel centres are whole numbers
static constexpr float SCREEN_CY = (SCREEN_HEIGHT - 1) * 0.5f;
static constexpr float SCREEN_RADIUS = SCREEN_MIN_SIDE * 0.5f;
static constexpr float SAFE_RADIUS = SCREEN_RADIUS - SAFE_MARGIN * SCREEN_MIN_SIDE;
static constexpr float EYE_HALF_WIDTH = EYE_WIDTH * SCREEN_MIN_SIDE * 0.5f;   // px
static constexpr float EYE_HALF_HEIGHT = EYE_HEIGHT * SCREEN_MIN_SIDE * 0.5f; // px
static constexpr float EYE_HALF_SPACING = EYE_SPACING * SCREEN_MIN_SIDE * 0.5f;
static constexpr float EYE_CENTER_X = SCREEN_CX;
static constexpr float EYE_CENTER_Y = SCREEN_CY + EYE_OFFSET_Y * SCREEN_MIN_SIDE;
