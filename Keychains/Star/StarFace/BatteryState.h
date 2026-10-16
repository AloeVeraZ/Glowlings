#pragma once
#include <stdint.h>
#include "FaceConfig.h"

// GPIO1 measures VBAT through a 3:1 divider, not a coulomb-counting fuel gauge.
// Charging changes terminal voltage; it does not instantly add that much charge.
class BatteryState {
 public:
  enum Stage : uint8_t { NORMAL, TIRED, LOW_CHARGE, VERY_LOW, EXHAUSTED, CRITICAL };
  Stage stage = NORMAL;
  int percent = -1;
  uint8_t fatigueStep = 0; // one additional tired pose for each 10% drained
  bool charging = false; // inferred from VBAT trend; no wired charger STAT

  static bool mustRemainAsleep(int reading, float volts, bool locked, bool powered = false) {
    if (powered) return false;
    if (reading >= 0 && reading <= CRITICAL_BATTERY_PERCENT) return true;
    (void)volts; // VBAT cannot establish whether USB is connected
    return locked && (reading < 0 || reading < BATTERY_RECOVER_PERCENT);
  }

  static int estimate(float volts) {
    if (volts < 2.8f || volts > 4.4f) return -1;
    const float v[] = {3.30f, 3.55f, 3.70f, 3.78f, 3.86f, 3.95f, 4.08f, 4.20f};
    const int p[] = {0, 5, 15, 30, 50, 70, 90, 100};
    if (volts <= v[0]) return 0;
    for (int i = 1; i < 8; ++i)
      if (volts <= v[i]) return p[i-1] + int((p[i]-p[i-1]) * (volts-v[i-1]) / (v[i]-v[i-1]));
    return 100;
  }

  void update(int reading, bool powered = false) {
    charging = powered;
    percent = reading < 0 ? -1 : (reading > 100 ? 100 : reading);
    if (reading < 0) { stage = NORMAL; fatigueStep = 0; return; }
    uint8_t step = uint8_t((100 - percent) / 10);
    if (step >= fatigueStep || percent > 100 - fatigueStep * 10 + 2) fatigueStep = step;
    Stage next = reading <= 3 ? CRITICAL : reading <= 5 ? EXHAUSTED : reading <= 10 ? VERY_LOW
               : reading <= 25 ? LOW_CHARGE : reading <= 50 ? TIRED : NORMAL;
    // Falling charge takes effect immediately; recovery has a 2% deadband.
    const int boundary[] = {100, 50, 25, 10, 5, 3};
    if (next >= stage || reading > boundary[stage] + 2) stage = next;
  }

  float fatigue() const {
    const float f[] = {0, .07f, .15f, .24f, .34f, .45f, .57f, .69f, .81f, .91f, 1};
    return charging ? 0.0f : stage == CRITICAL ? 1.0f : f[fatigueStep];
  }
  float sadness() const {
    const float s[] = {0, .01f, .03f, .07f, .13f, .22f, .34f, .48f, .64f, .80f, 1};
    return charging ? 0.0f : stage == CRITICAL ? 1.0f : s[fatigueStep];
  }
  uint32_t color() const {
    if (charging) return BATTERY_CHARGING_COLOR;
    if (percent < 0) return EYE_COLOR;
    // Interpolate every percent: purple at full, yellow at half, red at 10%.
    if (percent >= 50) return blend(EYE_COLOR, BATTERY_YELLOW_COLOR, (100 - percent) / 50.0f);
    return blend(BATTERY_YELLOW_COLOR, BATTERY_RED_COLOR, (50 - (percent < 10 ? 10 : percent)) / 40.0f);
  }

 private:
  static uint32_t blend(uint32_t a, uint32_t b, float t) {
    uint32_t result = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
      float x = float((a >> shift) & 255), y = float((b >> shift) & 255);
      result |= uint32_t(x + (y - x) * t + .5f) << shift;
    }
    return result;
  }
};

// Infer charging from a sustained rise and disconnection from a sustained drop.
// This is a heuristic, not a USB/charger status pin (the board's USB goes
// through a CH343 bridge, so the ESP32 never sees VBUS). Connection detection and
// the voltage-based percentage estimator are separate: a cable event must never
// replace the displayed percentage. There is no current/capacity measurement.
class BatterySupply {
 public:
  struct Snapshot {
    uint32_t version = 0;
    float baseline = 0, filtered = 0, peak = 0;
    int32_t percent = -1;
    uint32_t settleMs = 0, stableMs = 0, stepMs = 0;
    uint8_t charging = 0;
  };
  static constexpr uint32_t SNAPSHOT_VERSION = 0x42544734;
  bool charging = false;
  int percent = -1;
  float volts = 0;
  float peakVolts = 0;
  float filteredVolts = 0, measuredVolts = 0;
  int rawPercent = -1; // immediate voltage estimate for critical shutdown, independent of the UI

  void restore(float previousVolts, int previousPercent, bool wasCharging = false, float previousPeak = 0) {
    if (!(previousVolts >= 2.8f && previousVolts <= 4.4f) || previousPercent < 0 || previousPercent > 100) return;
    volts = previousVolts;
    percent = previousPercent;
    filteredVolts = previousVolts;
    initialized = true;
    charging = wasCharging;
    smoothing = false;
    peakVolts = previousPeak >= previousVolts && previousPeak <= 4.4f ? previousPeak : previousVolts;
    settleMs = BATTERY_TRANSITION_MS;
  }

  bool restore(const Snapshot &s) {
    if ((s.version != SNAPSHOT_VERSION && s.version != 0x42544733) || !validVoltage(s.baseline) || !validVoltage(s.filtered) ||
        !(s.peak == 0 || validVoltage(s.peak)) || s.percent < 0 || s.percent > 100 || s.charging > 1 ||
        s.settleMs > BATTERY_TRANSITION_MS || s.stableMs > BATTERY_RELAX_MS || s.stepMs >= BATTERY_PERCENT_STEP_MS) return false;
    volts = s.baseline; filteredVolts = s.filtered; peakVolts = s.peak;
    // V3 retained an all-time peak. Migrate its gauge without treating old CV
    // voltage as a fresh cable-removal step on every wake.
    if (s.version == 0x42544733 && s.charging && peakVolts > filteredVolts)
      peakVolts = filteredVolts;
    percent = s.percent; charging = s.charging; initialized = true;
    smoothing = false;
    settleMs = s.settleMs; stableMs = s.stableMs; stepMs = s.stepMs;
    return true;
  }

  Snapshot snapshot() const {
    Snapshot s{};
    if (!initialized || percent < 0 || !validVoltage(volts) || !validVoltage(filteredVolts)) return s;
    s.version = SNAPSHOT_VERSION; s.baseline = volts; s.filtered = filteredVolts;
    // Retain the last observation, not a historical maximum. After sleep the
    // next sample must be compared with the actual pre-sleep supply voltage.
    s.peak = validVoltage(measuredVolts) ? measuredVolts : peakVolts;
    s.percent = percent; s.charging = charging; s.settleMs = settleMs; s.stableMs = stableMs; s.stepMs = stepMs;
    return s;
  }

  bool settled() const { return initialized && settleMs == 0 && !chargeSuspected(); }
  int voltageTarget() const {
    int target = BatteryState::estimate(filteredVolts);
    return charging && target > 99 ? 99 : target; // terminal CV voltage does not prove charge termination
  }

  bool chargeSuspected() const { return riseVotes > 0 || fastRiseVotes > 0; }
  // Commissioning input from a host that has independently confirmed USB power.
  // This supplies connection state, never a fabricated capacity/percentage.
  void confirmUsbPower() {
    if (rawPercent < 0) return;
    charging = true; peakVolts = measuredVolts; riseVotes = fallVotes = fastRiseVotes = 0;
    smoothing = false;
    beginTransition();
  }

  void sample(float rawVolts, uint32_t now, uint32_t checkMs = POWER_CHECK_MS) {
    if (!(rawVolts >= 2.0f && rawVolts <= 4.4f)) { rawPercent = -1; return; }
    // Ten-tap/swipe reads between polls must not supply extra confirmation votes.
    if (checkMs < BATTERY_LIVE_POWER_CHECK_MS) checkMs = BATTERY_LIVE_POWER_CHECK_MS;
    if (sampled && now - sampledAt < checkMs) return;
    uint32_t elapsed = sampled ? now - sampledAt : 0;
    // A missed poll/sleep is not evidence of charging current or elapsed capacity.
    if (elapsed > BATTERY_CHECK_MS) elapsed = BATTERY_CHECK_MS;
    sampled = true;
    sampledAt = now;
    float previousMeasured = measuredVolts;
    // Two preceding observations protect the edge detector from a single low
    // ADC outlier. The percentage filter is deliberately much slower than USB.
    float stepBaseline = previousMeasured > olderMeasured ? previousMeasured : olderMeasured;
    olderMeasured = previousMeasured;
    measuredVolts = rawVolts; rawPercent = BatteryState::estimate(rawVolts);
    if (!initialized) {
      volts = filteredVolts = rawVolts; percent = rawPercent;
      initialized = true; estimatedAt = now;
    }
    // Time-based voltage filtering, independent of tap/read frequency.
    float blend = elapsed / (10000.0f + elapsed);
    filteredVolts += (rawVolts - filteredVolts) * blend;
    settleMs = elapsed >= settleMs ? 0 : settleMs - elapsed;
    if (!trackingTrend) {
      trendVolts = volts; trendAt = now; trackingTrend = true;
    }
    if (charging) {
      // The reference is the smoothed peak, so single noisy ADC samples
      // can neither raise it nor reset the unplug check.
      if (!smoothing) { smoothVolts = rawVolts; smoothing = true; sagging = false; sagMs = 0; }
      else smoothVolts += (rawVolts - smoothVolts) * (elapsed / (float(CHARGE_SMOOTH_MS) + elapsed));
      if (smoothVolts > peakVolts) peakVolts = smoothVolts;
      else {
        float creep = CHARGE_REF_SAG_VOLTS_PER_S * elapsed * .001f;
        peakVolts -= peakVolts - smoothVolts < creep ? peakVolts - smoothVolts : creep;
      }
      // A big cable step is confirmed by two raw polls. Near full the step is
      // small and the cell then keeps relaxing downward; the reference does
      // not follow, so the smoothed gap grows until it is confirmed.
      // Both the smoothed and the current reading must be low, so one ADC dip
      // never counts; the time is measured from the first low poll.
      fallVotes = peakVolts - rawVolts >= CHARGE_FALL_VOLTS ? fallVotes + 1 : 0;
      bool sag = peakVolts - smoothVolts >= CHARGE_SMALL_FALL_VOLTS && peakVolts - rawVolts >= CHARGE_SMALL_FALL_VOLTS;
      sagMs = sag ? (sagging ? sagMs + elapsed : 0) : 0;
      sagging = sag;
      if (fallVotes >= 2 || (sagging && sagMs >= CHARGE_SAG_CONFIRM_MS)) {
        charging = false; fallVotes = riseVotes = fastRiseVotes = 0; smoothing = false;
        volts = rawVolts; // classification baseline only: do not reset the gauge
        estimatedAt = trendAt = now; trendVolts = volts;
        beginTransition();
      }
      updatePercent(elapsed);
      return;
    }
    // A near-full cell has a smaller cable step. During the connection-settling
    // window, compare against the fresh unplugged/restored baseline.
    float riseThreshold = peakVolts > 0 && settleMs ? CHARGE_TREND_VOLTS : CHARGE_RISE_VOLTS;
    // Also recognize a small abrupt plug-in step outside the transition window.
    // Require three observations on the elevated plateau; a spike cannot pass,
    // and gradual changes retain the existing slower trend confirmation.
    // Latch the recent low plateau, independently of the slowly changing gauge
    // baseline. After unplugging, the cell can relax below that old baseline;
    // reconnecting to the very same voltage still needs to count as a new rise.
    bool quickRise = fastRiseVotes && rawVolts - riseBaseline >= CHARGE_TREND_VOLTS;
    if (!quickRise && validVoltage(stepBaseline) && rawVolts - stepBaseline >= CHARGE_TREND_VOLTS) {
      riseBaseline = stepBaseline;
      quickRise = true;
    }
    // A high outlier immediately before the real edge can hide its first
    // step from the two-sample guard. A sustained plateau above the gauge's
    // known baseline can still start confirmation on the following poll.
    if (!quickRise && rawVolts - volts >= CHARGE_TREND_VOLTS && rawVolts >= previousMeasured) {
      riseBaseline = volts;
      quickRise = true;
    }
    fastRiseVotes = quickRise ? (fastRiseVotes < 3 ? fastRiseVotes + 1 : 3) : 0;
    bool rise = rawVolts - volts >= riseThreshold ||
                fastRiseVotes >= 2 ||
                (now - trendAt >= CHARGE_TREND_MS / 2 && rawVolts - trendVolts >= CHARGE_TREND_VOLTS);
    riseVotes = rise ? riseVotes + 1 : 0;
    if (riseVotes >= 2) {
      charging = true;
      peakVolts = validVoltage(previousMeasured) && previousMeasured < rawVolts ? previousMeasured : rawVolts;
      riseVotes = fastRiseVotes = 0; smoothing = false;
      beginTransition();
      return;
    }
    if (chargeSuspected()) return; // freeze the percentage before confirming the voltage jump
    // Do not erase a genuine slow rise every ten seconds. The measured charger
    // can take a minute to add 25 mV, while the SOC baseline follows it upward.
    // Flat/noisy traces rebase normally; the trend has a bounded two-minute age.
    if (now - trendAt >= CHARGE_TREND_MS &&
        (filteredVolts <= trendVolts + .001f || now - trendAt >= CHARGE_TREND_MAX_MS)) {
      trendAt = now; trendVolts = filteredVolts;
    }
    if (now - estimatedAt >= BATTERY_CHECK_MS) {
      volts += (rawVolts - volts) * .4f;
      estimatedAt = now;
    }
    updatePercent(elapsed);
  }

 private:
  bool initialized = false;
  uint32_t estimatedAt = 0;
  bool sampled = false, trackingTrend = false;
  uint32_t sampledAt = 0, trendAt = 0;
  float trendVolts = 0, olderMeasured = 0, riseBaseline = 0;
  uint8_t riseVotes = 0, fallVotes = 0, fastRiseVotes = 0;
  bool smoothing = false, sagging = false; // smoothVolts restarts whenever charging (re)starts
  float smoothVolts = 0;
  uint32_t sagMs = 0;
  uint32_t settleMs = 0, stableMs = 0, stepMs = 0;

  static bool validVoltage(float v) { return v >= 2.8f && v <= 4.4f; }
  void beginTransition() { settleMs = BATTERY_TRANSITION_MS; stableMs = stepMs = 0; }
  void updatePercent(uint32_t elapsed) {
    if (percent < 0) { if (rawPercent >= 0) percent = rawPercent; return; }
    if (settleMs || chargeSuspected() || rawPercent < 0) { stepMs = 0; return; }
    if (charging) stableMs = 0;
    else stableMs = elapsed >= BATTERY_RELAX_MS - stableMs ? BATTERY_RELAX_MS : stableMs + elapsed;
    int target = voltageTarget();
    bool up = target > percent + BATTERY_PERCENT_DEADBAND && (charging || stableMs >= BATTERY_RELAX_MS);
    bool down = target < percent - BATTERY_PERCENT_DEADBAND && !charging;
    // The 1% noise deadband must not strand the charging gauge at 98% forever.
    up |= charging && target == 99 && percent == 98;
    // Endpoints are allowed after relaxation; charging never forces an existing 100% down to 99%.
    if (!charging && stableMs >= BATTERY_RELAX_MS) {
      up |= target == 100 && percent == 99;
      down |= target == 0 && percent == 1;
    }
    if (!up && !down) { stepMs = 0; return; }
    stepMs += elapsed;
    if (stepMs >= BATTERY_PERCENT_STEP_MS) {
      percent += up ? 1 : -1;
      stepMs -= BATTERY_PERCENT_STEP_MS; // at most one step per accepted poll
    }
  }
};
