#include "ai_util.h"
#include "realxi/ai.h"
#include "realxi/match.h"

namespace rx {
using namespace aiutil;

void Match::collideFrame() {
  Ball& b = w_.ball;
  if (b.inNet) {
    float gx = b.pos.x < kPitchL * 0.5f ? 0.0f : kPitchL;
    float inward = gx == 0.0f ? -1.0f : 1.0f;  // the net lies beyond the line
    b.vel.x *= 0.96f; b.vel.y *= 0.96f;
    float depth = (b.pos.x - gx) * inward;
    if (depth > kNetDepth) { b.pos.x = gx + inward * kNetDepth; b.vel.x = -b.vel.x * 0.2f; }
    if (depth < 0.0f) { b.pos.x = gx; b.vel.x = -b.vel.x * 0.2f; }
    float dy = b.pos.y - kMidY;
    if (std::fabs(dy) > kGoalHalfW) { b.pos.y = kMidY + (dy > 0 ? kGoalHalfW : -kGoalHalfW); b.vel.y = -b.vel.y * 0.2f; }
    if (b.pos.z > kGoalH) { b.pos.z = kGoalH; b.vel.z = -std::fabs(b.vel.z) * 0.2f; }
    return;
  }
  if (postCooldown_ > 0) --postCooldown_;
  const float R = kPostR + kBallR;
  for (float gx : {0.0f, kPitchL}) {
    for (float py : {kMidY - kGoalHalfW, kMidY + kGoalHalfW}) {
      Vec2 d = b.pos.xy() - Vec2{gx, py};
      float dd = d.len();
      if (dd < R && dd > 1e-4f && b.pos.z < kGoalH + 0.1f) {
        Vec2 n = d * (1.0f / dd);
        float vn = b.vel.x * n.x + b.vel.y * n.y;
        if (vn < 0) {
          b.vel.x -= 1.6f * vn * n.x;
          b.vel.y -= 1.6f * vn * n.y;
          b.pos.x = gx + n.x * R; b.pos.y = py + n.y * R;
          if (postCooldown_ == 0) { pushEvent(EventType::Post, w_.lastTouchTeam, w_.lastTouchId); postCooldown_ = 20; }
          w_.lastTouchKick = false;
        }
      }
    }
    if (std::fabs(b.pos.y - kMidY) < kGoalHalfW) {
      float dx = b.pos.x - gx, dz = b.pos.z - kGoalH;
      float dd = std::sqrt(dx * dx + dz * dz);
      if (dd < R && dd > 1e-4f) {
        float nx = dx / dd, nz = dz / dd;
        float vn = b.vel.x * nx + b.vel.z * nz;
        if (vn < 0) {
          b.vel.x -= 1.6f * vn * nx;
          b.vel.z -= 1.6f * vn * nz;
          b.pos.x = gx + nx * R; b.pos.z = kGoalH + nz * R;
          if (postCooldown_ == 0) { pushEvent(EventType::Post, w_.lastTouchTeam, w_.lastTouchId); postCooldown_ = 20; }
          w_.lastTouchKick = false;
        }
      }
    }
  }
}

void Match::scoreGoal(int team) {
  ++w_.score[team];
  w_.phase = Phase::GoalPause;
  goalTimer_ = 3.2f;
  w_.ball.inNet = true;
  w_.owner = -1;
  lastScorer_ = team;
  w_.penaltyLive = false;
  int minute = static_cast<int>(w_.gameTime / 60.0f) + 1;
  pushEvent(EventType::Goal, team, w_.lastTouchId, minute);
  if (w_.lastTouchId >= 0 && w_.lastTouchTeam == team) {
    w_.p[w_.lastTouchId].state = PState::Celebrate;
    w_.p[w_.lastTouchId].stateT = 3.0f;
  }
}

void Match::checkBoundaries() {
  Ball& b = w_.ball;
  if (b.pos.y < 0.0f || b.pos.y > kPitchW) {
    if (cfg_.mode == 1) { outInDrill_ = true; return; }
    int team = w_.lastTouchTeam < 0 ? 0 : 1 - w_.lastTouchTeam;
    Vec2 spot{clampf(b.pos.x, 1.0f, kPitchL - 1.0f), b.pos.y < 0 ? 0.0f : kPitchW};
    pushEvent(EventType::ThrowIn, team, -1);
    beginRestart(Restart::ThrowIn, team, spot);
    return;
  }
  if (b.pos.x < 0.0f || b.pos.x > kPitchL) {
    float gx = b.pos.x < 0 ? 0.0f : kPitchL;
    int defending = gx == 0.0f ? 0 : 1;
    if (std::fabs(b.pos.y - kMidY) < kGoalHalfW && b.pos.z < kGoalH) { scoreGoal(1 - defending); return; }
    if (cfg_.mode == 1) { outInDrill_ = true; return; }
    if (w_.lastTouchTeam == defending) {
      Vec2 spot{gx, b.pos.y < kMidY ? 0.0f : kPitchW};
      ++w_.stats[1 - defending].corners;
      pushEvent(EventType::Corner, 1 - defending, -1);
      beginRestart(Restart::Corner, 1 - defending, spot);
    } else {
      pushEvent(EventType::GoalKick, defending, -1);
      beginRestart(Restart::GoalKick, defending, {gx == 0.0f ? kSixDepth : kPitchL - kSixDepth, kMidY});
    }
  }
}

int Match::pickTaker(Restart type, int team, Vec2 spot) {
  int best = -1;
  float bestScore = -1e9f;
  for (int s = 0; s < kTeamSize; ++s) {
    const Player& p = w_.p[team * kTeamSize + s];
    if (!p.live()) continue;
    float score = -1e9f;
    float d = dist(p.pos, spot);
    switch (type) {
      case Restart::KickOff:
        if (p.role == Role::GK) break;
        score = -dist(p.pos, Vec2{kPitchL * 0.5f, kMidY}) + (lineOf(p.role) == Line::Att ? 5.0f : 0.0f);
        break;
      case Restart::GoalKick:
        score = p.role == Role::GK ? 100.0f : -1e9f;
        break;
      case Restart::ThrowIn:
        if (p.role != Role::GK) score = -d;
        break;
      case Restart::Corner:
        if (p.role != Role::GK) score = -d + (lineOf(p.role) == Line::Mid || lineOf(p.role) == Line::Att ? 12.0f : 0.0f);
        break;
      case Restart::Penalty:
        if (p.role != Role::GK) score = p.shooting * 10.0f;
        break;
      default: {  // free kicks
        if (p.role == Role::GK) break;
        bool shootRange = dist(spot, w_.goalCentre(team)) < 30.0f;
        score = (shootRange ? p.shooting * 10.0f : p.passing * 10.0f) - d * 0.35f;
        break;
      }
    }
    if (score > bestScore) { bestScore = score; best = p.id; }
  }
  return best;
}

void Match::beginRestart(Restart type, int team, Vec2 spot) {
  w_.phase = type == Restart::KickOff ? Phase::Kickoff : Phase::DeadBall;
  w_.restart = type;
  w_.restartTeam = team;
  w_.restartSpot = spot;
  w_.restartTimer = 0;
  Ball& b = w_.ball;
  b.pos = {spot.x, spot.y, kBallR};
  b.vel = {0, 0, 0};
  b.spin = {0, 0, 0};
  b.inNet = false;
  w_.owner = -1;
  w_.passTeam = -1;
  w_.passTarget = -1;
  w_.offsideMask = 0;
  w_.penaltyLive = false;
  w_.noHandleTeam = type == Restart::ThrowIn ? team : -1;
  w_.since = 0;
  outInDrill_ = false;
  pushEvent(EventType::Whistle, team, -1, static_cast<int>(type));
  w_.taker = pickTaker(type, team, spot);
  if (w_.taker >= 0) {
    Player& t = w_.p[w_.taker];
    Vec2 inward = norm(Vec2{kPitchL * 0.5f, kMidY} - spot);
    Vec2 goal = w_.goalCentre(team);
    if (type == Restart::FreeKick || type == Restart::IndirectFreeKick || type == Restart::Penalty) inward = norm(goal - spot);
    float back = type == Restart::Penalty ? 3.0f : (type == Restart::ThrowIn ? 0.0f : 0.9f);
    t.pos = spot - inward * back;
    if (type == Restart::ThrowIn) t.pos = {spot.x, spot.y < kMidY ? -0.4f : kPitchW + 0.4f};
    t.vel = {0, 0};
    t.facing = angleOf(inward);
    t.state = PState::Idle;
    t.stateT = 0;
  }
  if (type == Restart::Penalty) {
    Player& gk = w_.p[(1 - team) * kTeamSize];
    if (gk.live()) {
      float gx = w_.goalCentre(team).x;
      gk.pos = {gx + (gx == 0.0f ? 0.3f : -0.3f), kMidY};
      gk.vel = {0, 0};
      gk.facing = gx == 0.0f ? 0.0f : kPi;
      gk.state = PState::Idle;
    }
  }
}

void Match::kickoffReset(int team) {
  placeFormation(0, true);
  placeFormation(1, true);
  for (Player& p : w_.p) if (p.sentOff) p.pos = {-50, -50};
  endPending_ = false;
  beginRestart(Restart::KickOff, team, {kPitchL * 0.5f, kMidY});
}

void Match::enforceRestartSpacing() {
  const Vec2 spot = w_.restartSpot;
  const int rt = w_.restartTeam;
  auto pushOut = [&](Player& p, Vec2 c, float r) {
    Vec2 d = p.pos - c;
    float L = d.len();
    if (L < r) p.pos = c + (L > 1e-4f ? d * (r / L) : Vec2{r, 0});
  };
  for (Player& p : w_.p) {
    if (!p.live() || p.id == w_.taker) continue;
    switch (w_.restart) {
      case Restart::KickOff:
        if (p.team != rt) pushOut(p, spot, kCentreCircle);
        if (p.team == 0) p.pos.x = std::min(p.pos.x, kPitchL * 0.5f - 0.3f);
        else p.pos.x = std::max(p.pos.x, kPitchL * 0.5f + 0.3f);
        break;
      case Restart::FreeKick: case Restart::IndirectFreeKick: case Restart::Corner:
        if (p.team != rt) pushOut(p, spot, kCentreCircle);
        break;
      case Restart::ThrowIn:
        if (p.team != rt) pushOut(p, spot, 2.0f);
        break;
      case Restart::Penalty: {
        bool defGK = p.team != rt && p.role == Role::GK;
        if (defGK) break;
        float gx = w_.goalCentre(rt).x;
        float s = gx == 0.0f ? 1.0f : -1.0f;
        if (std::fabs(p.pos.x - gx) < kBoxDepth + 0.5f && std::fabs(p.pos.y - kMidY) < kBoxHalfW + 0.5f)
          p.pos.x = gx + s * (kBoxDepth + 1.0f);
        pushOut(p, spot, kCentreCircle);
        break;
      }
      default: break;
    }
  }
}

void Match::updateRestart() {
  w_.restartTimer += kDt;
  enforceRestartSpacing();
  Ball& b = w_.ball;
  b.pos = {w_.restartSpot.x, w_.restartSpot.y, kBallR};
  b.vel = {0, 0, 0};
}

void Match::foul(Player& off, Player& victim, bool slide, bool serious) {
  ++w_.stats[off.team].fouls;
  pushEvent(EventType::Foul, off.team, off.id);
  float mult = w_.refereeProfile == 0 ? 1.3f : (w_.refereeProfile == 2 ? 0.75f : 1.0f);
  float yP = ((slide ? 0.35f : 0.15f) + (serious ? 0.25f : 0.0f)) / mult;
  float rP = ((serious && slide) ? 0.08f : 0.01f) / mult;
  bool red = false;
  if (rng_.chance(rP)) {
    red = true;
  } else if (rng_.chance(yP)) {
    ++off.yellows;
    ++w_.stats[off.team].yellows;
    pushEvent(EventType::YellowCard, off.team, off.id);
    if (off.yellows >= 2) red = true;
  }
  if (red) {
    off.sentOff = true;
    off.active = false;
    ++w_.stats[off.team].reds;
    pushEvent(EventType::RedCard, off.team, off.id);
  }
  Vec2 spot = clampPitch(victim.pos, 0.5f);
  float gx = off.team == 0 ? 0.0f : kPitchL;
  bool inBox = std::fabs(spot.x - gx) < kBoxDepth && std::fabs(spot.y - kMidY) < kBoxHalfW;
  if (red) off.pos = {-50, -50};
  if (inBox) beginRestart(Restart::Penalty, victim.team, {gx == 0.0f ? kPenaltySpot : kPitchL - kPenaltySpot, kMidY});
  else beginRestart(Restart::FreeKick, victim.team, spot);
}

void Match::markOffsides(int team, int passerId) {
  float line = std::max(offsideLineProgress(w_, team), w_.progress(team, w_.ball.pos.xy()));
  uint32_t mask = 0;
  for (int s = 0; s < kTeamSize; ++s) {
    const Player& t = w_.p[team * kTeamSize + s];
    if (t.id == passerId || !t.live()) continue;
    if (w_.progress(team, t.pos) > line + 0.002f) mask |= 1u << t.id;
  }
  w_.offsideMask = mask;
  w_.offsideTeam = team;
}

bool Match::checkOffsideOnTouch(Player& p) {
  if (!w_.offsideOn || w_.offsideMask == 0) return false;
  if (p.team != w_.offsideTeam) { w_.offsideMask = 0; return false; }
  if ((w_.offsideMask & (1u << p.id)) && w_.lastTouchTeam == p.team && p.id != w_.lastTouchId && w_.lastTouchKick) {
    ++w_.stats[p.team].offsides;
    pushEvent(EventType::Offside, p.team, p.id);
    Vec2 spot = clampPitch(p.pos, 0.5f);
    beginRestart(Restart::IndirectFreeKick, 1 - p.team, spot);
    return true;
  }
  return false;
}

void Match::endHalfOrMatch() {
  endPending_ = false;
  w_.owner = -1;
  if (w_.half == 1) {
    w_.phase = Phase::HalfTime;
    halfTimeTimer_ = 4.0f;
    w_.gameTime = 2700.0f;
    pushEvent(EventType::HalfTime, -1, -1);
  } else {
    w_.phase = Phase::FullTime;
    w_.gameTime = 5400.0f;
    pushEvent(EventType::FullTime, -1, -1);
  }
}

}  // namespace rx
