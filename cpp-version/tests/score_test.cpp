#include <gtest/gtest.h>
#include "score/scoring.h"

//------------------------------------------------------------------------------
// Alert bands by threshold.
//------------------------------------------------------------------------------
TEST(AlertBandTest, Thresholds) {
    EXPECT_EQ(alert_band(0.0), AlertBand::Green);
    EXPECT_EQ(alert_band(999.9), AlertBand::Green);
    EXPECT_EQ(alert_band(1000.0), AlertBand::Yellow);
    EXPECT_EQ(alert_band(1999.9), AlertBand::Yellow);
    EXPECT_EQ(alert_band(2000.0), AlertBand::Amber);
    EXPECT_EQ(alert_band(2999.9), AlertBand::Amber);
    EXPECT_EQ(alert_band(3000.0), AlertBand::Red);
    EXPECT_EQ(alert_band(5000.0), AlertBand::Red);
}

TEST(AlertBandTest, ScoreRatePerBand) {
    EXPECT_DOUBLE_EQ(alert_score_rate(AlertBand::Green), 0.0);
    EXPECT_DOUBLE_EQ(alert_score_rate(AlertBand::Yellow), 5.0);
    EXPECT_DOUBLE_EQ(alert_score_rate(AlertBand::Amber), 10.0);
    EXPECT_DOUBLE_EQ(alert_score_rate(AlertBand::Red), 15.0);
}

//------------------------------------------------------------------------------
// Alert glow beacon: band colours (uber green/yellow/amber/red) + pulse rate rising with level.
//------------------------------------------------------------------------------
TEST(AlertGlowTest, BandColours) {
    auto eq = [](AlertColor c, float r, float g, float b) {
        return c.r == r && c.g == g && c.b == b;
    };
    EXPECT_TRUE(eq(alert_band_color(AlertBand::Green),  0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(eq(alert_band_color(AlertBand::Yellow), 1.0f, 1.0f, 0.0f));
    EXPECT_TRUE(eq(alert_band_color(AlertBand::Amber),  0.8f, 0.5f, 0.0f));
    EXPECT_TRUE(eq(alert_band_color(AlertBand::Red),    1.0f, 0.0f, 0.0f));
}

TEST(AlertGlowTest, PulseRateRisesWithLevel) {
    // Calm floor at/below 0, red-alert ceiling clamped at ALERT_RED and beyond.
    EXPECT_DOUBLE_EQ(alert_pulse_hz(0.0), 0.4);
    EXPECT_DOUBLE_EQ(alert_pulse_hz(-100.0), 0.4);        // clamps low
    EXPECT_DOUBLE_EQ(alert_pulse_hz(ALERT_RED), 2.0);     // ceiling
    EXPECT_DOUBLE_EQ(alert_pulse_hz(ALERT_RED * 2.0), 2.0);  // clamps high
    // Monotonic rise between the extremes.
    EXPECT_GT(alert_pulse_hz(ALERT_YELLOW), alert_pulse_hz(0.0));
    EXPECT_GT(alert_pulse_hz(ALERT_AMBER), alert_pulse_hz(ALERT_YELLOW));
    EXPECT_GT(alert_pulse_hz(ALERT_RED), alert_pulse_hz(ALERT_AMBER));
}

//------------------------------------------------------------------------------
// Points = 50 x class (typeCode/100, clamped 1..9).
//------------------------------------------------------------------------------
TEST(ScorePointsTest, PerClass) {
    EXPECT_EQ(score_points_for_typecode(101), 50);   // class 1
    EXPECT_EQ(score_points_for_typecode(249), 100);  // class 2
    EXPECT_EQ(score_points_for_typecode(302), 150);  // class 3
    EXPECT_EQ(score_points_for_typecode(900), 450);  // class 9
    EXPECT_EQ(score_points_for_typecode(50), 50);    // <100 clamps to class 1
    EXPECT_EQ(score_points_for_typecode(1200), 450); // >900 clamps to class 9
}

//------------------------------------------------------------------------------
// Display clocks toward the logical score at 50/s, both directions, no overshoot.
//------------------------------------------------------------------------------
TEST(ScoreClockTest, ChasesAndClamps) {
    // Up: 50/s.
    EXPECT_DOUBLE_EQ(score_clock_step(0.0, 100.0, 1.0), 50.0);
    // Up but target closer than a full step -> snap to target (no overshoot).
    EXPECT_DOUBLE_EQ(score_clock_step(0.0, 30.0, 1.0), 30.0);
    // Down (recharge lowered score): 50/s toward target.
    EXPECT_DOUBLE_EQ(score_clock_step(100.0, 0.0, 1.0), 50.0);
    EXPECT_DOUBLE_EQ(score_clock_step(20.0, 0.0, 1.0), 0.0);   // clamp at target going down
    // Already at target.
    EXPECT_DOUBLE_EQ(score_clock_step(42.0, 42.0, 1.0), 42.0);
    // Zero dt: no movement.
    EXPECT_DOUBLE_EQ(score_clock_step(10.0, 100.0, 0.0), 10.0);
}
