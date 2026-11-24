// Small core pieces: deterministic RNG, AI difficulty profiles, formation table and ball physics.
#pragma once
#include "types.h"

namespace rx {

// PCG32: tiny, fast, deterministic across platforms (same seed => same match on every device).
class Rng {
 public:
  explicit Rng(uint64_t seed = 1) { reseed(seed); }
  void reseed(uint64_t seed) { state_ = 0; inc_ = (seed << 1u) | 1u; next(); state_ += seed * 0x9E3779B97F4A7C15ull; next(); }
  uint32_t next() {
    uint64_t old = state_;
    state_ = old * 6364136223846793005ull + inc_;
    uint32_t xs = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = static_cast<uint32_t>(old >> 59u);
    return (xs >> rot) | (xs << ((~rot + 1u) & 31u));
  }
  float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }
  float range(float a, float b) { return a + (b - a) * uniform(); }
  int index(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<uint32_t>(n)); }
  bool chance(float p) { return uniform() < p; }
  float gauss() {  // Box-Muller, one value per call
    float u1 = std::max(uniform(), 1e-6f), u2 = uniform();
    return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * kPi * u2);
  }
 private:
  uint64_t state_ = 0, inc_ = 1;
};

// Everything that makes one AI tier play differently. No tier cheats: the AI never reads the player's
// input and never gets stat boosts; tiers differ only in how well it perceives, decides and executes.
struct AIProfile {
  const char* name;
  float thinkInterval;   // seconds between decisions for each player
  float reaction;        // seconds before the AI acts on a new situation (ball won, loose ball, shot)
  float passErrDeg;      // base aim noise on passes (1 sigma, degrees)
  float shotErrDeg;      // base aim noise on shots
  float lineHeight;      // 0 deep block .. 1 very high line
  float pressIntensity;  // 0..1 how many players hunt the ball and how early
  float aggression;      // willingness to tackle / slide
  float riskTolerance;   // 0..1 appetite for incisive but risky passes
  float patience;        // 0..1 prefers recycling possession over forcing the ball forward
  float shootBias;       // multiplier on shot value (greater => shoots earlier)
  float sprintUsage;     // 0..1 how freely it sprints
  float gkReaction;      // keeper reaction time after a shot
  float gkReach;         // multiplier on keeper reach
  float gkRead;          // 0..1 trajectory reading fidelity (1 = reads spin and bounce)
  float markingQuality;  // 0..1 goal-side marking and lane cutting precision
  float runQuality;      // 0..1 off-ball run intelligence (candidate sampling)
  float offsideTrap;     // 0..1 chance to step up as a line
  float feintResist;     // 0..1 how little a defender bites on feints and skill moves
  float adapt;           // 0..1 how much it shifts toward the player's habits (scouting)
  float setPiece;        // 0..1 set piece quality (aim, wall, routines)
};

const AIProfile& aiProfile(int tier);
constexpr int kNumTiers = 4;

struct FormationSlot {
  Role role;
  float x;  // 0 left .. 1 right (across the pitch)
  float y;  // 0 own goal .. 1 opponent goal (along the pitch)
};
struct Formation {
  const char* name;
  FormationSlot slots[kTeamSize];
};
constexpr int kNumFormations = 21;
const Formation& formation(int index);

// World position of a formation slot for a team that attacks toward +x (team 0) or -x (team 1).
Vec2 slotToWorld(const FormationSlot& s, int team);

struct Ball {
  Vec3 pos{kPitchL * 0.5f, kMidY, kBallR};
  Vec3 vel{};
  Vec3 spin{};  // angular velocity (rad/s): z = side spin (curl), x/y = top/back spin
  bool inNet = false;
};

// Advances the ball one step: gravity, drag, Magnus curl, bounce, rolling friction. No player or rules logic.
void stepBall(Ball& b, float dt);
// Samples the free-flight path every `step` seconds for `count` samples (index 0 = now). Used by AI and keepers.
void predictBall(const Ball& b, float step, int count, Vec3* out);
// Ground pass: initial speed (m/s) so the ball arrives after `d` metres still moving at `arrive` m/s.
constexpr float kPassArrive = 8.0f;      // ground pass arrival speed (crisp, not dying on the turf)
constexpr float kThroughArrive = 10.5f;  // through ball arrival speed
float groundSpeedFor(float d, float arrive);
// Time (s) a ground ball launched at v0 needs to cover distance d (infinity if it stops short).
float groundTimeFor(float v0, float d);

}  // namespace rx
