#!/bin/sh
# Runs the desktop checks against the real sketch sources:
#   shake_test - simulated shakes, flicks, walking, running and knocks through
#                the sketch's shake/twist detectors (awake and wake-from-sleep)
#   motion_test - touch-free gestures: knocks (at the sensor's real rate and
#                filter), tilt flicks, rocking, against walking/running/shaking
#   touch_test - finger tracking, taps, flick direction, holds, lost lift reports
#   soak_test  - hours of random input through the animator and renderer,
#                with address/undefined-behaviour sanitizers
set -e
cd "$(dirname "$0")"
SRC=../../StarFace
OUT=${TMPDIR:-/tmp}
g++ -std=gnu++17 -O2 -Wall -I$SRC shake_test.cpp -o "$OUT/starface_shake_test"
"$OUT/starface_shake_test"

g++ -std=gnu++17 -O2 -Wall -I$SRC left_right_wake_test.cpp -o "$OUT/starface_left_right_wake_test"
"$OUT/starface_left_right_wake_test"
g++ -std=gnu++17 -O2 -Wall -I$SRC motion_test.cpp -o "$OUT/starface_motion_test"
"$OUT/starface_motion_test"
g++ -std=gnu++17 -O2 -Wall -I$SRC periodic_gaze_test.cpp -o "$OUT/starface_periodic_gaze_test"
"$OUT/starface_periodic_gaze_test"

g++ -std=gnu++17 -O2 -Wall -I$SRC wake_origin_test.cpp -o "$OUT/starface_wake_origin_test"
"$OUT/starface_wake_origin_test"

g++ -std=gnu++17 -O2 -Wall -I$SRC horizontal_tracking_test.cpp -o "$OUT/starface_horizontal_tracking_test"
"$OUT/starface_horizontal_tracking_test"
g++ -std=gnu++17 -O2 -Wall -I$SRC orientation_loop_test.cpp -o "$OUT/starface_orientation_loop_test"
"$OUT/starface_orientation_loop_test"
g++ -std=gnu++17 -O2 -Wall -I$SRC touch_test.cpp -o "$OUT/starface_touch_test"
"$OUT/starface_touch_test"
g++ -std=gnu++17 -O2 -Wall -Itouch_stubs -I$SRC touch_irq_test.cpp $SRC/CST816S.cpp -o "$OUT/starface_touch_irq_test"
"$OUT/starface_touch_irq_test"
g++ -std=gnu++17 -O2 -Wall -Istubs -I$SRC always_on_test.cpp $SRC/EyeRenderer.cpp $SRC/Eyes.cpp $SRC/Expressions.cpp $SRC/CreatureAnimator.cpp -o "$OUT/starface_always_on_test"
"$OUT/starface_always_on_test"

g++ -std=gnu++17 -O2 -Wall -Istubs -I$SRC battery_test.cpp $SRC/EyeRenderer.cpp $SRC/Eyes.cpp $SRC/Expressions.cpp $SRC/CreatureAnimator.cpp -o "$OUT/starface_battery_test"
"$OUT/starface_battery_test"
g++ -std=gnu++17 -O2 -Wall -Istubs -I$SRC animation_test.cpp $SRC/EyeRenderer.cpp $SRC/Eyes.cpp $SRC/Expressions.cpp $SRC/CreatureAnimator.cpp -o "$OUT/starface_animation_test"
"$OUT/starface_animation_test"
g++ -std=gnu++17 -O2 -Wall -Istubs -I$SRC motion_sleep_test.cpp $SRC/EyeRenderer.cpp $SRC/Eyes.cpp $SRC/Expressions.cpp $SRC/CreatureAnimator.cpp -o "$OUT/starface_motion_sleep_test"
"$OUT/starface_motion_sleep_test"

g++ -std=gnu++17 -O2 -Wall -Istubs -I$SRC gaze_layout_test.cpp $SRC/EyeRenderer.cpp $SRC/Eyes.cpp $SRC/Expressions.cpp $SRC/CreatureAnimator.cpp -o "$OUT/starface_gaze_layout_test"
"$OUT/starface_gaze_layout_test"
g++ -std=gnu++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -Istubs -I$SRC soak_test.cpp $SRC/EyeRenderer.cpp $SRC/Eyes.cpp $SRC/Expressions.cpp $SRC/CreatureAnimator.cpp -o "$OUT/starface_soak_test"
"$OUT/starface_soak_test"
