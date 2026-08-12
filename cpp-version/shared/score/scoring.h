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
// its band drives a passive score trickle. Thresholds (points): green < 1000 <= yellow < 2000 <= amber < 3000 <= red.
inline constexpr double ALERT_DECAY_RATE = 5.0;      // points/second
inline constexpr double ALERT_YELLOW = 1000.0;
inline constexpr double ALERT_AMBER  = 2000.0;
inline constexpr double ALERT_RED    = 3000.0;

// Charger recharge: while the controlled unit sits on a charger below max health it heals
// and the score drains; both stop at full health.
inline constexpr double RECHARGE_DRAIN_RATE = 5.0;             // score points/second
inline constexpr float  CHARGER_HEAL_FRACTION_PER_SEC = 0.25f; // fraction of maxHealth/second

// Displayed score lags the logical score, "clocking" through integers at this rate.
inline constexpr double SCORE_CLOCK_RATE = 100.0;     // points/second

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

//------------------------------------------------------------------------------
// Alert glow (ship alert beacon). Mirrors the uber renderer: an alert-sourced glow surface takes
// the band's colour, pulsed by a sine 0..1. Kept raylib-free (plain RGB) so it stays header-only
// and unit-testable; the 3D game feeds the result into the scene shader's emissive term for
// `glowSource: alert` scenery (the alert lights). See docs/scenery_entities.md.
//------------------------------------------------------------------------------

struct AlertColor { float r = 0.0f, g = 0.0f, b = 0.0f; };

// Band colours match uber (game.cpp alertGreen/Yellow/Amber/Red).
inline AlertColor alert_band_color(AlertBand band) {
    switch (band) {
        case AlertBand::Red:    return {1.0f, 0.0f, 0.0f};
        case AlertBand::Amber:  return {0.8f, 0.5f, 0.0f};
        case AlertBand::Yellow: return {1.0f, 1.0f, 0.0f};
        case AlertBand::Green:  default: return {0.0f, 1.0f, 0.0f};
    }
}

// Glow pulse frequency (Hz), rising with alert level so the beacon blinks faster under higher
// alert (uber used a fixed 0.4 Hz; scaling the rate is our addition). ~0.4 Hz when calm up to
// ~2.0 Hz at red alert. Integrate this into a phase (don't multiply straight into sin(t)) so the
// pulse stays continuous as the rate changes.
inline double alert_pulse_hz(double level) {
    double t = level / ALERT_RED;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return 0.4 + 1.6 * t;
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
