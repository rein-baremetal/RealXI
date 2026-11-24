#include "ai_util.h"

namespace rx {
using namespace aiutil;


float Scouting::favouredChannel() const {
  float tot = lane[0] + lane[1] + lane[2];
  return (lane[2] - lane[0]) / tot;
}

TeamAI::TeamAI(int team, int tier, int formationIdx, uint32_t seed)
    : team_(team), prof_(&aiProfile(tier)), tier_(tier), formation_(formationIdx), rng_(seed * 2654435761u + team * 7919u + 17u) {}

void TeamAI::setTier(int tier) { tier_ = std::clamp(tier, 0, kNumTiers - 1); prof_ = &aiProfile(tier_); }

Vec2 TeamAI::baseAnchor(const World&, int slot) const {
  return slotToWorld(formation(formation_).slots[slot], team_);
}

void TeamAI::analyse(const World& w, Analysis& a) const {
  a.ballXY = w.ballXY();
  a.ballProg = w.progress(team_, a.ballXY);
  a.carrierId = w.owner;
  a.ours = w.owner >= 0 && w.p[w.owner].team == team_;
  a.theirs = w.owner >= 0 && w.p[w.owner].team != team_;
  a.loose = w.owner < 0;
  predictBall(w.ball, kTrackStep, kTrackN, a.track);
  for (int i = 0; i < kNumPlayers; ++i) {
    const Player& p = w.p[i];
    a.tBall[i] = 1e9f;
    a.ballPt[i] = a.ballXY;
    if (!p.live()) continue;
    if (a.loose) {
      Vec2 where = a.ballXY;
      float t = interceptTime(p, a.track, kTrackN, &where);
      if (t >= 1e8f) { t = timeTo(p, a.track[kTrackN - 1].xy(), 0.7f) + 2.0f; where = a.track[kTrackN - 1].xy(); }
      a.tBall[i] = t; a.ballPt[i] = where;
    } else {
      const Player& c = w.p[w.owner];
      Vec2 q = c.pos + c.vel * 0.3f;
      a.tBall[i] = timeTo(p, q, 0.9f);
      a.ballPt[i] = q;
    }
    if (p.role == Role::GK && !a.loose) a.tBall[i] += 3.0f;  // keepers do not press
    if (p.team == team_) {
      if (a.tBall[i] < a.ourBest) { a.ourBest = a.tBall[i]; a.interceptor = i; }
    } else if (a.tBall[i] < a.oppBest) {
      a.oppBest = a.tBall[i]; a.oppInterceptor = i;
    }
  }
}

Vec2 TeamAI::shapeTarget(const World&, const Analysis& a, int slot, bool attacking, const Scouting* sc) const {
  const FormationSlot& s = formation(formation_).slots[slot];
  float prog;
  if (s.role == Role::GK) {
    prog = 0.04f;
  } else {
    float baseLine = lerpf(0.16f, 0.36f, prof_->lineHeight);
    float shift = (a.ballProg - 0.5f) * (attacking ? 0.60f : 0.44f);
    float L = clampf(baseLine + shift + (attacking ? 0.10f : 0.0f), 0.10f, 0.58f);
    prog = L + (s.y - 0.23f) * (attacking ? 1.05f : 0.80f);
    if (trapOn_ && !attacking && lineOf(s.role) == Line::Def) prog = L;
    // Forwards never retreat behind the ball line by much; they stay as outlets.
    if (lineOf(s.role) == Line::Att && !attacking) prog = std::max(prog, std::min(0.62f, a.ballProg - 0.04f));
    prog = clampf(prog, 0.04f, 0.93f);
  }
  float ballLat = team_ == 0 ? a.ballXY.y / kPitchW : 1.0f - a.ballXY.y / kPitchW;
  float compact = attacking ? 0.18f : 0.36f;
  float lat = lerpf(s.x, ballLat, compact * (lineOf(s.role) == Line::Att ? 0.6f : 1.0f));
  bool wide = s.role == Role::LM || s.role == Role::RM || s.role == Role::LW || s.role == Role::RW ||
              s.role == Role::LWB || s.role == Role::RWB;
  if (attacking && wide) lat = clampf(s.x + (s.x - 0.5f) * 0.14f, 0.04f, 0.96f);
  Vec2 world = slotToWorld({s.role, lat, prog}, team_);
  if (!attacking && sc && prof_->adapt > 0.05f) {
    // Slide the block toward the channel the player uses most (up to ~4 m at the top tier).
    world.y += sc->favouredChannel() * 4.0f * prof_->adapt;
  }
  return clampPitch(world, 1.0f);
}

static void goTo(const AIProfile& pr, const Player& me, Vec2 target, float urgency, Intent& it) {
  Vec2 d = target - me.pos;
  float L = d.len();
  it.dest = target;
  if (L < 0.3f) { it.move = {0, 0}; it.speed = 0; return; }
  it.move = d * (1.0f / L);
  float s = urgency;
  if (L < 3.0f) s = std::min(s, 0.15f + L * 0.3f);
  it.speed = sat(s);
  float thresh = lerpf(0.65f, 0.15f, pr.sprintUsage);
  it.sprint = urgency > 0.9f && L > 4.5f && me.stamina > thresh;
}

void TeamAI::think(const World& w, float dt, int humanId, const Scouting* sc, std::array<Intent, kNumPlayers>& out) {
  dt_ = dt;
  scout_ = sc;
  Analysis a;
  analyse(w, a);
  for (int slot = 0; slot < kTeamSize; ++slot) {
    out[team_ * kTeamSize + slot] = Intent{};
    out[team_ * kTeamSize + slot].dest = w.p[team_ * kTeamSize + slot].pos;
    per_[slot].tackleWait = std::max(0.0f, per_[slot].tackleWait - dt);
    if (w.owner == team_ * kTeamSize + slot) per_[slot].haveBallT += dt; else per_[slot].haveBallT = 0;
  }
  if (runRequest_ > 0) runRequest_ -= dt;
  if (pressRequest_ > 0) pressRequest_ -= dt;
  if (oneTwoT_ > 0) oneTwoT_ -= dt;
  {
    bool ours = a.ours;
    if (ours && !hadBall_) sinceWin_ = 0; else sinceWin_ += dt;
    if (ours) sinceOwn_ += dt; else sinceOwn_ = 0;
    hadBall_ = ours;
  }
  trapCheck_ -= dt;
  if (trapOn_) { trapT_ -= dt; if (trapT_ <= 0) trapOn_ = false; }

  if (w.phase == Phase::GoalPause || w.phase == Phase::HalfTime || w.phase == Phase::FullTime ||
      w.phase == Phase::DrillResult) {
    return;
  }
  if (w.phase != Phase::Live) {
    setPiece(w, a, humanId, sc, out);
  } else if (a.ours) {
    attackOffBall(w, a, humanId, sc, out);
  } else if (a.theirs) {
    defend(w, a, humanId, sc, out);
  } else {
    looseBall(w, a, humanId, out);
  }
  // Keeper: always handled by its own logic during live play.
  int gkId = team_ * kTeamSize;
  if (w.phase == Phase::Live && gkId != humanId && w.p[gkId].live()) {
    Per& st = per_[0];
    if (w.owner != gkId) st.holdT = 0;
    keeper(w, a, st, out[gkId]);
  }
}

void TeamAI::looseBall(const World& w, const Analysis& a, int humanId, std::array<Intent, kNumPlayers>& out) {
  bool ourSide = a.ballProg < 0.5f;
  int chasers = 0;
  // Our two fastest to the ball go for it (when we are not clearly beaten and it is worth the run).
  int order[kTeamSize];
  int n = 0;
  for (int s = 1; s < kTeamSize; ++s) order[n++] = s;
  std::sort(order, order + n, [&](int x, int y) { return a.tBall[team_ * 11 + x] < a.tBall[team_ * 11 + y]; });
  for (int k = 0; k < n; ++k) {
    int slot = order[k];
    int id = team_ * kTeamSize + slot;
    const Player& me = w.p[id];
    if (id == humanId || !me.live()) continue;
    Intent& it = out[id];
    bool intended = w.passTeam == team_ && w.passTarget == id;
    const int maxChasers = 1 + (prof_->pressIntensity > 0.45f ? 1 : 0) + (humanId >= 0 ? 1 : 0) + (pressRequest_ > 0 ? 1 : 0);
    bool go = intended || (chasers < maxChasers && a.tBall[id] < 6.0f);
    if (go && (intended || a.tBall[id] < a.oppBest + 1.0f || ourSide || dist(me.pos, a.ballPt[id]) < 12.0f)) {
      goTo(*prof_, me, a.ballPt[id], 1.0f, it);
      ++chasers;
    } else {
      goTo(*prof_, me, shapeTarget(w, a, slot, !ourSide, nullptr), 0.7f, it);
    }
  }
}

void TeamAI::defend(const World& w, const Analysis& a, int humanId, const Scouting* sc, std::array<Intent, kNumPlayers>& out) {
  const Player& car = w.p[a.carrierId];
  int oppTeam = 1 - team_;
  Vec2 ownGoal = w.ownGoal(team_);
  // Offside trap decision: re-evaluated every 1.5 s, held for 2.5 s.
  if (trapCheck_ <= 0) {
    trapCheck_ = 1.5f;
    bool pressed = false;
    for (int i = 0; i < kNumPlayers; ++i)
      if (w.p[i].team == team_ && w.p[i].live() && w.p[i].role != Role::GK && dist(w.p[i].pos, car.pos) < 4.0f) pressed = true;
    if (!trapOn_ && pressed && a.ballProg < 0.62f && rng_.chance(prof_->offsideTrap) && w.offsideOn) {
      trapOn_ = true; trapT_ = 2.5f;
    }
  }
  float carProgThem = w.progress(oppTeam, car.pos);
  // Who presses: the fastest outfield player, then a cover player behind him.
  int presser = -1, cover = -1;
  float best = 1e9f, second = 1e9f;
  for (int s = 1; s < kTeamSize; ++s) {
    int id = team_ * kTeamSize + s;
    if (!w.p[id].live() || id == humanId) continue;
    float t = a.tBall[id];
    if (t < best) { second = best; cover = presser; best = t; presser = id; }
    else if (t < second) { second = t; cover = id; }
  }
  // Press zone: tougher tiers hunt higher up the pitch.
  float pressLine = lerpf(0.35f, 0.85f, prof_->pressIntensity);
  bool pressOn = a.ballProg <= pressLine || carProgThem > 0.5f || pressRequest_ > 0;
  if (presser >= 0 && !pressOn) { cover = -1; }

  // Threat list for marking.
  struct Threat { int id; float v; };
  Threat threats[kNumPlayers];
  int nt = 0;
  for (int i = 0; i < kNumPlayers; ++i) {
    const Player& o = w.p[i];
    if (o.team != oppTeam || !o.live() || i == a.carrierId || o.role == Role::GK) continue;
    float v = possessionValue(w, oppTeam, o.pos) + 0.25f * sat(1.0f - dist(o.pos, car.pos) / 40.0f);
    threats[nt++] = {i, v};
  }
  std::sort(threats, threats + nt, [](const Threat& x, const Threat& y) { return x.v > y.v; });
  bool taken[kTeamSize] = {false};
  int markOf[kTeamSize];
  for (int s = 0; s < kTeamSize; ++s) markOf[s] = -1;
  int maxMarks = 2 + static_cast<int>(prof_->markingQuality * 3.0f);
  for (int k = 0; k < nt && k < maxMarks; ++k) {
    const Player& o = w.p[threats[k].id];
    if (dist(o.pos, ownGoal) > 42.0f && w.progress(oppTeam, o.pos) < 0.55f) continue;
    int bestSlot = -1;
    float bestCost = 1e9f;
    for (int s = 1; s < kTeamSize; ++s) {
      int id = team_ * kTeamSize + s;
      if (taken[s] || id == presser || id == cover || id == humanId || !w.p[id].live()) continue;
      Line ln = lineOf(w.p[id].role);
      if (ln == Line::Att) continue;
      float c = dist(w.p[id].pos, o.pos) + 0.4f * dist(shapeTarget(w, a, s, false, nullptr), o.pos);
      if (ln == Line::Def) c -= 3.0f;
      if (c < bestCost) { bestCost = c; bestSlot = s; }
    }
    if (bestSlot >= 0 && bestCost < 34.0f) { taken[bestSlot] = true; markOf[bestSlot] = threats[k].id; }
  }

  for (int s = 1; s < kTeamSize; ++s) {
    int id = team_ * kTeamSize + s;
    const Player& me = w.p[id];
    if (id == humanId || !me.live()) continue;
    Per& st = per_[s];
    Intent& it = out[id];
    if (id == presser && pressOn) {
      Vec2 cpos = car.pos + car.vel * 0.25f;
      Vec2 gs = norm(ownGoal - cpos);
      float d = dist(me.pos, cpos);
      Vec2 tgt = d > 4.0f ? cpos + gs * 1.2f : cpos + gs * std::max(0.9f, 1.9f - prof_->aggression);
      goTo(*prof_, me, tgt, 1.0f, it);
      st.thinkT -= dt_;
      if (me.tackleCD <= 0 && st.tackleWait <= 0 && me.state != PState::Slide) {
        float reach = 1.5f;
        if (d < reach) {
          float want = 0.25f + 0.6f * prof_->aggression;
          if (car.skillT > 0 && rng_.chance(prof_->feintResist)) want *= 0.3f;
          if (rng_.chance(want * 0.12f + 0.02f)) { it.act = Act::Tackle; st.tackleWait = lerpf(1.2f, 0.5f, prof_->aggression); }
        } else if (d < 3.2f && prof_->aggression > 0.45f && carProgThem > 0.55f && rng_.chance(0.004f + 0.01f * prof_->aggression)) {
          // Last-ditch lunge when the carrier is bearing down on goal.
          it.act = Act::Slide; st.tackleWait = 1.6f;
        }
      }
      continue;
    }
    if (id == cover && pressOn && presser >= 0) {
      const Player& pr = w.p[presser];
      Vec2 tgt = pr.pos + norm(ownGoal - pr.pos) * 5.0f;
      tgt.y = lerpf(tgt.y, kMidY, 0.15f);
      goTo(*prof_, me, clampPitch(tgt), 0.95f, it);
      continue;
    }
    // Anyone within 11 m of the carrier (or 18 m on a team press) squeezes in so possession is actually contested.
    if (pressOn && (humanId >= 0 || pressRequest_ > 0 || prof_->pressIntensity > 0.75f) && id != presser && id != cover && lineOf(me.role) != Line::Def &&
        dist(me.pos, car.pos) < (pressRequest_ > 0 ? 18.0f : 11.0f)) {
      Vec2 gs = norm(ownGoal - car.pos);
      goTo(*prof_, me, car.pos + gs * 2.2f, 0.95f, it);
      continue;
    }
    Vec2 base = shapeTarget(w, a, s, false, sc);
    if (markOf[s] >= 0) {
      const Player& o = w.p[markOf[s]];
      float goalSide = lerpf(3.0f, 1.2f, prof_->markingQuality);
      Vec2 tgt = o.pos + o.vel * 0.3f + norm(ownGoal - o.pos) * goalSide + (a.ballXY - o.pos) * 0.04f;
      if (prof_->markingQuality > 0.75f && lineOf(me.role) == Line::Mid) tgt = lerp(tgt, car.pos, 0.12f);  // lane cutting
      // Do not abandon the line completely: stay within 14 m of the shape position.
      Vec2 off = tgt - base;
      if (off.len() > 14.0f) tgt = base + norm(off) * 14.0f;
      goTo(*prof_, me, clampPitch(tgt), 0.9f, it);
    } else {
      goTo(*prof_, me, base, 0.75f, it);
    }
  }
}

void TeamAI::attackOffBall(const World& w, const Analysis& a, int humanId, const Scouting* sc, std::array<Intent, kNumPlayers>& out) {
  const Player& car = w.p[a.carrierId];
  float line = offsideLineProgress(w, team_);
  bool carPressed = nearestOpponent(w, team_, car.pos) < 3.0f;
  for (int s = 0; s < kTeamSize; ++s) {
    int id = team_ * kTeamSize + s;
    const Player& me = w.p[id];
    if (id == humanId || !me.live() || s == 0) continue;
    Per& st = per_[s];
    Intent& it = out[id];
    if (id == a.carrierId) { carrier(w, a, me, st, it, false); continue; }
    if (id == oneTwoId_ && oneTwoT_ > 0) {
      // Pass-and-run: break past the carrier into the channel ahead and offer the return ball.
      const float dirX = static_cast<float>(World::dir(team_));
      float side = me.pos.y < car.pos.y ? -1.0f : 1.0f;
      Vec2 tgt = clampPitch({car.pos.x + dirX * 14.0f, car.pos.y + side * 7.0f}, 1.5f);
      goTo(*prof_, me, tgt, 1.0f, it);
      it.sprint = true;
      continue;
    }
    st.runHold -= dt_;
    Vec2 anchor = shapeTarget(w, a, s, true, sc);
    bool onsideNow = w.progress(team_, me.pos) <= line + 0.002f;
    if (st.runHold <= 0) {
      Line ln = lineOf(me.role);
      int samples = 4 + static_cast<int>(prof_->runQuality * 8.0f);
      float R = lerpf(5.0f, 12.0f, prof_->runQuality);
      Vec2 bestP = anchor;
      float bestScore = -1e9f;
      auto score = [&](Vec2 c, bool behind) {
        float sc2 = 1.2f * possessionValue(w, team_, c);
        sc2 += 0.35f * sat(nearestOpponent(w, team_, c) / 7.0f);
        float v0 = groundSpeedFor(std::max(3.0f, dist(car.pos, c)), 7.0f);
        sc2 += 0.30f * (1.0f - interceptRisk(w, team_, car.pos, c, v0, false, id));
        sc2 -= 0.012f * dist(c, anchor);
        int crowd = 0;
        for (int k = 0; k < kTeamSize; ++k) {
          int oid = team_ * kTeamSize + k;
          if (oid != id && w.p[oid].live() && dist(w.p[oid].pos, c) < 6.0f) ++crowd;
        }
        sc2 -= 0.12f * crowd;
        if (w.offsideOn && w.progress(team_, c) > line - 0.004f) sc2 -= (behind && onsideNow) ? 0.05f : 1.0f;
        if (carPressed) sc2 += 0.25f * std::exp(-std::pow(dist(c, car.pos) - 9.0f, 2.0f) / 60.0f);
        if (behind) sc2 += 0.10f + (runRequest_ > 0 ? 0.35f : 0.0f);
        return sc2;
      };
      bestScore = score(anchor, false);
      if (carPressed && me.role != Role::GK) {
        // PitchBot triangle support: offer a short outlet 7-11 m from the carrier at +-55 degrees to the attack line.
        const float dirX = static_cast<float>(World::dir(team_));
        for (int sgn = -1; sgn <= 1; sgn += 2) {
          float ang = sgn * 55.0f * kPi / 180.0f;
          float r = rng_.range(7.0f, 11.0f);
          Vec2 c = clampPitch({car.pos.x + dirX * std::cos(ang) * r * 0.6f, car.pos.y + std::sin(ang) * r}, 1.0f);
          float sc2 = score(c, false) + 0.08f - 0.01f * dist(c, anchor) * 0.5f;
          if (sc2 > bestScore) { bestScore = sc2; bestP = c; }
        }
      }
      for (int k = 0; k < samples; ++k) {
        Vec2 off{rng_.range(-0.3f, 1.0f) * R * static_cast<float>(World::dir(team_)), rng_.range(-1.0f, 1.0f) * R};
        Vec2 c = clampPitch(anchor + off, 1.0f);
        float sc2 = score(c, false);
        if (sc2 > bestScore) { bestScore = sc2; bestP = c; }
      }
      if (ln == Line::Att || (ln == Line::Mid && prof_->runQuality > 0.7f)) {
        float behindProg = clampf(line + rng_.range(0.03f, 0.08f), 0.5f, 0.95f);
        float myLat = team_ == 0 ? me.pos.y / kPitchW : 1.0f - me.pos.y / kPitchW;
        Vec2 c = slotToWorld({me.role, clampf(myLat + rng_.range(-0.15f, 0.15f), 0.08f, 0.92f), behindProg}, team_);
        c = clampPitch(c, 1.0f);
        float sc2 = score(c, true);
        if (sc2 > bestScore) { bestScore = sc2; bestP = c; }
      }
      st.run = bestP;
      st.runHold = lerpf(0.95f, 0.45f, prof_->runQuality) + rng_.range(0.0f, 0.3f);
    }
    bool behindRun = w.progress(team_, st.run) > line - 0.004f;
    goTo(*prof_, me, st.run, behindRun ? 1.0f : 0.8f, it);
  }
}

void TeamAI::setPiece(const World& w, const Analysis& a, int humanId, const Scouting* sc, std::array<Intent, kNumPlayers>& out) {
  const bool mine = w.restartTeam == team_;
  const Vec2 spot = w.restartSpot;
  const Restart r = w.restart;
  const float s = team_ == 0 ? -1.0f : 1.0f;                // direction pointing into the pitch from the goal we attack
  const float attackGX = team_ == 0 ? kPitchL : 0.0f;       // goal we attack
  const float ownGX = team_ == 0 ? 0.0f : kPitchL;
  Vec2 target[kTeamSize];
  for (int sl = 0; sl < kTeamSize; ++sl) target[sl] = shapeTarget(w, a, sl, mine, sc);

  auto assignNearest = [&](const Vec2* pts, int n, bool used[kTeamSize]) {
    for (int k = 0; k < n; ++k) {
      int bestS = -1; float bestD = 1e9f;
      for (int sl = 1; sl < kTeamSize; ++sl) {
        int id = team_ * kTeamSize + sl;
        if (used[sl] || !w.p[id].live() || id == w.taker) continue;
        float d = dist(w.p[id].pos, pts[k]);
        if (d < bestD) { bestD = d; bestS = sl; }
      }
      if (bestS >= 0) { used[bestS] = true; target[bestS] = pts[k]; }
    }
  };
  bool used[kTeamSize] = {false};
  if (r == Restart::KickOff) {
    for (int sl = 0; sl < kTeamSize; ++sl) {
      float pr = w.progress(team_, target[sl]);
      if (pr > 0.47f) target[sl] = slotToWorld({formation(formation_).slots[sl].role, formation(formation_).slots[sl].x, 0.47f}, team_);
    }
  } else if (r == Restart::Corner) {
    float cs = spot.y < kMidY ? -1.0f : 1.0f;
    if (mine) {
      Vec2 pts[5] = {{attackGX + s * 5.5f, kMidY + cs * 3.0f}, {attackGX + s * 11.0f, kMidY - cs * 1.0f},
                     {attackGX + s * 6.5f, kMidY - cs * 3.0f}, {attackGX + s * 18.0f, kMidY},
                     {attackGX + s * 14.0f, kMidY + cs * 6.0f}};
      // Prefer forwards and midfielders; defenders only fill if needed (assignNearest uses distance, so pre-filter).
      for (int sl = 1; sl < kTeamSize; ++sl) used[sl] = lineOf(formation(formation_).slots[sl].role) == Line::Def && sl > 2;
      assignNearest(pts, 5, used);
    } else {
      Vec2 posts[2] = {{ownGX - s * 0.8f, kMidY - 3.0f}, {ownGX - s * 0.8f, kMidY + 3.0f}};
      assignNearest(posts, 2, used);
      // Man-mark attackers in the box, remaining forwards stay up.
      for (int sl = 1; sl < kTeamSize; ++sl) {
        if (used[sl]) continue;
        int id = team_ * kTeamSize + sl;
        if (!w.p[id].live() || id == humanId) continue;
        if (lineOf(w.p[id].role) == Line::Att) { target[sl] = {kPitchL * 0.5f + (team_ == 0 ? -5.0f : 5.0f), kMidY}; continue; }
        float bd = 1e9f; int bi = -1;
        for (int i = 0; i < kNumPlayers; ++i) {
          const Player& o = w.p[i];
          if (o.team == team_ || !o.live() || o.role == Role::GK || i == w.taker) continue;
          if (std::fabs(o.pos.x - ownGX) > 24.0f) continue;
          bool already = false;
          for (int k = 0; k < kTeamSize; ++k) if (k != sl && per_[k].markId == i) already = true;
          if (already) continue;
          float d = dist(o.pos, w.p[id].pos);
          if (d < bd) { bd = d; bi = i; }
        }
        per_[sl].markId = bi;
        if (bi >= 0) target[sl] = w.p[bi].pos + norm(Vec2{ownGX, kMidY} - w.p[bi].pos) * 1.2f;
        else target[sl] = {ownGX - s * 16.0f, kMidY + (sl % 2 ? 6.0f : -6.0f)};
      }
    }
  } else if (r == Restart::FreeKick || r == Restart::IndirectFreeKick) {
    float dGoalAtt = dist(spot, Vec2{attackGX, kMidY});
    if (mine && dGoalAtt < 32.0f) {
      Vec2 pts[4] = {{attackGX + s * 8.0f, kMidY - 4.0f}, {attackGX + s * 10.0f, kMidY + 4.0f},
                     {attackGX + s * 13.0f, kMidY}, {attackGX + s * 20.0f, kMidY + 8.0f}};
      for (int sl = 1; sl < kTeamSize; ++sl) used[sl] = lineOf(formation(formation_).slots[sl].role) == Line::Def && sl > 2;
      assignNearest(pts, 4, used);
    } else if (!mine) {
      float dGoalOwn = dist(spot, Vec2{ownGX, kMidY});
      int n = dGoalOwn < 18.0f ? 5 : dGoalOwn < 24.0f ? 4 : dGoalOwn < 32.0f ? 3 : 0;
      Vec2 dirToGoal = norm(Vec2{ownGX, kMidY} - spot);
      Vec2 pts[5];
      for (int k = 0; k < n; ++k) pts[k] = spot + dirToGoal * 9.4f + perp(dirToGoal) * ((k - (n - 1) * 0.5f) * 0.85f);
      assignNearest(pts, n, used);
    }
  } else if (r == Restart::Penalty) {
    for (int sl = 1; sl < kTeamSize; ++sl) {
      int id = team_ * kTeamSize + sl;
      if (id == w.taker) continue;
      float k = (sl - 5.5f) * 2.4f;
      target[sl] = mine ? Vec2{attackGX + s * 22.0f, kMidY + k} : Vec2{ownGX - s * 22.0f, kMidY + k};
    }
  }

  for (int sl = 0; sl < kTeamSize; ++sl) {
    int id = team_ * kTeamSize + sl;
    const Player& me = w.p[id];
    if (id == humanId || !me.live()) continue;
    Intent& it = out[id];
    if (id == w.taker && mine) {
      Per& st = per_[sl];
      carrier(w, a, me, st, it, true);
      continue;
    }
    if (sl == 0) {
      // Keeper: penalties and corners use dedicated spots, otherwise normal positioning.
      if (r == Restart::Penalty && !mine) { goTo(*prof_, me, {ownGX - s * 0.4f, kMidY}, 0.6f, it); }
      else if (r == Restart::GoalKick && mine) { goTo(*prof_, me, {ownGX - s * 6.0f, kMidY}, 0.7f, it); }
      else keeper(w, a, per_[0], it);
      continue;
    }
    goTo(*prof_, me, clampPitch(target[sl], 0.8f), 0.9f, it);
  }
}

}  // namespace rx
