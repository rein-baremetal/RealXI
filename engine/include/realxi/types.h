// Shared types and pitch constants for the RealXI offline engine.
// World units are metres and seconds. x runs along the pitch (0 = home goal line, 105 = away goal line),
// y runs across the pitch (0..68), z is height. Team 0 (the player) always attacks toward +x.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rx {

constexpr float kPi = 3.14159265358979f;
constexpr float kPitchL = 105.0f;
constexpr float kPitchW = 68.0f;
constexpr float kMidY = kPitchW * 0.5f;
constexpr float kGoalHalfW = 3.66f;
constexpr float kGoalH = 2.44f;
constexpr float kPostR = 0.06f;
constexpr float kNetDepth = 2.2f;
constexpr float kBoxDepth = 16.5f;
constexpr float kBoxHalfW = 20.16f;
constexpr float kSixDepth = 5.5f;
constexpr float kSixHalfW = 9.16f;
constexpr float kPenaltySpot = 11.0f;
constexpr float kCentreCircle = 9.15f;
constexpr float kBallR = 0.11f;
constexpr float kGravity = 9.81f;
constexpr float kDt = 1.0f / 60.0f;
constexpr int kTeamSize = 11;
constexpr int kNumPlayers = 22;

struct Vec2 {
  float x = 0, y = 0;
  Vec2() = default;
  Vec2(float x_, float y_) : x(x_), y(y_) {}
  Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(float s) const { return {x * s, y * s}; }
  Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
  Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
  Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
  float len() const { return std::sqrt(x * x + y * y); }
  float len2() const { return x * x + y * y; }
};
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline float dist(Vec2 a, Vec2 b) { return (a - b).len(); }
inline Vec2 norm(Vec2 v, Vec2 fallback = {1, 0}) {
  float l = v.len();
  return l > 1e-5f ? v * (1.0f / l) : fallback;
}
inline Vec2 perp(Vec2 v) { return {-v.y, v.x}; }
inline Vec2 rotate(Vec2 v, float a) {
  float c = std::cos(a), s = std::sin(a);
  return {v.x * c - v.y * s, v.x * s + v.y * c};
}
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float sat(float v) { return clampf(v, 0.0f, 1.0f); }
inline float sigmoid(float v) { return 1.0f / (1.0f + std::exp(-v)); }
inline float angleOf(Vec2 v) { return std::atan2(v.y, v.x); }
inline float wrapAngle(float a) {
  while (a > kPi) a -= 2 * kPi;
  while (a < -kPi) a += 2 * kPi;
  return a;
}
// Distance from point p to segment ab, and the parameter t in [0,1] of the closest point.
inline float distToSegment(Vec2 p, Vec2 a, Vec2 b, float* tOut = nullptr) {
  Vec2 ab = b - a;
  float l2 = ab.len2();
  float t = l2 > 1e-6f ? clampf(dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
  if (tOut) *tOut = t;
  return dist(p, a + ab * t);
}

struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec2 xy() const { return {x, y}; }
};

enum class Role : uint8_t { GK, CB, LB, RB, LWB, RWB, CDM, CM, CAM, LM, RM, LW, RW, ST };
enum class Line : uint8_t { GK, Def, Mid, Att };
inline Line lineOf(Role r) {
  switch (r) {
    case Role::GK: return Line::GK;
    case Role::CB: case Role::LB: case Role::RB: case Role::LWB: case Role::RWB: return Line::Def;
    case Role::CDM: case Role::CM: case Role::CAM: case Role::LM: case Role::RM: return Line::Mid;
    default: return Line::Att;
  }
}

enum class Phase : uint8_t { Kickoff = 0, Live = 1, DeadBall = 2, GoalPause = 3, HalfTime = 4, FullTime = 5, DrillResult = 6 };
enum class Restart : uint8_t { None = 0, KickOff, ThrowIn, GoalKick, Corner, FreeKick, IndirectFreeKick, Penalty };
enum class PState : uint8_t { Idle = 0, Run, Sprint, Kick, Tackle, Slide, Recover, Dive, Header, Celebrate };

enum class EventType : int32_t {
  None = 0, Kick, Pass, Shot, Goal, Save, Post, Foul, YellowCard, RedCard, Offside, Corner, GoalKick, ThrowIn,
  Whistle, HalfTime, FullTime, Tackle, TackleWon, Bounce, Header, DrillSuccess, DrillFail, DrillComplete, Catch, Skill
};

struct Event {
  int32_t type = 0;
  int32_t team = -1;
  int32_t player = -1;
  int32_t arg = 0;     // type specific (e.g. shot speed * 10, goal minute)
};

struct Input {
  float moveX = 0, moveY = 0;  // screen-aligned stick, +x = toward away goal
  bool sprint = false, pass = false, shoot = false, lob = false, tackle = false;
  bool finesse = false, call = false, sw = false, skill = false;
};

struct TeamStats {
  float possession = 0;  // seconds with the ball
  int shots = 0, shotsOnTarget = 0, passes = 0, passesCompleted = 0, tackles = 0, tacklesWon = 0;
  int fouls = 0, yellows = 0, reds = 0, corners = 0, offsides = 0, saves = 0;
  float xg = 0;
};

struct MatchConfig {
  uint32_t seed = 1;
  int mode = 0;                // 0 = full match, 1 = practice drill
  int drillId = 0;
  int difficulty = 1;          // AI tier: 0 Regular, 1 Hard, 2 Pro, 3 Legend
  int homeDifficulty = -1;     // >= 0 makes the home team fully AI driven (self-play tests)
  int formationHome = 0, formationAway = 0;
  float halfSeconds = 180.0f;  // real seconds per half
  int referee = 1;             // 0 lenient, 1 standard, 2 strict
  float assist = 0.25f;        // 0 = none (pure aim), 1 = strong aim help
  bool offside = true, fouls = true, adaptiveAI = true, infiniteStamina = false;
  int drillAttempts = 10;
};

}  // namespace rx
