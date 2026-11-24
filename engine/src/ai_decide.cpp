#include "ai_util.h"

namespace rx {
using namespace aiutil;

namespace {
struct Opt {
  Act act = Act::None;
  Vec2 point;
  float ev = -1e9f;
  float power = 0.6f;
  int target = -1;
  float curl = 0;
  Vec2 move;
  float speed = 0.9f;
  bool sprint = false;
};

}  // namespace

// Decision engine for the player on the ball (or taking a restart). Every option is scored on one scale,
// "chance this turns into a goal minus the cost of losing the ball", then the best option is executed.
void TeamAI::carrier(const World& w, const Analysis& a, const Player& me, Per& st, Intent& it, bool restartTaker) {
  const AIProfile& pr = *prof_;
  const Vec2 goal = w.goalCentre(team_);
  const float dirX = static_cast<float>(World::dir(team_));
  const bool restart = restartTaker && w.phase != Phase::Live;
  const Restart rs = restart ? w.restart : Restart::None;
  const bool isGK = me.role == Role::GK;
  const Vec2 defMove = norm(goal - me.pos);
  (void)a;

  if (restart) {
    if (w.restartTimer < pr.reaction + 0.9f) { it.move = {0, 0}; it.speed = 0; return; }
  } else {
    if (st.haveBallT < pr.reaction) { it.move = norm(me.vel, defMove); it.speed = 0.45f; return; }
    st.thinkT -= dt_;
    const bool swarmed = nearestOpponent(w, team_, me.pos) < 2.6f;
    if (st.thinkT > 0 && !(swarmed && st.thinkT > pr.thinkInterval * 0.4f)) { it.move = st.last.move; it.speed = st.last.speed; it.sprint = st.last.sprint; it.dest = st.last.dest; return; }
    st.thinkT = pr.thinkInterval;
  }

  int nearId = -1;
  const float dOpp = nearestOpponent(w, team_, me.pos, &nearId);
  const float press = sat((3.5f - dOpp) / 3.0f);
  const float noise = (1.25f - pr.runQuality) * 0.014f;
  // Ported from PitchBot: game-state plan, crowd detection and counter-attack window shape every score below.
  const int mode = gameMode(w, team_);                       // 0 neutral, 1 early press, 2 protect lead, 3 chase
  int crowdN = 0;
  const float crowd = crowdAround(w, team_, me.pos, &crowdN);
  const bool crowded = crowdN >= 2 || dOpp < 3.5f;
  const bool counter = sinceWin_ < 4.0f && w.progress(team_, me.pos) > 0.25f;
  const float riskNow = clampf(pr.riskTolerance + (mode == 3 ? 0.2f : 0.0f) - (mode == 2 ? 0.2f : 0.0f) + (counter ? 0.12f : 0.0f), 0.0f, 1.0f);
  const float shootMul = pr.shootBias * (mode == 3 ? 1.3f : (mode == 2 ? 0.85f : 1.0f));
  Opt best;
  auto consider = [&](Opt o) {
    if (o.act == Act::None && o.speed <= 0 && o.ev < -1e8f) return;
    o.ev += rng_.gauss() * noise;
    if (o.ev > best.ev) best = o;
  };

  // ---- shooting
  const float dGoal = dist(me.pos, goal);
  const bool canShoot = !isGK && (rs == Restart::None || rs == Restart::FreeKick || rs == Restart::Penalty);
  if (canShoot && (dGoal < 34.0f || rs == Restart::Penalty)) {
    const Player& gk = w.p[(1 - team_) * kTeamSize];
    static const float offs[5] = {-2.9f, -1.6f, 0.0f, 1.6f, 2.9f};
    float bestQ = -1e9f, bestOpen = 0;
    int bestBlock = 0;
    Vec2 bestPt{goal.x, kMidY};
    for (float off : offs) {
      Vec2 pt{goal.x, kMidY + off};
      float open = gk.live() ? sat(std::fabs(gk.pos.y - pt.y) / 4.5f) : 1.0f;
      int block = 0;
      if (rs != Restart::Penalty) {
        for (int i = 0; i < kNumPlayers; ++i) {
          const Player& o = w.p[i];
          if (o.team == team_ || !o.live() || o.role == Role::GK) continue;
          float t = 0;
          if (distToSegment(o.pos, me.pos, pt, &t) < 1.0f && t > 0.1f && t < 0.95f) ++block;
        }
      }
      float q = open * 0.7f - 0.25f * block + 0.04f * std::fabs(off);
      if (q > bestQ) { bestQ = q; bestOpen = open; bestBlock = block; bestPt = pt; }
    }
    float xg = xgAt(me.pos, goal) * (0.55f + 0.55f * bestOpen) * std::pow(0.55f, static_cast<float>(bestBlock));
    if (rs == Restart::Penalty) xg = 0.78f;
    if (rs == Restart::FreeKick) xg *= 0.55f + 0.2f * pr.setPiece;
    Opt o;
    o.act = Act::Shoot;
    o.point = bestPt;
    o.power = rs == Restart::Penalty ? 0.8f : 0.92f;
    o.ev = xg * shootMul + 0.025f + 0.14f * sat((xg - 0.05f) / 0.10f);  // take clear chances
    // PitchBot "goal hunger": a clear lane inside the box is taken at once; long shots need an open lane and real xG.
    const bool inBox = std::fabs(me.pos.x - goal.x) < 17.0f && std::fabs(me.pos.y - kMidY) < 15.0f;
    if (rs == Restart::None) {
      if (inBox && bestBlock == 0) o.ev += 0.07f + 0.05f * pr.runQuality;
      if (!inBox && bestBlock == 0 && dGoal < 27.0f && xg >= 0.04f) o.ev += 0.05f * (0.5f + pr.runQuality);
      if (!inBox && (bestBlock > 0 || xg < 0.04f)) o.ev -= 0.06f;
    }
    if (rs == Restart::FreeKick && rng_.chance(0.6f * pr.setPiece)) o.curl = rng_.chance(0.5f) ? 1.0f : -1.0f;
    else if (rs == Restart::None && pr.runQuality > 0.6f && std::fabs(me.pos.y - kMidY) > 8.0f && dGoal < 24.0f && rng_.chance(0.3f))
      o.curl = me.pos.y < kMidY ? 1.0f : -1.0f;
    consider(o);
  }

  // ---- passing
  auto evalPass = [&](const Player& t, Act kind, Vec2 P) {
    Opt o;
    o.act = kind; o.point = P; o.target = t.id;
    float d = dist(me.pos, P);
    if (d < 2.0f || d > 62.0f) return o;
    bool lofted = kind == Act::Lob || kind == Act::Cross;
    float v0 = lofted ? 22.0f : groundSpeedFor(d, kind == Act::Through ? kThroughArrive : kPassArrive);
    float p = 1.0f - interceptRisk(w, team_, me.pos, P, v0, lofted, t.id);
    bool offsideRule = w.offsideOn && rs != Restart::ThrowIn && rs != Restart::GoalKick && rs != Restart::Corner;
    if (offsideRule && offsideAt(w, team_, t.pos, w.ballXY())) p *= 0.03f;
    float tBall = lofted ? d / 20.0f + 0.7f : groundTimeFor(v0, d);
    float tRecv = timeTo(t, P, 0.6f);
    if (tRecv > tBall + (kind == Act::Through ? 0.7f : 0.35f)) p *= 0.35f;
    p *= 0.8f + 0.2f * sat(nearestOpponent(w, team_, P) / 4.0f);
    if (lofted) p *= kind == Act::Cross ? 0.62f : 0.72f;
    float pv = possessionValue(w, team_, P) * (kind == Act::Cross ? 0.85f : 1.0f);
    float ev = p * pv - (1.0f - p) * lossCost(w, team_, (me.pos + P) * 0.5f, pr);
    // xT: reward the threat the pass gains, weighted by arrival chance (PitchBot's utility).
    ev += 0.55f * p * (xtAt(w, team_, P) - xtAt(w, team_, me.pos));
    // Chance creation: reward passes (through balls, cut-backs, crosses) that put a teammate in a better shooting spot.
    if (rs == Restart::None || rs == Restart::Corner || rs == Restart::FreeKick) {
      float gain = xgAt(P, goal) - xgAt(me.pos, goal);
      if (gain > 0.0f) ev += (0.9f + 0.8f * pr.runQuality) * p * gain;
    }
    float prog = w.progress(team_, P) - w.progress(team_, me.pos);
    if (prog < -0.02f) ev -= (1.0f - pr.patience) * 0.03f;
    if (p > 0.88f) ev += pr.patience * 0.03f;
    if (t.role == Role::GK) ev -= 0.05f;
    if (!lofted && kind == Act::Pass) {
      // Triangles: a pass that leaves a 40-80 degree support angle keeps the move alive (short tempo).
      ev += (0.025f + 0.02f * pr.patience) * triangleSupport(w, team_, me, t, P);
      // Crowded carrier: a short, safe, non-backward release beats everything else (fast-triangle build-up).
      if (crowded && d < 16.0f && p > 0.72f && prog > -0.10f) ev += 0.05f + 0.06f * crowd;
    }
    if (counter && prog > 0.05f && p > 0.5f) ev += 0.03f + 0.03f * pr.riskTolerance;   // counter window: go forward
    if (mode == 2) { ev += (p - 0.8f) * 0.06f; if (prog < 0) ev += 0.02f; }              // protect a lead: keep it
    if (mode == 3 && prog > 0.04f) ev += 0.03f;                                           // chase: forward balls
    o.ev = ev;
    return o;
  };

  const bool wideAttack = w.progress(team_, me.pos) > 0.7f && std::fabs(me.pos.y - kMidY) > 16.0f;
  for (int s = 0; s < kTeamSize; ++s) {
    int tid = team_ * kTeamSize + s;
    const Player& t = w.p[tid];
    if (tid == me.id || !t.live()) continue;
    float d = dist(me.pos, t.pos);
    if (rs == Restart::ThrowIn && d > 30.0f) continue;
    if (rs == Restart::Penalty) continue;
    bool inBox = std::fabs(t.pos.x - goal.x) < 18.0f && std::fabs(t.pos.y - kMidY) < 14.0f;
    if (rs == Restart::Corner) {
      if (inBox) consider(evalPass(t, Act::Cross, clampPitch(t.pos + t.vel * 0.4f)));
      else if (d < 18.0f) consider(evalPass(t, Act::Pass, clampPitch(t.pos + t.vel * 0.3f)));
      continue;
    }
    if (d >= (restart ? 2.0f : 3.0f) && !(isGK && d < 6.0f)) {
      float lead = clampf(d / 20.0f, 0.15f, 0.8f);
      consider(evalPass(t, Act::Pass, clampPitch(t.pos + t.vel * lead)));
    }
    if (t.role != Role::GK && rs != Restart::ThrowIn && riskNow > 0.2f && d > 6.0f) {
      Vec2 P2 = t.pos + norm(goal - t.pos) * lerpf(7.0f, 11.0f, riskNow);
      P2.y = lerpf(P2.y, kMidY, 0.15f);
      P2.x = clampf(P2.x, 3.0f, kPitchL - 3.0f);
      if (w.progress(team_, P2) > w.progress(team_, t.pos) + 0.04f && w.progress(team_, P2) < 0.98f)
        consider(evalPass(t, Act::Through, clampPitch(P2)));
    }
    if (d > 22.0f && rs != Restart::ThrowIn) consider(evalPass(t, Act::Lob, clampPitch(t.pos + t.vel * 0.8f)));
    if (wideAttack && inBox && rs != Restart::ThrowIn) consider(evalPass(t, Act::Cross, clampPitch(t.pos + t.vel * 0.4f)));
  }

  // ---- dribbling (open play only, outfield only)
  if (!restart && !isGK) {
    Vec2 cur = norm(me.vel, defMove);
    for (int k = 0; k < 16; ++k) {
      float ang = k * (2.0f * kPi / 16.0f);
      Vec2 dir{std::cos(ang), std::sin(ang)};
      Vec2 E = me.pos + dir * 4.0f;
      if (E.x < 1.5f || E.x > kPitchL - 1.5f || E.y < 1.5f || E.y > kPitchW - 1.5f) continue;
      float tMe = 4.0f / (0.88f * me.maxSpeed());
      float pLoss = 0;
      for (int i = 0; i < kNumPlayers; ++i) {
        const Player& o = w.p[i];
        if (o.team == team_ || !o.live()) continue;
        float tOpp = timeTo(o, E, 0.8f);
        if (tOpp < tMe + 0.25f) pLoss += 0.45f * sigmoid((tMe + 0.25f - tOpp) * 3.0f);
        if (distToSegment(o.pos + o.vel * 0.2f, me.pos, E) < 1.4f) pLoss += 0.35f;
      }
      pLoss = clampf(pLoss * (1.15f - 0.3f * me.dribbling), 0.0f, 0.92f);
      float ev = (1.0f - pLoss) * possessionValue(w, team_, E) - pLoss * lossCost(w, team_, me.pos, pr) + 0.01f * dot(dir, cur);
      ev += 0.025f * (1.0f - press) + 0.35f * (w.progress(team_, E) - w.progress(team_, me.pos)) * (1.0f - pLoss);
      if (crowded) ev -= 0.04f + 0.05f * crowd;           // do not dribble into the press: release instead
      if (mode == 2) ev -= 0.015f;
      Opt o;
      o.act = Act::None; o.move = dir; o.speed = 0.92f; o.ev = ev;
      o.sprint = pr.sprintUsage > 0.5f && press < 0.3f && me.stamina > 0.3f;
      o.point = E;
      consider(o);
    }
  }

  // ---- clearance under heavy pressure deep in our own half, and the keeper's default long kick
  if (!restart && !isGK && w.progress(team_, me.pos) < 0.28f && press > 0.4f) {
    Opt o;
    o.act = Act::Clear;
    o.point = {clampf(me.pos.x + dirX * 34.0f, 2.0f, kPitchL - 2.0f), me.pos.y < kMidY ? 6.0f : kPitchW - 6.0f};
    o.power = 0.9f;
    o.ev = press * 0.10f - 0.02f + (mode == 2 ? 0.03f : 0.0f);
    consider(o);
  }
  if (best.ev < -1e8f) {
    // No usable option (e.g. a restart with no teammates in range): fall back to a safe long ball or a plain pass.
    Opt o;
    o.act = restart && rs == Restart::KickOff ? Act::Pass : Act::Clear;
    float bd = 1e9f; int bt = -1;
    for (int s = 0; s < kTeamSize; ++s) {
      int tid = team_ * kTeamSize + s;
      if (tid == me.id || !w.p[tid].live()) continue;
      float d = dist(me.pos, w.p[tid].pos);
      if (d < bd && d > 2.0f) { bd = d; bt = tid; }
    }
    o.point = bt >= 0 ? w.p[bt].pos : Vec2{clampf(me.pos.x + dirX * 30.0f, 2.0f, kPitchL - 2.0f), kMidY};
    o.act = bt >= 0 ? Act::Pass : Act::Clear;
    o.target = bt;
    o.ev = 0;
    best = o;
  }

  it = Intent{};
  it.dest = best.point;
  if (best.act == Act::None) {
    it.move = best.move;
    it.speed = best.speed;
    it.sprint = best.sprint;
  } else {
    it.act = best.act;
    it.point = best.point;
    it.power = best.power;
    it.curl = best.curl;
    it.targetId = best.target;
    it.move = norm(me.vel, defMove);
    it.speed = 0.3f;
  }
  st.last = it;
  st.last.act = Act::None;
}

void TeamAI::keeper(const World& w, const Analysis& a, Per& st, Intent& it) {
  const AIProfile& pr = *prof_;
  const Player& gk = w.p[team_ * kTeamSize];
  const Vec2 ownGoal = w.ownGoal(team_);
  const float inward = team_ == 0 ? -1.0f : 1.0f;   // x direction pointing at our goal line
  const float goalX = ownGoal.x;

  // Holding the ball: wait a beat, then distribute through the normal decision engine.
  if (w.owner == gk.id) {
    st.holdT += dt_;
    it.move = {0, 0}; it.speed = 0;
    if (st.holdT >= 1.3f + pr.reaction * 2.0f) carrier(w, a, gk, st, it, false);
    return;
  }
  st.holdT = 0;

  // Shot / cross detection from the predicted ball path (spin-aware only for the sharper tiers).
  Vec3 tr[kTrackN];
  if (pr.gkRead >= 0.7f) {
    for (int i = 0; i < kTrackN; ++i) tr[i] = a.track[i];
  } else {
    Ball b = w.ball; b.spin = {};
    predictBall(b, kTrackStep, kTrackN, tr);
  }
  bool threat = false;
  float tCross = 0, yC = kMidY, zC = 0;
  if (w.owner < 0 && w.ball.vel.x * inward > 5.0f && w.lastTouchTeam != team_) {
    for (int i = 1; i < kTrackN; ++i) {
      bool crossed = team_ == 0 ? tr[i].x <= goalX : tr[i].x >= goalX;
      if (!crossed) continue;
      float denom = tr[i].x - tr[i - 1].x;
      float f = std::fabs(denom) > 1e-5f ? (goalX - tr[i - 1].x) / denom : 0.0f;
      yC = lerpf(tr[i - 1].y, tr[i].y, f);
      zC = lerpf(tr[i - 1].z, tr[i].z, f);
      tCross = (i - 1 + f) * kTrackStep;
      threat = std::fabs(yC - kMidY) < kGoalHalfW + 1.8f && zC < 3.0f;
      break;
    }
  }

  // Penalty: commit to a guess (using what the player tends to do) or read the strike, depending on tier.
  if (w.penaltyLive && w.since < 1.2f && !st.diveSent) {
    if (st.guess == 0.0f) {
      bool guessing = rng_.chance(1.0f - pr.gkRead * 0.6f);
      float lean = scout_ ? scout_->penaltyLean() : 0.0f;
      float side = (scout_ && rng_.chance(pr.adapt * 0.6f)) ? (lean >= 0 ? 1.0f : -1.0f) : (rng_.chance(0.5f) ? 1.0f : -1.0f);
      st.guess = guessing ? side : 2.0f;  // 2 = read the shot instead
      st.reactT = 0;
    }
    st.reactT += dt_;
    if (st.guess != 2.0f && st.reactT >= pr.gkReaction * 0.35f) {
      it.act = Act::Dive;
      it.point = {gk.pos.x, kMidY + st.guess * 2.6f};
      st.diveSent = true;
      return;
    }
  }
  if (!w.penaltyLive) st.guess = 0.0f;

  if (threat) {
    if (st.reactT < 0.0f) { st.reactT = 0; st.diveSent = false; }
    st.reactT += dt_;
    if (st.reactT >= pr.gkReaction) {
      float dy = yC - gk.pos.y;
      float tLeft = std::max(0.05f, tCross - st.reactT);
      float runReach = 0.85f * gk.maxSpeed() * tLeft + 0.6f;
      if (std::fabs(dy) <= 0.6f) {
        it.dest = {gk.pos.x, yC};
        it.move = norm(Vec2{0, dy}, {0, 0}); it.speed = std::fabs(dy) < 0.15f ? 0.0f : 0.5f;
      } else if (std::fabs(dy) <= runReach && zC < 1.8f) {
        it.dest = {gk.pos.x, yC};
        it.move = {0, dy > 0 ? 1.0f : -1.0f}; it.speed = 1.0f; it.sprint = true;
      } else if (!st.diveSent && gk.state != PState::Dive) {
        it.act = Act::Dive;
        it.point = {gk.pos.x, yC};
        it.power = zC;
        st.diveSent = true;
      }
    }
    return;
  }
  if (st.reactT >= 0.0f && !w.penaltyLive) { st.reactT = -1.0f; st.diveSent = false; }

  // Rush out only for loose balls inside our own penalty area; the keeper never leaves the box.
  const float boxDx = std::fabs(a.ballPt[gk.id].x - goalX), boxDy = std::fabs(a.ballPt[gk.id].y - kMidY);
  if (w.owner < 0 && a.interceptor == gk.id && a.tBall[gk.id] < a.oppBest - 0.15f && boxDx < kBoxDepth - 1.0f && boxDy < kBoxHalfW - 1.5f) {
    it.dest = a.ballPt[gk.id];
    it.move = norm(a.ballPt[gk.id] - gk.pos); it.speed = 1.0f; it.sprint = true;
    return;
  }
  // 1v1: narrow the angle.
  if (w.owner >= 0 && w.p[w.owner].team != team_) {
    const Player& c = w.p[w.owner];
    float dc = dist(c.pos, ownGoal);
    if (dc < 20.0f && w.progress(1 - team_, c.pos) > 0.78f) {
      float r = clampf(dc * 0.42f, 1.5f, 9.0f);
      Vec2 tgt = ownGoal + norm(c.pos - ownGoal) * r;
      it.dest = tgt;
      Vec2 d = tgt - gk.pos;
      it.move = norm(d, {0, 0}); it.speed = d.len() < 0.3f ? 0.0f : 0.8f;
      return;
    }
  }
  float dBall = dist(a.ballXY, ownGoal);
  float r = clampf(dBall * 0.17f, 1.2f, 7.0f);
  if (dBall > 38.0f && pr.lineHeight > 0.5f)
    r = std::max(r, lerpf(7.0f, 12.0f, sat((pr.lineHeight - 0.5f) * 2.0f)) * sat((dBall - 38.0f) / 30.0f));
  Vec2 pos = ownGoal + norm(a.ballXY - ownGoal) * r;
  if (r < 8.0f) pos.y = clampf(pos.y, kMidY - 5.0f, kMidY + 5.0f);
  Vec2 d = pos - gk.pos;
  it.dest = pos;
  it.move = norm(d, {0, 0});
  it.speed = d.len() < 0.3f ? 0.0f : sat(0.25f + d.len() * 0.3f);
}

}  // namespace rx
