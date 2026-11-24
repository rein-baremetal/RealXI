// Engine regression tests. Build and run with:  make engine-test   (or ctest via CMake)
#include <cmath>
#include <cstdio>
#include <cstring>

#include "realxi/api.h"
#include "realxi/match.h"
#include "ai_util.h"

namespace rx {
class MatchTestAccess {
 public:
  static World& w(Match& m) { return m.w_; }
};
}  // namespace rx

using namespace rx;
static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (cond) { ++g_pass; } else { ++g_fail; printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
  } while (0)

static void testBallPhysics() {
  Ball b;
  b.pos = {10, 34, 0.11f};
  b.vel = {20, 0, 0};
  for (int i = 0; i < 600; ++i) stepBall(b, kDt);
  CHECK(b.vel.x < 0.5f && b.pos.x > 40.0f && b.pos.x < 90.0f, "rolling ball decelerates to a stop after a plausible distance");
  Ball c;
  c.pos = {10, 34, 0.11f};
  c.vel = {25, 0, 4};
  c.spin = {0, 0, 55};
  for (int i = 0; i < 90; ++i) stepBall(c, kDt);
  CHECK(c.pos.y > 35.0f, "positive side spin curls the ball toward +y");
  Ball d;
  d.pos = {50, 34, 5.0f};
  d.vel = {0, 0, 0};
  float maxZ = 0;
  for (int i = 0; i < 240; ++i) { stepBall(d, kDt); if (i > 60) maxZ = std::max(maxZ, d.pos.z); }
  CHECK(maxZ > 0.3f && maxZ < 3.0f, "dropped ball bounces with energy loss");
  float v = groundSpeedFor(20.0f, 6.0f);
  Ball e;
  e.pos = {0, 34, 0.11f};
  e.vel = {v, 0, 0};
  while (e.pos.x < 20.0f && e.vel.x > 0.1f) stepBall(e, kDt);
  CHECK(std::fabs(e.vel.x - 6.0f) < 0.6f, "groundSpeedFor lands the pass at the requested arrival speed");
}

static void testDeterminism() {
  MatchConfig c;
  c.seed = 42; c.homeDifficulty = 2; c.difficulty = 2; c.halfSeconds = 60;
  Match a(c), b(c);
  for (int i = 0; i < 6000; ++i) { a.tick(); b.tick(); }
  const World &wa = a.world(), &wb = b.world();
  CHECK(wa.ball.pos.x == wb.ball.pos.x && wa.ball.pos.y == wb.ball.pos.y, "same seed gives the same ball path");
  CHECK(wa.score[0] == wb.score[0] && wa.stats[0].passes == wb.stats[0].passes, "same seed gives the same stats");
}

static void testFullMatchSanity() {
  MatchConfig c;
  c.seed = 7; c.homeDifficulty = 1; c.difficulty = 1; c.halfSeconds = 60;
  Match m(c);
  long ticks = 0;
  bool finite = true, inBounds = true;
  while (m.world().phase != Phase::FullTime && ticks < 60 * 400) {
    m.tick();
    ++ticks;
    const World& w = m.world();
    finite = finite && std::isfinite(w.ball.pos.x) && std::isfinite(w.ball.pos.y) && std::isfinite(w.ball.pos.z);
    for (const Player& p : w.p) if (p.live()) {
      finite = finite && std::isfinite(p.pos.x) && std::isfinite(p.pos.y);
      inBounds = inBounds && p.pos.x > -4 && p.pos.x < kPitchL + 4 && p.pos.y > -4 && p.pos.y < kPitchW + 4;
    }
  }
  CHECK(m.world().phase == Phase::FullTime, "a match reaches full time");
  CHECK(finite, "no NaN/inf in a full match");
  CHECK(inBounds, "players stay on or next to the pitch");
  CHECK(m.world().stats[0].passes > 5 && m.world().stats[1].passes > 5, "both sides pass the ball");
}

static void testTierOrdering() {
  int shotsHigh = 0, shotsLow = 0, goalsHigh = 0, goalsLow = 0;
  for (int i = 0; i < 10; ++i) {
    MatchConfig c;
    c.seed = 100 + i; c.homeDifficulty = 3; c.difficulty = 0; c.halfSeconds = 90;
    Match m(c);
    long t = 0;
    while (m.world().phase != Phase::FullTime && t < 60 * 500) { m.tick(); ++t; }
    shotsHigh += m.world().stats[0].shots; shotsLow += m.world().stats[1].shots;
    goalsHigh += m.world().score[0]; goalsLow += m.world().score[1];
  }
  CHECK(shotsHigh > shotsLow * 2, "Legend AI creates far more shots than Regular AI");
  CHECK(goalsHigh >= goalsLow, "Legend AI does not score fewer goals than Regular AI");
}

static void testDrills() {
  for (int d = 0; d < 8; ++d) {
    MatchConfig c;
    c.seed = 5 + d; c.mode = 1; c.drillId = d; c.difficulty = 2; c.homeDifficulty = 2; c.drillAttempts = 4;
    Match m(c);
    long t = 0;
    while (!m.drill().complete && t < 60 * 120) { m.tick(); ++t; }
    char msg[64];
    snprintf(msg, sizeof msg, "drill %d completes its attempts", d);
    CHECK(m.drill().complete && m.drill().attempt == 4, msg);
  }
}

static void testHumanInput() {
  MatchConfig c;
  c.seed = 3; c.difficulty = 1; c.halfSeconds = 60;
  Match m(c);
  Input in;
  in.moveX = 1; in.sprint = true;
  double vx = 0;
  int n = 0;
  for (int i = 0; i < 900; ++i) {
    m.setInput(in);
    m.tick();
    const World& w = m.world();
    if (i > 600 && w.activeHuman >= 0 && w.phase == Phase::Live) { vx += w.p[w.activeHuman].vel.x; ++n; }
  }
  CHECK(m.world().activeHuman >= 0, "the human controls a player");
  CHECK(n > 50 && vx / n > 1.0, "stick input drives the controlled player toward the opposition goal");
}

static void testApiAndSnapshot() {
  void* h = rx_create(1, 0, 0, 1, 0, 0, 60.0f, 1, 0.25f, 0b0111, 5, -1);
  float* in = rx_input_buffer(h);
  in[0] = 1.0f;
  for (int i = 0; i < 120; ++i) rx_step(h, kDt);
  float* s = rx_snapshot_buffer(h);
  bool finite = true;
  for (int i = 0; i < rx::snap::kFloats; ++i) finite = finite && std::isfinite(s[i]);
  CHECK(finite, "snapshot contains only finite values");
  CHECK(s[rx::snap::kBallX] > 0.0f && s[rx::snap::kBallX] < kPitchL, "snapshot ball position is on the pitch");
  rx_command(h, 3, 3.0f);  // replay 3 s (needs frames; may be ignored this early)
  rx_command(h, 4, 0.0f);
  rx_destroy(h);
  CHECK(rx_snapshot_floats() == rx::snap::kFloats, "snapshot size matches the layout header");
}

// Logic ported from PitchBot: xT grid, crowd detection, triangle support and the game-state plan.
static void testPitchBotLogic() {
  World w;
  CHECK(aiutil::xtAt(w, 0, {95, 34}) > aiutil::xtAt(w, 0, {60, 34}), "xT rises toward the opposition goal");
  CHECK(aiutil::xtAt(w, 0, {60, 34}) > aiutil::xtAt(w, 0, {15, 34}), "xT is higher in midfield than in our own third");
  CHECK(aiutil::xtAt(w, 0, {95, 34}) > aiutil::xtAt(w, 0, {95, 4}), "xT prefers central zones in front of goal");
  CHECK(std::fabs(aiutil::xtAt(w, 1, {10, 34}) - aiutil::xtAt(w, 0, {kPitchL - 10, 34})) < 1e-4f, "xT is symmetric for the away team");
  w.score[0] = 1; w.score[1] = 0; w.gameTime = 4500;
  CHECK(aiutil::gameMode(w, 0) == 2 && aiutil::gameMode(w, 1) == 3, "late lead protects, late deficit chases");
  w.score[0] = 0; w.gameTime = 600;
  CHECK(aiutil::gameMode(w, 0) == 1, "level early game presses");
  // Crowd: two opponents within 7 m count as crowded.
  for (auto& p : w.p) { p.team = 1; p.active = true; p.sentOff = false; p.role = Role::CB; p.state = PState::Idle; p.pos = {5, 5}; }
  w.p[11].pos = {50, 34}; w.p[12].pos = {53, 34}; w.p[13].pos = {40, 40};
  int n = 0;
  float c = aiutil::crowdAround(w, 0, {50, 30}, &n);
  CHECK(n >= 2 && c > 0.3f, "two close opponents make the carrier crowded");
}

static void testKeepersStayInBox() {
  int outside = 0;
  for (int seed = 1; seed <= 3; ++seed) {
    MatchConfig c;
    c.seed = seed; c.homeDifficulty = 1; c.difficulty = 2; c.halfSeconds = 60.0f;
    Match m(c);
    for (long t = 0; t < 60 * 60 * 3 && m.world().phase != Phase::FullTime; ++t) {
      m.tick();
      const World& w = m.world();
      if (w.phase != Phase::Live) continue;
      const Player& a = w.p[0]; const Player& b = w.p[kTeamSize];
      if (a.pos.x > kBoxDepth + 0.5f || std::fabs(a.pos.y - kMidY) > kBoxHalfW + 0.5f) ++outside;
      if (b.pos.x < kPitchL - kBoxDepth - 0.5f || std::fabs(b.pos.y - kMidY) > kBoxHalfW + 0.5f) ++outside;
    }
  }
  CHECK(outside == 0, "goalkeepers never leave their penalty area in open play");
}

static void testSecondHalfSwapsEnds() {
  MatchConfig c;
  c.seed = 4; c.homeDifficulty = 1; c.difficulty = 1; c.halfSeconds = 20.0f;
  Match m(c);
  float buf[snap::kFloats];
  m.writeSnapshot(buf);
  bool firstOk = buf[snap::kPlayers + 0] < 20.0f;  // home keeper on the left in the first half
  bool swapped = false;
  for (long t = 0; t < 60 * 120 && !swapped; ++t) {
    m.tick();
    if (m.world().half == 2 && m.world().phase == Phase::Live) {
      m.writeSnapshot(buf);
      swapped = buf[snap::kPlayers + 0] > kPitchL - 30.0f;  // home keeper now shown on the right
      Input in; in.moveX = 1.0f; m.setInput(in);
    }
  }
  CHECK(firstOk, "first half: home keeper starts on the left");
  CHECK(swapped, "second half: teams change ends in the view");
}

static void testAllFormationsPlay() {
  bool ok = true;
  for (int f = 0; f < kNumFormations; ++f) {
    MatchConfig c;
    c.seed = 5 + f; c.homeDifficulty = 1; c.difficulty = 1; c.halfSeconds = 60.0f;
    c.formationHome = f; c.formationAway = (f * 7 + 3) % kNumFormations;
    Match m(c);
    for (int t = 0; t < 60 * 25; ++t) {
      m.tick();
      for (const Player& p : m.world().p)
        if (!std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) || p.pos.x < -5 || p.pos.x > kPitchL + 5) ok = false;
    }
  }
  CHECK(ok, "every formation plays 25 s without NaN or players leaving the pitch");
}

int main() {
  testAllFormationsPlay();
  testKeepersStayInBox();
  testSecondHalfSwapsEnds();
  testPitchBotLogic();
  testBallPhysics();
  testDeterminism();
  testFullMatchSanity();
  testTierOrdering();
  testDrills();
  testHumanInput();
  testApiAndSnapshot();
  printf("%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
