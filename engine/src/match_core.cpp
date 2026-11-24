#include <cstring>

#include "ai_util.h"
#include "realxi/ai.h"
#include "realxi/match.h"
#include "realxi/snapshot.h"

namespace rx {
using namespace aiutil;

namespace {
constexpr int kReplayFrames = 360;  // 12 s at 30 Hz
}

Match::Match(const MatchConfig& cfg) : cfg_(cfg), rng_(cfg.seed) {
  w_.humanControlled = cfg.homeDifficulty < 0;
  w_.refereeProfile = cfg.referee;
  w_.offsideOn = cfg.offside;
  w_.foulsOn = cfg.fouls;
  clockRate_ = 5400.0f / (2.0f * std::max(30.0f, cfg.halfSeconds));
  replay_.resize(kReplayFrames);
  initPlayers();
  ai_[0] = std::make_unique<TeamAI>(0, cfg.homeDifficulty >= 0 ? cfg.homeDifficulty : 1, cfg.formationHome, cfg.seed + 11);
  ai_[1] = std::make_unique<TeamAI>(1, cfg.difficulty, cfg.formationAway, cfg.seed + 23);
  if (cfg.mode == 1) {
    drill_.id = cfg.drillId;
    drill_.attemptsMax = cfg.drillAttempts;
    setupDrill();
  } else {
    kickoffReset(0);
  }
}

Match::~Match() = default;

void Match::initPlayers() {
  Rng prng(cfg_.seed ^ 0xA5A5u);
  for (int team = 0; team < 2; ++team) {
    const Formation& f = formation(team == 0 ? cfg_.formationHome : cfg_.formationAway);
    for (int s = 0; s < kTeamSize; ++s) {
      Player& p = w_.p[team * kTeamSize + s];
      p = Player{};
      p.id = team * kTeamSize + s;
      p.team = team;
      p.slot = s;
      p.role = f.slots[s].role;
      Line ln = lineOf(p.role);
      auto v = [&](float base) { return clampf(base + prng.range(-0.04f, 0.04f), 0.35f, 0.95f); };
      p.pace = v(ln == Line::Att ? 0.78f : ln == Line::Mid ? 0.72f : 0.68f);
      p.accel = v(0.72f);
      p.shooting = v(ln == Line::Att ? 0.82f : ln == Line::Mid ? 0.68f : 0.5f);
      p.passing = v(ln == Line::Mid ? 0.80f : 0.68f);
      p.dribbling = v(ln == Line::Att ? 0.78f : 0.66f);
      p.tackling = v(ln == Line::Def ? 0.80f : ln == Line::Mid ? 0.68f : 0.5f);
      p.strength = v(0.7f);
      p.vision = v(0.7f);
      p.keeping = ln == Line::GK ? v(0.82f) : 0.25f;
    }
  }
  placeFormation(0, true);
  placeFormation(1, true);
}

void Match::placeFormation(int team, bool kickoffShape) {
  const Formation& f = formation(team == 0 ? cfg_.formationHome : cfg_.formationAway);
  for (int s = 0; s < kTeamSize; ++s) {
    Player& p = w_.p[team * kTeamSize + s];
    p.role = f.slots[s].role;
    FormationSlot slot = f.slots[s];
    if (kickoffShape && slot.y > 0.47f) slot.y = 0.47f;
    p.pos = slotToWorld(slot, team);
    if (p.sentOff) p.pos = {-50.0f, -50.0f};
    p.vel = {0, 0};
    p.facing = team == 0 ? 0.0f : kPi;
    p.state = PState::Idle;
    p.stateT = 0;
    p.touchCD = 0;
    p.skillT = p.fakeT = p.tackleCD = 0;
    p.slideHit = false;
    if (!p.sentOff) p.active = true;
  }
}

void Match::pushEvent(EventType t, int team, int player, int arg) {
  if (events_.size() < 64) events_.push_back(Event{static_cast<int32_t>(t), team, player, arg});
}

int Match::drainEvents(int32_t* out, int maxEvents) {
  int n = std::min<int>(maxEvents, static_cast<int>(events_.size()));
  for (int i = 0; i < n; ++i) {
    out[i * 4 + 0] = events_[i].type;
    out[i * 4 + 1] = events_[i].team;
    out[i * 4 + 2] = events_[i].player;
    out[i * 4 + 3] = events_[i].arg;
  }
  events_.clear();
  return n;
}

void Match::advance(float realDt) {
  if (paused_) return;
  accum_ += std::min(realDt, 0.1f) * timeScale_;
  int guard = 0;
  while (accum_ >= kDt && guard++ < 12) {
    tick();
    accum_ -= kDt;
  }
  if (accum_ > 0.25f) accum_ = 0;
}

void Match::command(int cmd, float arg) {
  switch (cmd) {
    case kPause: paused_ = arg > 0.5f; break;
    case kTimeScale: timeScale_ = clampf(arg, 0.1f, 2.0f); break;
    case kReplayStart: {
      int avail = std::min(replayCount_, kReplayFrames);
      int want = std::min(avail, static_cast<int>(std::max(1.0f, arg) * 30.0f));
      if (want < 10) break;
      replaying_ = true;
      replayLen_ = want;
      replayStart_ = (replayWrite_ - want + kReplayFrames * 2) % kReplayFrames;
      replayPos_ = 0;
      break;
    }
    case kReplayStop: replaying_ = false; break;
    case kReplaySpeed: replaySpeed_ = clampf(arg, 0.1f, 2.0f); break;
    case kSelectSkill: selectedSkill_ = std::clamp(static_cast<int>(arg), 0, 4); break;
    case kSetAssist: cfg_.assist = sat(arg); break;
    case kSetAdaptive: cfg_.adaptiveAI = arg > 0.5f; break;
    case kSetDifficulty: cfg_.difficulty = std::clamp(static_cast<int>(arg), 0, kNumTiers - 1); ai_[1]->setTier(cfg_.difficulty); break;
    case kSetFormationHome: {
      cfg_.formationHome = std::clamp(static_cast<int>(arg), 0, kNumFormations - 1);
      ai_[0]->setFormation(cfg_.formationHome);
      const Formation& f = formation(cfg_.formationHome);
      for (int s = 0; s < kTeamSize; ++s) {
        w_.p[s].role = f.slots[s].role;
      }
      break;
    }
    case kDrillNext:
    case kDrillRestart:
      if (cfg_.mode == 1) {
        if (cmd == kDrillRestart || drill_.complete) {
          int id = drill_.id, mx = drill_.attemptsMax;
          drill_ = DrillState{};
          drill_.id = id;
          drill_.attemptsMax = mx;
        }
        setupDrill();
      }
      break;
    default: break;
  }
}

// ---------------------------------------------------------------- replay
void Match::recordReplay() {
  if (tickCount_ & 1) return;
  Frame& f = replay_[replayWrite_];
  f.v[0] = w_.ball.pos.x; f.v[1] = w_.ball.pos.y; f.v[2] = w_.ball.pos.z;
  for (int i = 0; i < kNumPlayers; ++i) {
    const Player& p = w_.p[i];
    f.v[3 + i * 4 + 0] = p.pos.x;
    f.v[3 + i * 4 + 1] = p.pos.y;
    f.v[3 + i * 4 + 2] = p.facing;
    f.v[3 + i * 4 + 3] = static_cast<float>(p.state);
  }
  replayWrite_ = (replayWrite_ + 1) % kReplayFrames;
  ++replayCount_;
}

void Match::advanceReplay() {
  replayPos_ += 0.5f * replaySpeed_;
  if (replayPos_ >= replayLen_ - 1) replaying_ = false;
}

// ---------------------------------------------------------------- tick
void Match::tick() {
  ++tickCount_;
  if (replaying_) { advanceReplay(); return; }
  switch (w_.phase) {
    case Phase::FullTime: prevIn_ = in_; return;
    case Phase::HalfTime:
      halfTimeTimer_ -= kDt;
      if (halfTimeTimer_ <= 0) { w_.half = 2; kickoffReset(1 - w_.kickoffTeam); }
      return;
    case Phase::DrillResult:
      updateDrill();
      return;
    default: break;
  }
  tickLive();
  if (cfg_.mode == 1) updateDrill();
  recordReplay();
  prevIn_ = in_;
}

void Match::tickLive() {
  const float dt = kDt;
  w_.passAge += dt;
  chooseActive();

  // AI runs at 30 Hz; intents persist between runs.
  if ((tickCount_ & 1) == 0) {
    int humanId = w_.humanControlled ? w_.activeHuman : -1;
    // Full matches never stall: a human keeper or restart taker who waits too long hands over to the AI.
    if (cfg_.mode == 0 && humanId >= 0) {
      if (w_.p[humanId].role == Role::GK && w_.owner == humanId && w_.since > 4.0f) humanId = -1;
      if (w_.phase != Phase::Live && w_.restartTeam == 0 && w_.taker == humanId && w_.restartTimer > 7.0f) humanId = -1;
    }
    ai_[0]->think(w_, 2 * dt, humanId, &scout_, intents_);
    ai_[1]->think(w_, 2 * dt, -1, &scout_, intents_);
  }
  if (cfg_.mode == 1 && drill_.feederId >= 0 && drill_.feedTimer > 0) intents_[drill_.feederId] = Intent{};
  bool humanActs = w_.humanControlled && w_.activeHuman >= 0 && w_.phase != Phase::GoalPause;
  if (humanActs && cfg_.mode == 0 && w_.phase != Phase::Live && w_.restartTeam == 0 && w_.taker == w_.activeHuman &&
      w_.restartTimer > 7.0f)
    humanActs = false;
  if (humanActs && cfg_.mode == 0 && w_.p[w_.activeHuman].role == Role::GK && w_.owner == w_.activeHuman && w_.since > 4.0f)
    humanActs = false;
  if (humanActs) intents_[w_.activeHuman] = humanIntent(w_.p[w_.activeHuman]);
  for (int i = 0; i < kNumPlayers; ++i) {
    Player& p = w_.p[i];
    if (!p.live()) continue;
    applyIntent(p, intents_[i]);
    if (p.role == Role::GK && w_.phase == Phase::Live) {
      // Keepers stay inside their own penalty area in open play.
      const float gl = p.team == 0 ? 0.0f : kPitchL;
      const float inward = p.team == 0 ? 1.0f : -1.0f;
      float depth = (p.pos.x - gl) * inward;
      if (depth > kBoxDepth - 0.8f) { p.pos.x = gl + inward * (kBoxDepth - 0.8f); p.vel.x = std::min(p.vel.x * inward, 0.0f) * inward; }
      if (depth < 0.3f) p.pos.x = gl + inward * 0.3f;
      p.pos.y = clampf(p.pos.y, kMidY - kBoxHalfW + 0.8f, kMidY + kBoxHalfW - 0.8f);
    }
  }
  separatePlayers();

  if (w_.phase == Phase::GoalPause) {
    goalTimer_ -= dt;
    stepBall(w_.ball, dt);
    collideFrame();
    if (goalTimer_ <= 0 && cfg_.mode == 0) kickoffReset(1 - lastScorer_);
    return;
  }
  if (w_.phase == Phase::Kickoff || w_.phase == Phase::DeadBall) {
    updateRestart();
    return;
  }

  updatePossession();
  keeperPhysics();
  if (w_.owner < 0) {
    stepBall(w_.ball, dt);
    collideFrame();
    checkBoundaries();
  }
  if (w_.phase != Phase::Live) return;
  updateClock();
  updatePreview();
}

void Match::updateClock() {
  if (cfg_.mode == 1) return;
  const float dt = kDt;
  w_.gameTime += dt * clockRate_;
  float limit = w_.half == 1 ? 2700.0f : 5400.0f;
  if (!endPending_ && w_.gameTime >= limit) { endPending_ = true; endGrace_ = 0; }
  if (endPending_) {
    endGrace_ += dt;
    bool danger = w_.owner >= 0 ? w_.progress(w_.p[w_.owner].team, w_.p[w_.owner].pos) > 0.8f
                                : (std::fabs(w_.ball.pos.x - kPitchL * 0.5f) > 36.0f && w_.ball.vel.xy().len() > 6.0f);
    if (!danger || endGrace_ > 9.0f) endHalfOrMatch();
  }
  if (w_.owner >= 0) w_.stats[w_.p[w_.owner].team].possession += dt;
}

// ---------------------------------------------------------------- movement
void Match::movePlayer(Player& p, Vec2 dir, float speedFrac, bool sprint) {
  float vmax = p.maxSpeed();
  if (w_.owner == p.id) vmax *= 0.93f;
  float cap = (sprint && p.stamina > 0.02f) ? 1.0f : 0.78f;
  Vec2 desired = dir.len2() > 1e-6f ? norm(dir) * (vmax * cap * sat(speedFrac)) : Vec2{0, 0};
  float acc = lerpf(11.0f, 16.0f, p.accel);
  if (desired.len2() < p.vel.len2()) acc *= 1.5f;
  Vec2 dv = desired - p.vel;
  float L = dv.len();
  float maxd = acc * kDt;
  if (L > maxd) dv *= maxd / L;
  p.vel += dv;
  p.pos += p.vel * kDt;
  float spd = p.vel.len();
  if (!cfg_.infiniteStamina) {
    if (sprint && spd > 0.8f * p.maxSpeed()) p.stamina -= 0.07f * kDt;
    else if (spd < 0.65f * p.maxSpeed()) p.stamina += 0.04f * kDt;
    p.stamina = sat(p.stamina);
  } else {
    p.stamina = 1.0f;
  }
  Vec2 face = spd > 0.6f ? p.vel : dir;
  if (face.len2() > 1e-6f) {
    float diff = wrapAngle(angleOf(face) - p.facing);
    float maxTurn = (spd > 6.0f ? 9.0f : 14.0f) * kDt;
    p.facing = wrapAngle(p.facing + clampf(diff, -maxTurn, maxTurn));
  }
  if (p.state == PState::Idle || p.state == PState::Run || p.state == PState::Sprint ||
      (p.state == PState::Kick && p.stateT <= 0)) {
    p.state = spd > 5.5f ? PState::Sprint : (spd > 0.6f ? PState::Run : PState::Idle);
  }
}

void Match::applyIntent(Player& p, const Intent& it) {
  p.touchCD = std::max(0.0f, p.touchCD - kDt);
  p.skillT = std::max(0.0f, p.skillT - kDt);
  p.fakeT = std::max(0.0f, p.fakeT - kDt);
  p.tackleCD = std::max(0.0f, p.tackleCD - kDt);
  if (p.stateT > 0) p.stateT -= kDt;
  p.target = it.dest;

  switch (p.state) {
    case PState::Slide: {
      p.vel *= std::exp(-3.2f * kDt);
      p.pos += p.vel * kDt;
      if (!p.slideHit && w_.phase == Phase::Live) {
        if (w_.owner >= 0 && w_.p[w_.owner].team != p.team && dist(w_.p[w_.owner].pos, p.pos) < 1.05f) {
          p.slideHit = true;
          resolveTackleContact(p, w_.p[w_.owner], true);
        } else if (w_.owner < 0 && dist(w_.ball.pos.xy(), p.pos) < 1.0f && w_.ball.pos.z < 0.5f) {
          p.slideHit = true;
          Vec2 f{std::cos(p.facing), std::sin(p.facing)};
          w_.ball.vel = {f.x * 7.0f, f.y * 7.0f, 0.5f};
          w_.lastTouchTeam = p.team; w_.lastTouchId = p.id; w_.lastTouchKick = false;
          if (w_.noHandleTeam != p.team) w_.noHandleTeam = -1;
          w_.passTeam = -1;
        }
      }
      if (p.stateT <= 0) { p.state = PState::Recover; p.stateT = 0.6f; p.slideHit = false; }
      return;
    }
    case PState::Dive:
      p.vel *= std::exp(-2.0f * kDt);
      p.pos += p.vel * kDt;
      if (p.stateT <= 0) { p.state = PState::Recover; p.stateT = 0.5f; }
      return;
    case PState::Recover:
      p.vel *= std::exp(-6.0f * kDt);
      p.pos += p.vel * kDt;
      if (p.stateT <= 0) p.state = PState::Idle;
      return;
    case PState::Tackle:
      movePlayer(p, it.move, it.speed * 0.4f, false);
      p.state = PState::Tackle;
      if (p.stateT <= 0) p.state = PState::Idle;
      return;
    case PState::Celebrate:
      p.vel *= 0.92f;
      p.pos += p.vel * kDt;
      return;
    default: break;
  }
  movePlayer(p, it.move, it.speed, it.sprint);

  const bool ableToAct = canKick(p);
  switch (it.act) {
    case Act::Pass: case Act::Through: case Act::Lob: case Act::Cross:
      if (ableToAct) doPass(p, it.act, it.point, it.power, it.targetId);
      break;
    case Act::Shoot:
      if (ableToAct) doShot(p, it.point, it.power, it.curl, p.id == w_.activeHuman && w_.humanControlled);
      break;
    case Act::Clear:
      if (ableToAct) doClear(p, it.point, it.power);
      break;
    case Act::Tackle: tackleAttempt(p, false); break;
    case Act::Slide: tackleAttempt(p, true); break;
    case Act::Skill: doSkill(p, it.skill); break;
    case Act::Dive:
      if (p.role == Role::GK && p.state != PState::Dive && w_.phase == Phase::Live) {
        Vec2 d = it.point - p.pos;
        p.diveDir = norm(d, {0, p.pos.y < kMidY ? 1.0f : -1.0f});
        float sp = lerpf(5.5f, 8.2f, p.keeping);
        p.vel = p.diveDir * sp;
        p.state = PState::Dive;
        p.stateT = 0.6f;
      }
      break;
    default: break;
  }
}

void Match::separatePlayers() {
  for (int i = 0; i < kNumPlayers; ++i) {
    Player& a = w_.p[i];
    if (!a.live()) continue;
    for (int j = i + 1; j < kNumPlayers; ++j) {
      Player& b = w_.p[j];
      if (!b.live()) continue;
      Vec2 d = b.pos - a.pos;
      float L2 = d.len2();
      if (L2 >= 0.49f || L2 < 1e-8f) continue;
      float L = std::sqrt(L2);
      Vec2 n = d * (1.0f / L);
      float push = (0.7f - L) * 0.5f;
      float wa = b.strength / (a.strength + b.strength), wb = 1.0f - wa;
      a.pos -= n * (push * 2.0f * wa);
      b.pos += n * (push * 2.0f * wb);
    }
  }
  for (Player& p : w_.p) {
    if (!p.live()) continue;
    p.pos.x = clampf(p.pos.x, -3.0f, kPitchL + 3.0f);
    p.pos.y = clampf(p.pos.y, -3.0f, kPitchW + 3.0f);
  }
}

// ---------------------------------------------------------------- possession
bool Match::canKick(const Player& p) const {
  if (w_.phase == Phase::Live) {
    if (w_.owner == p.id) return true;
    return w_.owner < 0 && p.touchCD <= 0 && dist(p.pos, w_.ball.pos.xy()) < 1.5f && w_.ball.pos.z < 1.9f;
  }
  return (w_.phase == Phase::DeadBall || w_.phase == Phase::Kickoff) && w_.taker == p.id && w_.restartTimer > 0.3f;
}

void Match::releaseBall() {
  w_.owner = -1;
}

void Match::gainPossession(Player& p) {
  if (checkOffsideOnTouch(p)) return;
  if (w_.passTeam >= 0) {
    if (p.team == w_.passTeam && p.id != w_.lastTouchId) ++w_.stats[p.team].passesCompleted;
    w_.passTeam = -1;
    w_.passTarget = -1;
  }
  if (w_.noHandleTeam >= 0 && w_.noHandleTeam != p.team) w_.noHandleTeam = -1;
  w_.owner = p.id;
  w_.lastTouchTeam = p.team;
  w_.lastTouchId = p.id;
  w_.lastTouchKick = false;
  w_.penaltyLive = false;
  w_.offsideMask = 0;
  w_.ball.vel = {p.vel.x, p.vel.y, 0};
  w_.ball.spin = {};
  w_.ball.pos.z = kBallR;
  if (p.role != Role::GK) p.state = PState::Idle;
}

void Match::headerOrChest(Player& p) {
  Ball& b = w_.ball;
  float speed = b.vel.xy().len();
  w_.lastTouchTeam = p.team;
  w_.lastTouchId = p.id;
  w_.lastTouchKick = false;
  w_.passTeam = -1;
  if (w_.noHandleTeam >= 0 && w_.noHandleTeam != p.team) w_.noHandleTeam = -1;
  p.touchCD = 0.35f;
  if (speed < 7.0f) {
    gainPossession(p);
    return;
  }
  Vec2 dir;
  if (p.id == w_.activeHuman && w_.humanControlled && (std::fabs(in_.moveX) + std::fabs(in_.moveY)) > 0.2f) {
    dir = norm({in_.moveX, in_.moveY});
  } else {
    Vec2 goal = w_.goalCentre(p.team);
    if (w_.progress(p.team, p.pos) > 0.78f) dir = norm(goal - p.pos);
    else dir = {static_cast<float>(World::dir(p.team)), (p.pos.y < kMidY ? 0.6f : -0.6f)};
    dir = rotate(norm(dir), rng_.gauss() * 0.12f);
  }
  float sp = 8.0f + 0.4f * speed;
  b.vel = {dir.x * sp, dir.y * sp, 3.5f};
  b.spin = {};
  w_.owner = -1;
  p.state = PState::Header;
  p.stateT = 0.3f;
  pushEvent(EventType::Header, p.team, p.id);
}

void Match::tryPickup() {
  Ball& b = w_.ball;
  Vec2 bxy = b.pos.xy();
  const Vec2 prev = prevBallXY_;
  prevBallXY_ = bxy;
  if (b.pos.z > 2.2f) return;
  // Swept test: a fast ball is checked along the whole segment it travelled this tick, so it can never tunnel through a player.
  const bool sweep = dist(prev, bxy) > 0.05f && dist(prev, bxy) < 2.0f;
  // Bodies are solid: a player who is down, sliding or recovering still blocks a ball that hits him.
  if (sweep && b.pos.z < 1.1f && b.vel.xy().len() > 3.0f) {
    for (int i = 0; i < kNumPlayers; ++i) {
      Player& q = w_.p[i];
      if (!q.live() || q.role == Role::GK) continue;
      if (!(q.state == PState::Slide || q.state == PState::Recover || q.state == PState::Tackle)) continue;
      if (distToSegment(q.pos, prev, bxy) > 0.55f) continue;
      Vec2 n = norm(bxy - q.pos);
      Vec2 v = b.vel.xy();
      float vn = dot(v, n);
      if (vn >= 0) continue;
      Vec2 r = v - n * (2.0f * vn);
      b.vel = {r.x * 0.45f, r.y * 0.45f, 0.6f};
      b.pos = {q.pos.x + n.x * 0.6f, q.pos.y + n.y * 0.6f, b.pos.z};
      w_.lastTouchTeam = q.team; w_.lastTouchId = q.id; w_.lastTouchKick = false;
      if (q.team != w_.passTeam) w_.passTeam = -1;
      return;
    }
  }
  int cand = -1;
  float bestD = 1e9f;
  for (int i = 0; i < kNumPlayers; ++i) {
    Player& p = w_.p[i];
    if (!p.live() || p.touchCD > 0 || p.state == PState::Slide || p.state == PState::Recover || p.state == PState::Dive) continue;
    float d = sweep ? distToSegment(p.pos, prev, bxy) : dist(p.pos, bxy);
    float reach = 0.85f + 0.2f * p.dribbling;
    if (b.pos.z > 1.15f) reach = 0.9f;
    if (d < reach && d < bestD) { bestD = d; cand = i; }
  }
  if (cand < 0) return;
  Player& p = w_.p[cand];
  if (checkOffsideOnTouch(p)) return;
  if (b.pos.z > 1.15f) {
    if (p.role != Role::GK) headerOrChest(p);
    return;
  }
  Vec2 rel = b.vel.xy() - p.vel;
  float relSpeed = rel.len();
  float ctrl = 11.0f + 14.0f * p.dribbling;
  // Slow balls are simply controlled; fast ones are harder to take cleanly (and more so for a player not set for it).
  float pc = relSpeed <= 11.0f ? 1.0f : sat(1.0f - (relSpeed - 11.0f) / (ctrl + 12.0f));
  if (w_.passTarget == p.id && w_.passTeam == p.team) pc = std::min(1.0f, pc + 0.35f);
  if (rng_.chance(pc)) {
    gainPossession(p);
  } else {
    // Heavy first touch: the ball skids away.
    Vec2 away = norm(b.vel.xy() * 0.35f + Vec2{rng_.range(-2.0f, 2.0f), rng_.range(-2.0f, 2.0f)});
    float sp = std::min(relSpeed * 0.35f, 9.0f);
    b.vel = {away.x * sp, away.y * sp, 0};
    p.touchCD = 0.3f;
    w_.lastTouchTeam = p.team;
    w_.lastTouchId = p.id;
    w_.lastTouchKick = false;
    w_.passTeam = -1;
  }
}

void Match::updatePossession() {
  if (w_.owner >= 0) {
    Player& o = w_.p[w_.owner];
    if (!o.live() || o.state == PState::Slide || o.state == PState::Recover || o.state == PState::Dive) {
      if (o.role == Role::GK && o.state == PState::Dive) { /* keeper keeps the ball he caught */ }
      else { releaseBall(); return; }
    }
    if (o.role == Role::GK) {
      // Keeper holds the ball in his hands at chest height.
      w_.ball.pos = {o.pos.x + std::cos(o.facing) * 0.3f, o.pos.y + std::sin(o.facing) * 0.3f, 1.0f};
      w_.ball.vel = {0, 0, 0};
      return;
    }
    Vec2 f{std::cos(o.facing), std::sin(o.facing)};
    float spd = o.vel.len();
    float off = 0.55f + 0.20f * sat(spd / o.maxSpeed());
    Vec2 target = o.pos + f * off;
    Vec2 cur = w_.ball.pos.xy();
    Vec2 np = cur + (target - cur) * 0.78f;
    w_.ball.pos = {np.x, np.y, kBallR};
    w_.ball.vel = {o.vel.x, o.vel.y, 0};
    w_.ball.spin = {};
    if (o.team == 0 && w_.humanControlled) scout_.observeCarrier(o.pos.y, kDt);
    return;
  }
  tryPickup();
}

// ---------------------------------------------------------------- human control
void Match::chooseActive() {
  if (!w_.humanControlled) { w_.activeHuman = -1; return; }
  switchCD_ = std::max(0.0f, switchCD_ - kDt);
  int cur = w_.activeHuman;
  auto valid = [&](int id) { return id >= 0 && w_.p[id].live() && w_.p[id].team == 0; };
  if ((w_.phase == Phase::DeadBall || w_.phase == Phase::Kickoff) && w_.restartTeam == 0 && w_.taker >= 0 && valid(w_.taker)) {
    w_.activeHuman = w_.taker;
    return;
  }
  if (cfg_.mode == 1 && drill_.humanId >= 0 && w_.owner == drill_.feederId && drill_.feederId >= 0) { w_.activeHuman = drill_.humanId; return; }
  if (w_.owner >= 0 && w_.p[w_.owner].team == 0) { w_.activeHuman = w_.owner; return; }
  if (w_.owner < 0 && w_.passTeam == 0 && w_.passTarget >= 0 && w_.passAge < 3.0f && valid(w_.passTarget) &&
      w_.p[w_.passTarget].role != Role::GK) {
    w_.activeHuman = w_.passTarget;
    return;
  }
  // Defending or loose ball: the player closest (in time) to the ball.
  Vec2 target = w_.owner >= 0 ? w_.p[w_.owner].pos : w_.ball.pos.xy();
  int best = -1;
  float bestT = 1e9f, curT = 1e9f;
  for (int i = 0; i < kTeamSize; ++i) {
    const Player& p = w_.p[i];
    if (!p.live() || (p.role == Role::GK && dist(p.pos, target) > 14.0f)) continue;
    if (cfg_.mode == 1 && i == drill_.feederId) continue;
    float t = timeTo(p, target);
    if (i == cur) curT = t;
    if (in_.sw && !prevIn_.sw && i == cur) continue;  // manual switch skips the current player
    if (t < bestT) { bestT = t; best = i; }
  }
  bool manual = in_.sw && !prevIn_.sw;
  if (!valid(cur) || manual || (switchCD_ <= 0 && best >= 0 && best != cur && bestT < curT - 0.35f)) {
    if (best >= 0) { w_.activeHuman = best; switchCD_ = 0.4f; }
  }
}

int Match::bestPassTarget(const Player& p, Vec2 aim, bool through, Vec2* point) const {
  int best = -1;
  float bestScore = -1e9f;
  Vec2 goal = w_.goalCentre(p.team);
  float cone = lerpf(48.0f, 66.0f, cfg_.assist) * kPi / 180.0f;
  for (int s = 0; s < kTeamSize; ++s) {
    const Player& t = w_.p[p.team * kTeamSize + s];
    if (t.id == p.id || !t.live()) continue;
    Vec2 d = t.pos - p.pos;
    float L = d.len();
    if (L < 2.5f) continue;
    float ang = std::acos(clampf(dot(norm(d), aim), -1.0f, 1.0f));
    if (ang > cone) continue;
    Vec2 P = through ? clampPitch(t.pos + t.vel * 0.6f + norm(goal - t.pos) * 6.0f)
                     : clampPitch(t.pos + t.vel * clampf(L / 20.0f, 0.15f, 0.8f));
    float v0 = groundSpeedFor(std::max(3.0f, dist(p.pos, P)), kPassArrive);
    float risk = interceptRisk(w_, p.team, p.pos, P, v0, false, t.id);
    float score = (1.0f - ang / cone) * 1.0f + 0.5f * (1.0f - risk) - 0.006f * L + (t.role == Role::GK ? -0.4f : 0.0f);
    if (score > bestScore) { bestScore = score; best = t.id; *point = P; }
  }
  if (best < 0 && cone < 2.6f) {
    // Nobody in the aimed cone: pass assist picks the team-mate closest to the aimed direction instead of kicking blind.
    float bestAng = 1e9f;
    for (int s = 0; s < kTeamSize; ++s) {
      const Player& t = w_.p[p.team * kTeamSize + s];
      if (t.id == p.id || !t.live() || t.role == Role::GK) continue;
      Vec2 d = t.pos - p.pos;
      float L = d.len();
      if (L < 2.5f || L > 40.0f) continue;
      float ang = std::acos(clampf(dot(norm(d), aim), -1.0f, 1.0f)) + 0.004f * L;
      if (ang < bestAng) {
        bestAng = ang; best = t.id;
        *point = through ? clampPitch(t.pos + t.vel * 0.6f + norm(goal - t.pos) * 6.0f) : clampPitch(t.pos + t.vel * clampf(L / 20.0f, 0.15f, 0.8f));
      }
    }
  }
  if (best < 0) *point = clampPitch(p.pos + aim * 14.0f);
  return best;
}

Intent Match::humanIntent(Player& p) {
  Intent it;
  Vec2 stick{in_.moveX, in_.moveY};
  float mag = stick.len();
  if (mag < 0.12f) mag = 0;
  it.move = mag > 0 ? norm(stick) : Vec2{0, 0};
  it.speed = sat(mag);
  it.sprint = in_.sprint;
  it.dest = p.pos + it.move * 5.0f;
  Vec2 face{std::cos(p.facing), std::sin(p.facing)};
  Vec2 aim = mag > 0 ? norm(stick) : face;
  const bool able = canKick(p);

  bool passRel = !in_.pass && passHeld_;
  bool shootRel = !in_.shoot && shootHeld_;
  bool lobRel = !in_.lob && lobHeld_;
  if (in_.pass) chargePass_ += kDt;
  if (in_.shoot) chargeShoot_ += kDt;
  if (in_.lob) chargeLob_ += kDt;
  passHeld_ = in_.pass; shootHeld_ = in_.shoot; lobHeld_ = in_.lob;

  if (shootRel && able) {
    it.act = Act::Shoot;
    it.point = p.pos + aim * 40.0f;
    it.power = 0.35f + 0.65f * sat(chargeShoot_ / 0.9f);
    Vec2 g = w_.goalCentre(p.team) - p.pos;
    if (in_.finesse) {
      it.curl = cross(aim, g) >= 0 ? 1.0f : -1.0f;
    } else if (cfg_.assist > 0.3f && g.len() < 30.0f && std::fabs(p.pos.y - kMidY) > 8.0f) {
      it.curl = cross(aim, g) >= 0 ? 0.6f : -0.6f;      // assist: wide-angle shots bend back toward goal
    }
  } else if (passRel && able) {
    Vec2 pt;
    int tid = bestPassTarget(p, aim, false, &pt);
    it.act = Act::Pass; it.point = pt; it.targetId = tid;
    it.power = 0.4f + 0.6f * sat(chargePass_ / 0.7f);
    if (oneTwoArm_ > 0 && tid >= 0) { ai_[p.team]->requestOneTwo(p.id, 2.6f); oneTwoArm_ = 0; }
  } else if (lobRel && able) {
    Vec2 pt;
    bool lofted = in_.finesse;
    int tid = bestPassTarget(p, aim, !lofted, &pt);
    // Clear: a lofted ball from our own third with nobody on goes long and wide of the danger zone.
    if (lofted && tid < 0 && w_.progress(p.team, p.pos) < 0.3f) {
      Vec2 up = norm(w_.goalCentre(p.team) - p.pos);
      Vec2 side{0, p.pos.y < kMidY ? -1.0f : 1.0f};
      pt = clampPitch(p.pos + norm(up * 0.8f + side * 0.45f) * 42.0f);
    }
    it.act = lofted ? Act::Lob : Act::Through; it.point = pt; it.targetId = tid;
    it.power = 0.4f + 0.6f * sat(chargeLob_ / 0.7f);
  } else if (!able || (w_.owner != p.id)) {
    // Pressure (hold): close the carrier down goal-side, slowing to a jockey when close so we do not dive in.
    if (in_.finesse && w_.owner >= 0 && w_.p[w_.owner].team != p.team) {
      const Player& cc = w_.p[w_.owner];
      Vec2 gs = norm(w_.goalCentre(1 - p.team) - cc.pos);   // our own goal direction (the goal this carrier attacks)
      Vec2 tgt = cc.pos + cc.vel * 0.25f + gs * 1.7f;
      float dd = dist(p.pos, tgt);
      Vec2 toT = dd > 0.2f ? norm(tgt - p.pos) : Vec2{0, 0};
      Vec2 mv = mag > 0 ? norm(it.move * 0.45f + toT * 0.55f) : toT;
      it.move = mv;
      it.speed = mag > 0 ? 1.0f : sat(dd / 3.0f + 0.25f);
      it.dest = p.pos + mv * 5.0f;
    }
    float prev = tackleHold_;
    if (in_.tackle) tackleHold_ += kDt;
    if (in_.tackle && prev <= 0.22f && tackleHold_ > 0.22f) it.act = Act::Slide;
    else if (!in_.tackle && tackleHeld_ && prev <= 0.22f && prev > 0.0f) it.act = Act::Tackle;
    if (!in_.tackle) tackleHold_ = 0;
  } else if (!in_.tackle) {
    tackleHold_ = 0;
  }
  tackleHeld_ = in_.tackle;
  if (in_.skill && !prevIn_.skill && it.act == Act::None) { it.act = Act::Skill; it.skill = selectedSkill_; }
  if (oneTwoArm_ > 0) oneTwoArm_ -= kDt;
  if (in_.call && !prevIn_.call && p.team == 0) {
    if (w_.owner >= 0 && w_.p[w_.owner].team == 0) { ai_[0]->requestRun(3.0f); oneTwoArm_ = 2.0f; } else ai_[0]->requestPress(3.5f);   // CALL: runs on attack, team press on defence
  }

  if (!in_.pass && !passHeld_) chargePass_ = 0;
  if (!in_.shoot && !shootHeld_) chargeShoot_ = 0;
  if (!in_.lob && !lobHeld_) chargeLob_ = 0;
  return it;
}

// ---------------------------------------------------------------- snapshot
void Match::writeSnapshot(float* out) const {
  std::memset(out, 0, sizeof(float) * snap::kFloats);
  const World& w = w_;
  out[snap::kPhase] = static_cast<float>(w.phase);
  out[snap::kRestart] = static_cast<float>(w.restart);
  out[snap::kRestartTeam] = static_cast<float>(w.restartTeam);
  out[snap::kClock] = w.gameTime;
  out[snap::kHalf] = static_cast<float>(w.half);
  out[snap::kScoreHome] = static_cast<float>(w.score[0]);
  out[snap::kScoreAway] = static_cast<float>(w.score[1]);
  out[snap::kActive] = static_cast<float>(w.activeHuman);
  out[snap::kBallX] = w.ball.pos.x; out[snap::kBallY] = w.ball.pos.y; out[snap::kBallZ] = w.ball.pos.z;
  out[snap::kBallVX] = w.ball.vel.x; out[snap::kBallVY] = w.ball.vel.y; out[snap::kBallVZ] = w.ball.vel.z;
  out[snap::kOwner] = static_cast<float>(w.owner);
  out[snap::kReplaying] = replaying_ ? 1.0f : 0.0f;
  out[snap::kReplayProgress] = replaying_ && replayLen_ > 1 ? replayPos_ / (replayLen_ - 1) : 0.0f;
  out[snap::kTimeScale] = timeScale_;
  out[snap::kChargePass] = sat(std::max(chargePass_, chargeLob_) / 0.7f);
  out[snap::kChargeShoot] = sat(chargeShoot_ / 0.9f);
  out[snap::kActiveStamina] = w.activeHuman >= 0 ? w.p[w.activeHuman].stamina : 0.0f;
  out[snap::kDrillId] = static_cast<float>(cfg_.mode == 1 ? drill_.id + 1 : 0);
  out[snap::kDrillAttempt] = static_cast<float>(drill_.attempt);
  out[snap::kDrillSuccess] = static_cast<float>(drill_.success);
  out[snap::kDrillFail] = static_cast<float>(drill_.fail);
  out[snap::kDrillLast] = static_cast<float>(drill_.lastResult);
  out[snap::kDrillTimeLeft] = drill_.timeLeft;
  out[snap::kDrillStreak] = static_cast<float>(drill_.streak);
  out[snap::kDrillBest] = static_cast<float>(drill_.bestStreak);
  out[snap::kPaused] = paused_ ? 1.0f : 0.0f;
  out[snap::kTaker] = static_cast<float>(w.taker);
  out[snap::kDrillAttemptsMax] = static_cast<float>(drill_.attemptsMax);
  out[snap::kDrillComplete] = drill_.complete ? 1.0f : 0.0f;
  out[snap::kPreview] = previewActive_ ? 1.0f : 0.0f;
  out[snap::kEndPending] = endPending_ ? 1.0f : 0.0f;
  for (int i = 0; i < kNumPlayers; ++i) {
    const Player& p = w.p[i];
    float* o = out + snap::kPlayers + i * snap::kPlayerStride;
    o[0] = p.pos.x; o[1] = p.pos.y; o[2] = p.vel.x; o[3] = p.vel.y; o[4] = p.facing;
    o[5] = static_cast<float>(p.state); o[6] = p.stamina;
    int flags = (p.active ? 1 : 0) | (p.yellows > 0 ? 2 : 0) | (p.sentOff ? 4 : 0) | (w.owner == i ? 8 : 0);
    o[7] = static_cast<float>(flags);
    o[8] = static_cast<float>(p.role);
    o[9] = p.stateT;
    float* tgt = out + snap::kTargets + i * 2;
    tgt[0] = p.target.x; tgt[1] = p.target.y;
  }
  for (int t = 0; t < 2; ++t) {
    const TeamStats& s = w.stats[t];
    float* o = out + snap::kStats + t * snap::kStatsStride;
    o[0] = s.possession; o[1] = static_cast<float>(s.shots); o[2] = static_cast<float>(s.shotsOnTarget);
    o[3] = static_cast<float>(s.passes); o[4] = static_cast<float>(s.passesCompleted);
    o[5] = static_cast<float>(s.tackles); o[6] = static_cast<float>(s.tacklesWon);
    o[7] = static_cast<float>(s.fouls); o[8] = static_cast<float>(s.yellows); o[9] = static_cast<float>(s.reds);
    o[10] = static_cast<float>(s.corners); o[11] = static_cast<float>(s.offsides);
    o[12] = static_cast<float>(s.saves); o[13] = s.xg;
  }
  for (int i = 0; i < 12; ++i) {
    out[snap::kPredict + i * 3 + 0] = predicted_[i].x;
    out[snap::kPredict + i * 3 + 1] = predicted_[i].y;
    out[snap::kPredict + i * 3 + 2] = predicted_[i].z;
  }
  if (replaying_) {
    int idx = (replayStart_ + static_cast<int>(replayPos_)) % kReplayFrames;
    const Frame& f = replay_[idx];
    out[snap::kBallX] = f.v[0]; out[snap::kBallY] = f.v[1]; out[snap::kBallZ] = f.v[2];
    for (int i = 0; i < kNumPlayers; ++i) {
      float* o = out + snap::kPlayers + i * snap::kPlayerStride;
      o[0] = f.v[3 + i * 4]; o[1] = f.v[3 + i * 4 + 1]; o[4] = f.v[3 + i * 4 + 2]; o[5] = f.v[3 + i * 4 + 3];
      o[2] = o[3] = 0;
    }
  }
  if (w.half == 2) {  // teams change ends: rotate the view 180 degrees
    auto fx = [](float& x) { x = kPitchL - x; };
    auto fy = [](float& y) { y = kPitchW - y; };
    fx(out[snap::kBallX]); fy(out[snap::kBallY]);
    out[snap::kBallVX] = -out[snap::kBallVX]; out[snap::kBallVY] = -out[snap::kBallVY];
    for (int i = 0; i < kNumPlayers; ++i) {
      float* o = out + snap::kPlayers + i * snap::kPlayerStride;
      fx(o[0]); fy(o[1]); o[2] = -o[2]; o[3] = -o[3]; o[4] = wrapAngle(o[4] + kPi);
      float* tgt = out + snap::kTargets + i * 2;
      fx(tgt[0]); fy(tgt[1]);
    }
    for (int i = 0; i < 12; ++i) {
      if (out[snap::kPredict + i * 3 + 0] == 0 && out[snap::kPredict + i * 3 + 1] == 0) continue;
      fx(out[snap::kPredict + i * 3 + 0]); fy(out[snap::kPredict + i * 3 + 1]);
    }
  }
}

}  // namespace rx
