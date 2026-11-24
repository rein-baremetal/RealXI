#include "ai_util.h"
#include "realxi/ai.h"
#include "realxi/match.h"

namespace rx {
using namespace aiutil;

// Drill ids (keep in sync with Kotlin DrillId):
//  0 Penalties        1 Free-kick lab     2 1v1 vs keeper     3 3v2 overload
//  4 Corner delivery  5 Crossing + finish 6 Press escape      7 Defend 1v1
namespace {
const Vec2 kGoalR{kPitchL, kMidY};
}

void Match::setupDrill() {
  const int id = drill_.id;
  w_.offsideOn = false;
  w_.foulsOn = false;
  cfg_.mode = 1;
  // Park everyone, then bring in only the players this drill needs.
  for (int i = 0; i < kNumPlayers; ++i) {
    Player& p = w_.p[i];
    p.active = false;
    p.sentOff = false;
    p.pos = {-50.0f - i, -50.0f};
    p.vel = {0, 0};
    p.state = PState::Idle;
    p.stateT = 0; p.touchCD = 0; p.skillT = p.fakeT = p.tackleCD = 0;
    p.stamina = 1.0f;
    p.facing = i < kTeamSize ? 0.0f : kPi;
    intents_[i] = Intent{};
  }
  auto put = [&](int pid, Vec2 pos) {
    Player& p = w_.p[pid];
    p.active = true;
    p.pos = pos;
    p.facing = p.team == 0 ? 0.0f : kPi;
  };
  w_.owner = -1;
  w_.ball = Ball{};
  w_.passTeam = w_.passTarget = -1;
  w_.offsideMask = 0;
  w_.penaltyLive = false;
  w_.noHandleTeam = -1;
  w_.restart = Restart::None;
  w_.phase = Phase::Live;
  w_.taker = -1;
  w_.activeHuman = -1;
  w_.lastTouchTeam = w_.lastTouchId = -1;
  w_.since = 0;
  endPending_ = false;
  outInDrill_ = false;
  drill_.feederId = -1;
  drill_.humanId = -1;
  drill_.feedTimer = 0;
  drill_.complete = false;
  drill_.lastResult = 0;
  ++drill_.attempt;
  const float cs = rng_.chance(0.5f) ? 1.0f : -1.0f;
  auto giveBall = [&](int pid) {
    w_.owner = pid;
    Player& p = w_.p[pid];
    w_.ball.pos = {p.pos.x + std::cos(p.facing) * 0.6f, p.pos.y + std::sin(p.facing) * 0.6f, kBallR};
    w_.lastTouchTeam = p.team;
    w_.lastTouchId = pid;
  };

  switch (id) {
    case 0: {  // penalties
      drill_.limit = 9;
      put(9, {kPitchL - kPenaltySpot - 6.0f, kMidY});
      put(11, {kPitchL - 0.3f, kMidY});
      beginRestart(Restart::Penalty, 0, {kPitchL - kPenaltySpot, kMidY});
      break;
    }
    case 1: {  // free-kick lab
      drill_.limit = 10;
      float a = rng_.range(-0.8f, 0.8f), d = rng_.range(19.0f, 30.0f);
      Vec2 spot{kPitchL - d * std::cos(a), clampf(kMidY + d * std::sin(a), 8.0f, kPitchW - 8.0f)};
      put(9, spot - Vec2{3, 0});
      put(10, {kPitchL - 8.0f, kMidY - 4.0f});
      put(8, {kPitchL - 9.0f, kMidY + 5.0f});
      put(11, {kPitchL - 1.5f, kMidY});
      for (int s = 1; s <= 5; ++s) put(11 + s, {kPitchL - 14.0f, kMidY + (s - 3) * 5.0f});
      beginRestart(Restart::FreeKick, 0, spot);
      break;
    }
    case 2: {  // 1v1 against the keeper with a defender chasing
      drill_.limit = 14;
      Vec2 ap{rng_.range(62.0f, 72.0f), rng_.range(22.0f, 46.0f)};
      put(9, ap);
      put(11, {kPitchL - 2.5f, kMidY});
      put(13, {ap.x - 4.0f, ap.y + rng_.range(-3.0f, 3.0f)});
      giveBall(9);
      break;
    }
    case 3: {  // 3v2 overload
      drill_.limit = 18;
      float x = rng_.range(58.0f, 66.0f), y0 = rng_.range(26.0f, 42.0f);
      put(9, {x, y0});
      put(10, {x + 8.0f, y0 - 10.0f});
      put(8, {x + 6.0f, y0 + 11.0f});
      put(11, {kPitchL - 2.5f, kMidY});
      put(13, {x + 14.0f, y0 - 2.0f});
      put(14, {x + 15.0f, y0 + 6.0f});
      giveBall(9);
      break;
    }
    case 4: {  // corner delivery
      drill_.limit = 12;
      Vec2 spot{kPitchL, cs < 0 ? 0.0f : kPitchW};
      float inwardY = cs < 0 ? 1.0f : -1.0f;
      put(7, {kPitchL - 1.5f, spot.y + inwardY * 2.5f});
      put(5, {kPitchL - 18.0f, kMidY});
      put(6, {kPitchL - 20.0f, kMidY + 8.0f});
      put(8, {kPitchL - 9.0f, kMidY - 3.0f});
      put(9, {kPitchL - 11.0f, kMidY + 1.0f});
      put(10, {kPitchL - 7.0f, kMidY + 4.0f});
      put(11, {kPitchL - 1.0f, kMidY});
      for (int s = 1; s <= 6; ++s) put(11 + s, {kPitchL - 6.0f - (s % 3) * 3.0f, kMidY + (s - 3.5f) * 3.2f});
      beginRestart(Restart::Corner, 0, spot);
      w_.taker = 7;
      Player& t = w_.p[7];
      Vec2 inward = norm(Vec2{kPitchL * 0.5f, kMidY} - spot);
      t.pos = spot - inward * 0.9f;
      t.facing = angleOf(inward);
      break;
    }
    case 5: {  // crossing + finishing
      drill_.limit = 9;
      put(7, {rng_.range(86.0f, 92.0f), cs < 0 ? 6.0f : kPitchW - 6.0f});
      put(9, {rng_.range(90.0f, 95.0f), kMidY + rng_.range(-6.0f, 6.0f)});
      put(11, {kPitchL - 2.5f, kMidY});
      put(12, {kPitchL - 9.0f, kMidY - 4.0f});
      put(13, {kPitchL - 9.0f, kMidY + 4.0f});
      giveBall(7);
      w_.p[7].facing = cs < 0 ? kPi * 0.5f * 0.5f : -kPi * 0.25f;
      drill_.feederId = 7;
      drill_.humanId = 9;
      drill_.feedTimer = 1.2f;
      break;
    }
    case 6: {  // press escape: build out from the keeper against a high press
      drill_.limit = 25;
      put(0, {4.0f, kMidY});
      for (int s = 1; s <= 6; ++s) put(s, slotToWorld(formation(cfg_.formationHome).slots[s], 0) + Vec2{-4.0f, 0});
      put(11, {kPitchL - 4.0f, kMidY});
      for (int s = 6; s <= 10; ++s) put(11 + s, {rng_.range(30.0f, 42.0f), 8.0f + (s - 6) * 13.0f});
      giveBall(0);
      break;
    }
    case 7: {  // defend 1v1
      drill_.limit = 12;
      Vec2 ap{rng_.range(30.0f, 38.0f), rng_.range(24.0f, 44.0f)};
      put(20, ap);
      w_.p[20].facing = kPi;
      put(2, {ap.x - 12.0f, ap.y + rng_.range(-3.0f, 3.0f)});
      w_.p[2].facing = 0;
      put(0, {2.5f, kMidY});
      giveBall(20);
      break;
    }
    default: break;
  }
  drill_.timeLeft = drill_.limit;
  w_.ball.vel = {0, 0, 0};
  if (w_.owner >= 0) {
    // Make sure the carrier starts with the ball and the AI's reaction clock is fresh.
    w_.ball.vel = {0, 0, 0};
  }
}

void Match::finishAttempt(bool success, int reason) {
  drill_.lastResult = success ? 1 : 2;
  if (success) { ++drill_.success; ++drill_.streak; drill_.bestStreak = std::max(drill_.bestStreak, drill_.streak); }
  else { ++drill_.fail; drill_.streak = 0; }
  pushEvent(success ? EventType::DrillSuccess : EventType::DrillFail, 0, -1, drill_.attempt * 10 + reason);
  w_.phase = Phase::DrillResult;
  drill_.resultTimer = 1.6f;
}

void Match::updateDrill() {
  const int id = drill_.id;
  if (drill_.complete) return;
  if (w_.phase == Phase::DrillResult) {
    drill_.resultTimer -= kDt;
    if (drill_.resultTimer <= 0) {
      if (drill_.attempt >= drill_.attemptsMax) {
        drill_.complete = true;
        pushEvent(EventType::DrillComplete, 0, -1, drill_.success);
      } else {
        setupDrill();
      }
    }
    return;
  }
  if (w_.phase == Phase::GoalPause) {
    bool ours = lastScorer_ == 0;
    finishAttempt(id == 7 ? false : ours, id == 7 ? 8 : (ours ? 1 : 8));
    return;
  }
  if (outInDrill_) { finishAttempt(id == 7, id == 7 ? 7 : 3); return; }
  if (w_.phase != Phase::Live) return;

  // Crossing drill: the AI winger serves after a short delay.
  if (id == 5 && drill_.feedTimer > 0) {
    drill_.feedTimer -= kDt;
    if (drill_.feedTimer <= 0 && w_.owner == drill_.feederId) {
      Vec2 T{rng_.range(94.0f, 100.0f), kMidY + rng_.range(-6.0f, 6.0f)};
      doPass(w_.p[drill_.feederId], Act::Cross, T, 0.55f, drill_.humanId);
    }
    return;
  }
  drill_.timeLeft -= kDt;
  const int ot = w_.owner >= 0 ? w_.p[w_.owner].team : -1;
  const float ballSpeed = w_.ball.vel.xy().len();
  bool done = false, success = false;
  int reason = 3;
  const bool gkHas = w_.owner >= 0 && w_.p[w_.owner].role == Role::GK;
  switch (id) {
    case 0: case 1:
      if (ot == 1) { done = true; reason = gkHas ? 2 : 4; }
      else if (w_.owner < 0 && ballSpeed < 0.6f && w_.since > 1.0f) { done = true; reason = 3; }
      break;
    case 2: case 3:
      if (ot == 1) { done = true; reason = gkHas ? 2 : 4; }
      else if (w_.owner < 0 && w_.lastTouchTeam == 1 && ballSpeed < 3.0f && w_.ball.pos.z < 0.5f) { done = true; reason = 4; }
      else if (w_.owner < 0 && ballSpeed < 0.6f && w_.since > 1.0f) { done = true; reason = 3; }
      break;
    case 4: case 5:
      if (ot == 1) { done = true; reason = gkHas ? 2 : 4; }
      else if (w_.owner < 0 && w_.lastTouchTeam == 1 && w_.ball.pos.x < kPitchL - 24.0f) { done = true; reason = 4; }
      else if (w_.owner < 0 && ballSpeed < 0.6f && w_.since > 1.0f) { done = true; reason = 3; }
      break;
    case 6:
      if (ot == 1) { done = true; reason = 4; }
      else if (ot == 0 && w_.owner != 0 && w_.ball.pos.x > kPitchL * 0.5f + 2.0f) { done = true; success = true; reason = 6; }
      else if (w_.owner < 0 && w_.lastTouchTeam == 0 && w_.passTeam == 0 && w_.ball.pos.x > kPitchL * 0.5f + 8.0f) { done = true; success = true; reason = 6; }
      break;
    case 7:
      if (ot == 0) { done = true; success = true; reason = 7; }
      break;
    default: break;
  }
  if (!done && drill_.timeLeft <= 0) { done = true; success = (id == 7); reason = id == 7 ? 7 : 5; }
  if (done) finishAttempt(success, reason);
}

}  // namespace rx
