# Score & ship alert level

Two running game-state accumulators on `Game`, driven by the pure rules in
[`shared/score/scoring.h`](../shared/score/scoring.h): the player's **score** and the ship's
**alert level**. Both start at 0. See also [transfer.md](transfer.md) (kills/captures) and
[charger.md](charger.md) (recharge).

## Score

`Game::score` (double, logical) updates immediately; `Game::scoreDisplay` lags and "clocks"
toward it at **50 points/s** through integer values (arcade roll-up), in both directions.
The HUD shows `scoreDisplay`. Score is clamped ≥ 0.

Sources:
- **Destroy a droid directly**: `+50 × class` (class 1–9 = `typeCode / 100`). Applied in
  `game_reap_dead` when a droid's health reaches 0.
- **Capture a droid**: `+50 × class` — same as a kill. Applied when a transfer fly-over
  completes (`transfer_update`). Debug F1/F2 captures and cross-level re-piloting are not
  fresh captures and score nothing.
- **Destroying a captured unit**: no score effect (the reap path skips the captured unit,
  and the transfer detach/`destroyUnit` path never awards).
- **Alert trickle**: while the ship is on alert, score accrues passively — **yellow 5,
  amber 10, red 15 points/s** (green 0).
- **Recharge drain**: while the controlled unit sits on a charger **below max health**, it
  heals at `CHARGER_HEAL_FRACTION_PER_SEC` (25%/s of max) and the score drains **5
  points/s**; both stop at full health. (This is the charger recharge interaction.)

## Alert level

`Game::alertLevel` (double). Bands: **green < 500 ≤ yellow < 1000 ≤ amber < 1500 ≤ red**
(`alert_band`).

- **Rises** by the same amount as points scored on each kill/capture (so `+50 × class`),
  via `game_award_points` (which bumps both `score` and `alertLevel`).
- **Decays** steadily at `ALERT_DECAY_RATE` = **2 points/s**, clamped ≥ 0.

The band drives the score trickle above. Alert **visualisation is a separate task**; this
change only computes/stores the level (available via `alert_band(game->alertLevel)`).

## Per-frame order (`game_update_score`, only while not paused)

1. decay `alertLevel` (−2/s, clamp ≥0);
2. add the band's trickle to `score`;
3. charger recharge: if the controlled unit (captured, else the device) is on a charger
   footprint and below max health → heal + drain 5/s;
4. clamp `score` ≥ 0;
5. clock `scoreDisplay` toward `score` at 50/s.

## Ship activation

`alertLevel` resets to 0 (green) on ship activation. Ship-to-ship transfer isn't implemented
yet, so today this is just the initial value in `game_init`; a future ship-load path should
reset `alertLevel` (score persists across ships — it's the player's running total).

## Constants (`shared/score/scoring.h`)

`SCORE_POINTS_PER_CLASS=50`, `ALERT_DECAY_RATE=2`, `ALERT_YELLOW/AMBER/RED=500/1000/1500`,
`RECHARGE_DRAIN_RATE=5`, `CHARGER_HEAL_FRACTION_PER_SEC=0.25`, `SCORE_CLOCK_RATE=50`.

## Tests

`tests/score_test.cpp`: alert-band thresholds, per-band trickle rates,
`score_points_for_typecode` (class scaling + clamps), and `score_clock_step` (chases up/down
at 50/s, no overshoot).
