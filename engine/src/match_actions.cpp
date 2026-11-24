#include "ai_util.h"
#include "realxi/ai.h"
#include "realxi/match.h"

namespace rx {
using namespace aiutil;

namespace {
constexpr float kDeg = kPi / 180.0f;

void shotVector(const Player& p, Vec2 dir, float power, float curl, Vec3& vel, Vec3& spin) {
  float v = lerpf(15.0f, 32.0f * (0.8f + 0.3f * p.shooting), power);
  float th = lerpf(1.5f, 13.0f, power * power) * kDeg;
  if (curl != 0.0f) { v *= 0.82f; th += 4.0f * kDeg; }
  vel = {dir.x * v * std::cos(th), dir.y * v * std::cos(th), v * std::sin(th)};
  spin = {0, 0, curl * 55.0f};
}

// Simulates a ball launched with vel/spin from `from` and returns y where it crosses the goal line x = gx (NaN if it never does).
float crossingY(Vec2 from, Vec3 vel, Vec3 spin, float gx, float* zOut = nullptr) {
  Ball s;
  s.pos = {from.x, from.y, kBallR};
  s.vel = vel;
  s.spin = spin;
  for (int i = 0; i < 180; ++i) {
    Vec3 prev = s.pos;
    stepBall(s, kDt);
    bool crossed = (gx > kPitchL * 0.5f) ? s.pos.x >= gx : s.pos.x <= gx;
    if (crossed) {
      float den = s.pos.x - prev.x;
      float f = std::fabs(den) > 1e-6f ? (gx - prev.x) / den : 0.0f;
      if (zOut) *zOut = lerpf(prev.z, s.pos.z, f);
      return lerpf(prev.y, s.pos.y, f);
    }
  }
  return std::nanf("");
}
}  // namespace

float Match::pressureOn(const Player& p) const {
  return sat((3.0f - nearestOpponent(w_, p.team, p.pos)) / 2.5f);
}

void Match::kick(Player& p, Vec3 vel, Vec3 spin, EventType type, int) {
  const bool restartKick = w_.phase != Phase::Live;
  if (restartKick) {
    if (w_.restart == Restart::Penalty) w_.penaltyLive = true;
    w_.phase = Phase::Live;
    w_.restart = Restart::None;
    w_.restartTimer = 0;
  }
  Ball& b = w_.ball;
  b.vel = vel;
  b.spin = spin;
  b.pos.z = std::max(b.pos.z, kBallR);
  w_.owner = -1;
  p.touchCD = 0.30f;
  p.state = PState::Kick;
  p.stateT = 0.2f;
  w_.lastTouchTeam = p.team;
  w_.lastTouchId = p.id;
  w_.lastTouchKick = true;
  w_.noHandleTeam = p.team;
  w_.since = 0;
  w_.passAge = 0;
  w_.passTeam = -1;
  w_.passTarget = -1;
  float sp = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
  pushEvent(type, p.team, p.id, static_cast<int>(sp * 10.0f));
}

void Match::doPass(Player& p, Act kind, Vec2 point, float power, int targetId) {
  const Restart rs = w_.phase == Phase::Live ? Restart::None : w_.restart;
  const Vec2 from = w_.ball.pos.xy();
  Vec2 d = point - from;
  float L = d.len();
  if (L < 1.0f) { d = {std::cos(p.facing), std::sin(p.facing)}; L = 1.0f; }
  const bool human = w_.humanControlled && p.id == w_.activeHuman;
  const AIProfile& pr = ai_[p.team]->profile();
  float base = human ? 2.5f : (rs != Restart::None ? pr.passErrDeg * (1.4f - 0.6f * pr.setPiece) : pr.passErrDeg);
  float err = base * (1.3f - 0.6f * p.passing) * (1.0f + 0.9f * pressureOn(p));
  Vec2 dir = rotate(norm(d), rng_.gauss() * err * kDeg);
  float speedFactor = 0.9f + 0.2f * power;
  Vec3 vel;
  if (kind == Act::Pass || kind == Act::Through) {
    float v0 = groundSpeedFor(L, kind == Act::Through ? kThroughArrive : kPassArrive) * speedFactor;
    v0 = std::min(v0, 33.0f + 4.0f * p.passing);
    if (rs == Restart::ThrowIn) v0 = std::min(v0, 15.0f);
    vel = {dir.x * v0, dir.y * v0, 0};
  } else {
    float th = (kind == Act::Cross ? 30.0f : 34.0f) * kDeg;
    if (rs == Restart::GoalKick) th = 36.0f * kDeg;
    float v = std::sqrt(L * kGravity / std::sin(2.0f * th)) * 1.07f * speedFactor;
    v = std::min(v, 34.0f);
    vel = {dir.x * v * std::cos(th), dir.y * v * std::cos(th), v * std::sin(th)};
  }
  kick(p, vel, {}, EventType::Pass, targetId);
  w_.passTeam = p.team;
  w_.passTarget = targetId;
  ++w_.stats[p.team].passes;
  bool offsideApplies = w_.offsideOn && rs != Restart::ThrowIn && rs != Restart::GoalKick && rs != Restart::Corner &&
                        rs != Restart::KickOff && rs != Restart::Penalty;
  if (offsideApplies) markOffsides(p.team, p.id);
  if (human) scout_.observePass(L > 25.0f);
}

void Match::doShot(Player& p, Vec2 point, float power, float curl, bool humanTaken) {
  const Vec2 from = w_.ball.pos.xy();
  const Vec2 goal = w_.goalCentre(p.team);
  Vec2 dir;
  const AIProfile& pr = ai_[p.team]->profile();
  const Restart rs = w_.phase == Phase::Live ? Restart::None : w_.restart;
  if (humanTaken) {
    dir = norm(point - p.pos);
    if (std::fabs(dir.x) > 0.05f && (goal.x - from.x) * dir.x > 0) {
      float t = (goal.x - from.x) / dir.x;
      float yi = from.y + dir.y * t;
      if (std::fabs(yi - kMidY) < kGoalHalfW + 3.5f) {
        float inside = clampf(yi, kMidY - 3.1f, kMidY + 3.1f);
        dir = norm(Vec2{goal.x, lerpf(yi, inside, cfg_.assist)} - from);
      }
    }
  } else {
    dir = norm(point - from);
  }
  // Clear chance (close, central, unpressed): the System places it into the corner away from the keeper.
  float clear = 0.0f, tierSkill = 0.0f;
  if (!humanTaken) {
    tierSkill = clampf((7.0f - pr.shotErrDeg) / 5.2f, 0.0f, 1.0f);  // Regular 0 .. Legend 1
    float xg = xgAt(from, goal);
    clear = sat((xg - 0.10f) / 0.20f) * (1.0f - pressureOn(p));
    if (clear > 0.0f && curl == 0.0f && std::fabs(goal.x - from.x) > 1.0f) {
      const Player& gk = w_.p[(1 - p.team) * kTeamSize];
      float gy = gk.live() ? gk.pos.y : kMidY;
      float side = (gy >= kMidY) ? -1.0f : 1.0f;
      float tgtY = kMidY + side * lerpf(1.6f, 3.0f, tierSkill);
      Vec2 tgt{goal.x, lerpf(point.y, tgtY, clear * (0.4f + 0.6f * tierSkill))};
      dir = norm(tgt - from);
    }
  }
  Vec3 vel, spin;
  if (curl != 0.0f && !humanTaken) {
    // AI bends the ball: search the launch angle so that it arrives at the chosen point after the curve.
    float a = angleOf(dir);
    float D = std::max(5.0f, std::fabs(goal.x - from.x));
    for (int it = 0; it < 5; ++it) {
      shotVector(p, {std::cos(a), std::sin(a)}, power, curl, vel, spin);
      float yc = crossingY(from, vel, spin, goal.x);
      if (std::isnan(yc)) break;
      a -= (yc - point.y) / D;
    }
    dir = {std::cos(a), std::sin(a)};
  }
  float base = humanTaken ? 2.5f : (rs == Restart::FreeKick ? pr.shotErrDeg * (1.3f - 0.5f * pr.setPiece) : pr.shotErrDeg);
  float sprintFrac = sat(p.vel.len() / p.maxSpeed());
  float err = base * (1.4f - 0.8f * p.shooting) * (1.0f + 0.8f * pressureOn(p)) * (1.0f + 0.4f * sprintFrac);
  if (power > 0.92f) err *= 1.0f + (power - 0.92f) * 4.0f;
  if (w_.owner != p.id && rs == Restart::None) err *= 1.5f - 0.35f * clear * tierSkill;  // first-time strike
  err *= 1.0f - clear * (0.35f + 0.45f * tierSkill);
  dir = rotate(dir, rng_.gauss() * err * kDeg);
  shotVector(p, dir, power, curl, vel, spin);

  // Bookkeeping before the kick: on-target check from the actual launch vector.
  float z = 0;
  float yc = crossingY(from, vel, spin, goal.x, &z);
  bool onTarget = !std::isnan(yc) && std::fabs(yc - kMidY) < kGoalHalfW && z < kGoalH;
  kick(p, vel, spin, EventType::Shot, -1);
  ++w_.stats[p.team].shots;
  if (onTarget) ++w_.stats[p.team].shotsOnTarget;
  w_.stats[p.team].xg += xgAt(from, goal);
  if (humanTaken) scout_.observeShot(std::isnan(yc) ? kMidY : yc);
}

void Match::doClear(Player& p, Vec2 point, float) {
  const Vec2 from = w_.ball.pos.xy();
  Vec2 d = point - from;
  float L = clampf(d.len(), 20.0f, 48.0f);
  Vec2 dir = rotate(norm(d), rng_.gauss() * 6.0f * kDeg);
  float th = 38.0f * kDeg;
  float v = std::min(34.0f, std::sqrt(L * kGravity / std::sin(2.0f * th)) * 1.07f);
  kick(p, {dir.x * v * std::cos(th), dir.y * v * std::cos(th), v * std::sin(th)}, {}, EventType::Kick, -1);
}

void Match::doSkill(Player& p, int skill) {
  if (p.state == PState::Slide || p.state == PState::Recover || p.state == PState::Dive) return;
  Vec2 f{std::cos(p.facing), std::sin(p.facing)};
  Vec2 stick{in_.moveX, in_.moveY};
  float side = (w_.humanControlled && p.id == w_.activeHuman && stick.len() > 0.2f) ? (cross(f, norm(stick)) >= 0 ? 1.0f : -1.0f)
                                                                                      : (rng_.chance(0.5f) ? 1.0f : -1.0f);
  p.skillT = 0.6f;
  float bite = 0;
  switch (skill) {
    case 0: p.vel += perp(f) * (2.4f * side); bite = 0.5f; break;                     // step over
    case 1: p.vel += perp(f) * (-2.0f * side); bite = 0.55f; break;                    // body feint
    case 2: p.facing = wrapAngle(p.facing + kPi); p.vel *= -0.5f; bite = 0.3f; break; // quick turn
    case 3: p.vel = f * -3.2f; bite = 0.35f; break;                                    // drag back
    default: p.fakeT = 0.45f; bite = 0.8f; break;                                      // fake shot
  }
  for (int i = 0; i < kNumPlayers; ++i) {
    Player& o = w_.p[i];
    if (o.team == p.team || !o.live() || o.state == PState::Slide || o.state == PState::Dive) continue;
    if (dist(o.pos, p.pos) > 4.0f) continue;
    float fr = ai_[o.team]->profile().feintResist;
    if (!(w_.humanControlled && o.team == 0) && rng_.chance(bite * (1.0f - fr))) {
      o.state = PState::Recover;
      o.stateT = 0.35f;
    }
  }
  pushEvent(EventType::Skill, p.team, p.id, skill);
}

void Match::tackleAttempt(Player& p, bool slide) {
  if (p.tackleCD > 0 || w_.phase != Phase::Live) return;
  if (p.state == PState::Slide || p.state == PState::Recover || p.state == PState::Dive) return;
  Vec2 f{std::cos(p.facing), std::sin(p.facing)};
  if (slide) {
    p.state = PState::Slide;
    p.stateT = 0.55f;
    p.slideHit = false;
    p.vel = f * 9.0f;
    p.tackleCD = 1.5f;
    pushEvent(EventType::Tackle, p.team, p.id, 1);
    return;
  }
  p.tackleCD = 0.8f;
  if (w_.owner >= 0 && w_.p[w_.owner].team != p.team) {
    Player& c = w_.p[w_.owner];
    Vec2 d = c.pos - p.pos;
    if (d.len() < 1.45f && dot(f, norm(d)) > 0.2f) {
      ++w_.stats[p.team].tackles;
      pushEvent(EventType::Tackle, p.team, p.id, 0);
      resolveTackleContact(p, c, false);
      return;
    }
  } else if (w_.owner < 0 && dist(p.pos, w_.ball.pos.xy()) < 1.3f && w_.ball.pos.z < 0.6f && p.touchCD <= 0) {
    w_.ball.vel = {f.x * 6.0f, f.y * 6.0f, 0};
    w_.lastTouchTeam = p.team; w_.lastTouchId = p.id; w_.lastTouchKick = false;
    w_.passTeam = -1;
    if (w_.noHandleTeam != p.team) w_.noHandleTeam = -1;
    p.touchCD = 0.3f;
    return;
  }
  p.state = PState::Tackle;
  p.stateT = 0.35f;
}

void Match::resolveTackleContact(Player& t, Player& c, bool slide) {
  Vec2 tf{std::cos(t.facing), std::sin(t.facing)};
  Vec2 cf{std::cos(c.facing), std::sin(c.facing)};
  float align = dot(tf, norm(c.pos - t.pos));
  float behind = dot(cf, norm(t.pos - c.pos));  // < 0 means the tackler came from behind
  float p = 0.42f + 0.50f * (t.tackling - c.dribbling) + 0.12f * align + (slide ? 0.10f : 0.0f) -
            (c.skillT > 0 ? 0.35f : 0.0f) - 0.15f * sat(c.vel.len() / c.maxSpeed());
  p = clampf(p, 0.06f, 0.88f);
  bool fromBehind = behind < -0.35f;
  float foulP = (slide ? 0.30f : 0.08f) + (fromBehind ? 0.35f : 0.0f);
  float refMul = w_.refereeProfile == 0 ? 0.7f : (w_.refereeProfile == 2 ? 1.3f : 1.0f);
  foulP *= refMul;
  if (rng_.chance(p)) {
    if (slide && fromBehind && w_.foulsOn && rng_.chance(0.15f * refMul)) { foul(t, c, slide, true); return; }
    // Clean win: a strong, well-aligned tackle takes the ball outright; otherwise it squirts loose. Either way the carrier is rocked.
    const bool takes = rng_.chance(0.30f + 0.35f * t.tackling + 0.2f * align - 0.2f * c.dribbling);
    Vec2 away = norm(tf * 0.7f + Vec2{rng_.range(-0.5f, 0.5f), rng_.range(-0.5f, 0.5f)});
    w_.ball.vel = {away.x * (takes ? 2.0f : 5.5f), away.y * (takes ? 2.0f : 5.5f), 0.4f};
    w_.owner = -1;
    c.touchCD = 0.6f;
    c.state = PState::Recover; c.stateT = slide ? 0.7f : 0.5f;           // knocked off stride: possession really is lost
    c.vel = c.vel * 0.3f;
    if (takes && !slide) { t.touchCD = 0.0f; }
    w_.lastTouchTeam = t.team; w_.lastTouchId = t.id; w_.lastTouchKick = false;
    w_.passTeam = -1;
    if (w_.noHandleTeam != t.team) w_.noHandleTeam = -1;
    ++w_.stats[t.team].tacklesWon;
    pushEvent(EventType::TackleWon, t.team, t.id, slide ? 1 : 0);
  } else if (w_.foulsOn && rng_.chance(foulP)) {
    foul(t, c, slide, fromBehind);
  } else {
    t.state = PState::Recover;
    t.stateT = 0.45f;
  }
}

void Match::keeperPhysics() {
  if (w_.owner >= 0 || w_.phase != Phase::Live) return;
  Ball& b = w_.ball;
  for (int team = 0; team < 2; ++team) {
    Player& gk = w_.p[team * kTeamSize];
    if (!gk.live() || gk.touchCD > 0 || w_.noHandleTeam == team) continue;
    bool inBox = (team == 0 ? gk.pos.x < kBoxDepth + 1.0f : gk.pos.x > kPitchL - kBoxDepth - 1.0f) &&
                 std::fabs(gk.pos.y - kMidY) < kBoxHalfW + 1.0f;
    if (!inBox) continue;
    bool diving = gk.state == PState::Dive;
    float reach = diving ? lerpf(1.5f, 2.3f, gk.keeping) * ai_[team]->profile().gkReach : 0.85f;
    float reachV = diving ? 2.5f : 2.3f;
    if (dist(b.pos.xy(), gk.pos) > reach || b.pos.z > reachV) continue;
    float speed = std::sqrt(b.vel.x * b.vel.x + b.vel.y * b.vel.y + b.vel.z * b.vel.z);
    float pc = clampf(1.18f - speed / (27.0f * (0.58f + 0.58f * gk.keeping)), 0.07f, 0.98f);
    if (speed < 12.0f) pc = std::max(pc, 0.90f);     // soft and central shots are simply collected
    if (diving) pc *= 0.85f;
    bool shotFaced = speed > 8.0f && w_.lastTouchTeam != team;
    if (shotFaced && w_.lastTouchTeam >= 0 && !(w_.humanControlled && w_.lastTouchTeam == 0)) {
      // System finishers: a hard, well-placed strike (ball not right at the keeper) is harder to keep out at higher tiers.
      float skill = clampf((7.0f - ai_[w_.lastTouchTeam]->profile().shotErrDeg) / 5.2f, 0.0f, 1.0f);
      float placed = sat((dist(b.pos.xy(), gk.pos) - 0.3f) / 1.2f);
      if (speed > 18.0f) pc *= 1.0f - 0.45f * skill * placed;
    }
    if (rng_.chance(pc)) {
      w_.owner = gk.id;
      w_.lastTouchTeam = team; w_.lastTouchId = gk.id; w_.lastTouchKick = false;
      w_.passTeam = -1; w_.noHandleTeam = -1; w_.penaltyLive = false; w_.offsideMask = 0;
      w_.since = 0;
      b.vel = {0, 0, 0};
      if (shotFaced) { ++w_.stats[team].saves; pushEvent(EventType::Save, team, gk.id, 1); }
      else pushEvent(EventType::Catch, team, gk.id);
      if (diving) { gk.state = PState::Recover; gk.stateT = 0.4f; }
    } else {
      float sign = team == 0 ? 1.0f : -1.0f;  // away from our own goal
      b.vel = {sign * std::max(3.0f, 0.35f * speed), rng_.range(-4.0f, 4.0f), rng_.range(1.5f, 4.0f)};
      b.spin = {};
      gk.touchCD = 0.5f;
      w_.lastTouchTeam = team; w_.lastTouchId = gk.id; w_.lastTouchKick = false;
      w_.passTeam = -1; w_.penaltyLive = false;
      if (shotFaced) ++w_.stats[team].saves;
      pushEvent(EventType::Save, team, gk.id, 0);
    }
    break;
  }
}

// Shows the human where the shot would go while the shoot button is held (training aid; costs 12 sim steps).
void Match::updatePreview() {
  previewActive_ = false;
  for (int i = 0; i < 12; ++i) predicted_[i] = {};
  if (!w_.humanControlled || w_.activeHuman < 0 || !in_.shoot) return;
  const Player& p = w_.p[w_.activeHuman];
  if (!canKick(p)) return;
  Vec2 stick{in_.moveX, in_.moveY};
  Vec2 face{std::cos(p.facing), std::sin(p.facing)};
  Vec2 dir = stick.len() > 0.12f ? norm(stick) : face;
  float power = 0.35f + 0.65f * sat(chargeShoot_ / 0.9f);
  float curl = 0;
  if (in_.finesse) curl = cross(dir, w_.goalCentre(0) - p.pos) >= 0 ? 1.0f : -1.0f;
  Vec3 vel, spin;
  shotVector(p, dir, power, curl, vel, spin);
  Ball s;
  s.pos = {w_.ball.pos.x, w_.ball.pos.y, kBallR};
  s.vel = vel; s.spin = spin;
  for (int i = 0; i < 12; ++i) {
    for (int k = 0; k < 7; ++k) stepBall(s, kDt);
    predicted_[i] = s.pos;
  }
  previewActive_ = true;
}

}  // namespace rx
