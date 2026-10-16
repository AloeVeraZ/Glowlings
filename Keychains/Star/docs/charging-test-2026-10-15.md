# Near-full charging detection: physical test, October 15, 2026

## What this release establishes

The charging face, liquid reservoir, unplug finish and always-on shortcut are
implemented. USB/charging classification remains an estimate from VBAT and is
**not fully fixed**. No threshold change or host connection override was used in
this measurement, and no diagnostic recorder is included in normal firmware.

| Stable window | Mean VBAT | Range | Standard deviation | Samples |
| --- | ---: | ---: | ---: | ---: |
| Connected, before unplug | 4.14925 V | 4.147–4.152 V | 1.13 mV | 24 |
| Disconnected | 4.13416 V | 4.132–4.137 V | 1.46 mV | 55 |
| Connected again | 4.17559 V | 4.175–4.177 V | 0.60 mV | 17 |

[Download the voltage capture](charging-voltage-2026-10-15.csv).

## Method

A temporary build kept the normal awake face running, preserving the production
16-sample ADC input and charging detector. An independent 64-sample trimmed
average was logged about every half second. Values are calibrated ADC readings
multiplied by the board's 3:1 VBAT divider, then rounded to 1 mV for storage.
Oversampling measures repeatability; it does not establish absolute calibration
or battery capacity.

The recorder saved a 360-row ring buffer to onboard NVS every two seconds. Its
reset recovery was verified before the user was asked to change the cable. It
remained armed for over eight minutes before the physical cycle; USB removal did
not stop recording. The recovered buffer spans about 197 seconds and includes
both cable changes. The host serial port was absent for about 35 seconds.

Host port polling and USB enumeration lag the electrical cable event. CSV labels
use a one-second transition guard around observed disappearance/reappearance.
The voltage rise and inferred charging occurred just before the COM port returned;
this is not evidence of charging being asserted while physically unplugged.

Stable windows exclude the transitions: 15–2 seconds before host disappearance,
3 seconds after disappearance through 2 seconds before return, and 2–12 seconds
after return. These means therefore differ from whole-phase means.

## Observed behavior

Unplugging initially reduced VBAT from about 4.148 V to 4.137–4.139 V. It then
relaxed toward 4.133 V. Reconnecting raised it by roughly 43 mV to 4.176 V.
The ordinary and independent ADC measurements agree on both changes.

Before removal the detector was already false despite a live USB connection.
It stayed false while disconnected and switched true on the reconnection rise,
remaining true through the end of the retained trace. Reconnection was directly
verified. This run cannot directly time an on-to-off indicator transition because
the initial state was already off. Exact latency cannot be inferred from COM
enumeration timestamps. After normal firmware restoration, a connected startup
again reported inferred charging false.

## Why the threshold was retained

An isolated desktop replay used the recorded production ADC readings and an
explicitly simulated initially-charging state. This differs from the actual
initial state and uses the available half-second samples rather than the original
100 ms detector cadence, so its timings are approximate.

| Small-sag threshold | Replay disconnect after host disappearance | Still-connected 12 mV load-step simulation |
| --- | ---: | --- |
| 15 mV, current firmware | About 13.2 seconds | Stayed charging |
| 10 mV, rejected experiment | About 2.1 seconds | Falsely disconnected 2.5 seconds after the load step |

The load simulation included ±3 mV ADC noise and a continuing positive charging
trend. Faster detection in this single capture would have traded one incorrect
state for another. The 10 mV experiment was not flashed or included in the release.

## Remaining hardware and estimation limits

GPIO1 measures battery terminal voltage, not USB VBUS, charge current or charger
termination. The existing board interface supplies no charger-status input to
this firmware. A steady near-full VBAT cannot reliably establish whether USB is
present; load changes and charger behavior can also imitate connection changes.
A cable event cannot be guaranteed at every percentage or after a steady-voltage
boot through this heuristic alone.

The percentage is a filtered single-cell voltage estimate. Cable changes do not
reset it; transitions settle before rate-limited corrections. Charging targets
are capped at 99% because terminal voltage alone cannot prove completion. This
capture does not validate remaining capacity, runtime or a cell-specific curve.

Normal production firmware was restored after capture and its written flash hash
was verified. Start-asleep behavior and critical battery protection remain active.
