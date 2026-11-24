# Architecture

## Engine (engine/)

Fixed 60 Hz step, deterministic for a given seed (PCG32). Units are metres and seconds; team 0 (you) always attacks +x.

| File | Responsibility |
|------|----------------|
| `core.cpp` | System tier table, formations, ball physics (gravity, quadratic drag, Magnus curl, bounce, rolling friction) |
| `match_core.cpp` | tick pipeline, player movement and stamina, possession and pickup, human input to intents, snapshot, replay |
| `match_actions.cpp` | kicks, passes, shots (aim solve for System curl), tackles and slides, skills, keeper saves, shot preview |
| `match_rules.cpp` | goals, posts, out of play, restarts and spacing, fouls and cards, offside, half/full time |
| `ai_team.cpp` | team shape, pressing, marking, offside trap, off-ball runs, set-piece layouts |
| `ai_decide.cpp` | ball-carrier decision engine, goalkeeper |
| `drills.cpp` | the 8 practice drills and attempt scoring |
| `api.cpp` | flat C API used by JNI and tests; direct-buffer input, snapshot and events |

Snapshot layout: `include/realxi/snapshot.h`, mirrored in `app/.../engine/Snapshot.kt` (checked by `tools/check_layout.py`).

## How the hard System decides

Everything is scored on one scale: *chance this action turns into a goal, minus the cost of losing the ball here*.

* **Value map**: `possessionValue = 0.02 + 0.85 xG(pos) + 0.28 progress^2 + 0.08 progress`, with a logistic xG model on distance and
  goal angle. Losing the ball is cheaper in the attacking third.
* **Carrier options**: shoot (best of 5 goal-mouth targets, blockers and keeper position reduce xG), ground pass, through
  ball into space, lob, cross, 16 dribble directions, clearance under pressure. Passes use a lane-interception model (every
  opponent's time to reach 10 points on the ball path versus the ball's travel time, discounted by how hard a fast ball is to
  take), receiver reach, offside status, and receiver pressure.
* **Off-ball attackers**: sample candidate spots around the role anchor (more samples at higher tiers) and score value, openness,
  lane to the carrier, crowding and offside risk. Forwards add timed runs in behind. The human's CALL button boosts this.
* **Defending**: one presser with goal-side approach, a cover player, greedy marking of the most dangerous runners, lane
  cutting for midfielders at the top tiers, a shared back line that can step up as an offside trap, and a block that
  shifts toward the channel the human uses most (scouting model).
* **Goalkeeper**: predicts the ball path (spin-aware at Pro/Legend), reacts after a tier-specific delay, runs or dives to the
  crossing point, narrows the angle in 1v1s, sweeps loose balls, and at penalties either commits to a guess biased by your
  past penalties or reads the strike.
* **Fairness**: no input reading, no attribute boosts. Tiers change think interval, reaction time, aim noise, press intensity,
  line height, risk appetite, marking quality, run quality, keeper reaction and read, trap usage, feint resistance, adaptation.

## Tuning

`make sim ARGS="<matches> <homeTier> <awayTier> <halfSeconds> <seed>"` runs headless self-play and prints shots, passes,
tackles, xG and results. Tier profiles are in `core.cpp` (`kProfiles`); the value model is in `ai_util.h`.
