#ifndef SCORING_H
#define SCORING_H

//------------------------------------------------------------------------------
// Scoring + ship alert level (pure, header-only so it is unit-testable). The
// stateful accumulators live on Game; these are the constants and pure rules.
// See docs/scoring.md.
//------------------------------------------------------------------------------

// Points for destroying or capturing a droid: 50 x its class (1-9).
inline constexpr int SCORE_POINTS_PER_CLASS = 50;

// Alert level: rises by the points scored on each kill/capture, decays steadily, and
// its band drives a passive score trickle. Thresholds (points): green < 500 <= yellow <
// 1000 <= amber < 1500 <= red.
inline constexpr double ALERT_DECAY_RATE = 2.0;      // points/second
inline constexpr double ALERT_YELLOW = 500.0;
inline constexpr double ALERT_AMBER  = 1000.0;
inline constexpr double ALERT_RED    = 1500.0;

// Charger recharge: while the controlled unit sits on a charger below max health it heals
// and the score drains; both stop at full health.
inline constexpr double RECHARGE_DRAIN_RATE = 5.0;             // score points/second
inline constexpr float  CHARGER_HEAL_FRACTION_PER_SEC = 0.25f; // fraction of maxHealth/second

// Displayed score lags the logical score, "clocking" through integers at this rate.
inline constexpr double SCORE_CLOCK_RATE = 50.0;     // points/second

enum class AlertBand { Green, Yellow, Amber, Red };

inline AlertBand alert_band(double level) {
    if (level >= ALERT_RED)    return AlertBand::Red;
    if (level >= ALERT_AMBER)  return AlertBand::Amber;
    if (level >= ALERT_YELLOW) return AlertBand::Yellow;
    return AlertBand::Green;
}

// Passive score generated per second at a given alert band.
inline double alert_score_rate(AlertBand band) {
    switch (band) {
        case AlertBand::Yellow: return 5.0;
        case AlertBand::Amber:  return 10.0;
        case AlertBand::Red:    return 15.0;
        case AlertBand::Green:  default: return 0.0;
    }
}

// Points for a droid: 50 x class, where class = typeCode/100 clamped to 1..9.
inline int score_points_for_typecode(int typeCode) {
    int cls = typeCode / 100;
    if (cls < 1) cls = 1;
    if (cls > 9) cls = 9;
    return SCORE_POINTS_PER_CLASS * cls;
}

// Step the displayed score toward `target` by SCORE_CLOCK_RATE*dt, without overshooting
// (handles both directions — recharge can lower the score).
inline double score_clock_step(double display, double target, double dt) {
    double step = SCORE_CLOCK_RATE * (dt > 0.0 ? dt : 0.0);
    if (target > display) { double n = display + step; return n > target ? target : n; }
    if (target < display) { double n = display - step; return n < target ? target : n; }
    return display;
}

#endif // SCORING_H
