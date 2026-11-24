// World state, AI intents and the Match simulation (fixed 60 Hz step, fully deterministic for a given seed).
#pragma once
#include <memory>

#include "core.h"
#include "types.h"

namespace rx {

struct Player {
  int id = 0, team = 0, slot = 0;
  Role role = Role::CM;
  bool active = true;      // false = parked (drills) or sent off
  bool sentOff = false;
  Vec2 pos, vel;
  float facing = 0;
  float stamina = 1;
  // Attributes 0..1. Both teams draw from the same distribution so only the brain differs between tiers.
  float pace = .7f, accel = .7f, shooting = .7f, passing = .7f, dribbling = .7f, tackling = .7f;
  float strength = .7f, vision = .7f, keeping = .3f;
  PState state = PState::Idle;
  float stateT = 0;        // time left in a timed state (kick, slide, recover, dive)
  float touchCD = 0;       // cannot take the ball while > 0
  float skillT = 0;        // dribble skill window: harder to tackle
  float fakeT = 0;         // fake-shot window: nearby defenders may bite
  float tackleCD = 0;
  bool slideHit = false;
  Vec2 diveDir;
  int yellows = 0;
  Vec2 target;             // last movement target (debug overlay)
  float maxSpeed() const { return lerpf(6.8f, 8.6f, pace) * (0.86f + 0.14f * stamina); }
  bool live() const { return active && !sentOff; }
};

enum class Act : uint8_t { None, Pass, Through, Lob, Cross, Shoot, Clear, Tackle, Slide, Skill, Dive };

// What a controller (AI or human assist) wants a player to do this tick. The Match turns it into physics.
struct Intent {
  Vec2 move;               // desired movement direction (unit or zero)
  float speed = 0;         // fraction of max speed (0..1)
  bool sprint = false;
  Vec2 dest;               // movement target, for the debug overlay
  Act act = Act::None;
  Vec2 point;              // kick target on the pitch
  float power = 0.6f;      // 0..1
  float curl = 0;          // -1..1 side spin request
  int targetId = -1;       // intended receiver (passes)
  int skill = 0;           // skill move index
};

// Lightweight model of the human's habits that tougher AI tiers adapt to (in-session, never leaves the device).
struct Scouting {
  float lane[3] = {1, 1, 1};     // share of carrier time on the low-y / centre / high-y channels
  float shotLow = 1, shotHigh = 1;   // shot / penalty direction counts (low y vs high y)
  float longBalls = 1, shortBalls = 1;
  float samples = 3;
  void observeCarrier(float y, float dt) {
    int c = y < kPitchW / 3 ? 0 : (y > 2 * kPitchW / 3 ? 2 : 1);
    lane[c] += dt;
    samples += dt;
  }
  void observeShot(float targetY) { (targetY < kMidY ? shotLow : shotHigh) += 1; }
  void observePass(bool lng) { (lng ? longBalls : shortBalls) += 1; }
  float favouredChannel() const;   // -1 low-y .. +1 high-y
  float penaltyLean() const { return (shotHigh - shotLow) / (shotHigh + shotLow); }
  float longBallShare() const { return longBalls / (longBalls + shortBalls); }
};

struct DrillState {
  int id = 0, attempt = 0, attemptsMax = 10;
  int success = 0, fail = 0, streak = 0, bestStreak = 0;
  int lastResult = 0;      // 0 none, 1 success, 2 fail
  float timeLeft = 0;      // seconds left in this attempt
  float resultTimer = 0;   // pause after an attempt
  bool complete = false;
  float limit = 20;
  int feederId = -1;       // AI player that serves the ball in cross drills
  int humanId = -1;        // fixed human player for drills that serve the ball to him
  float feedTimer = 0;
};

struct World {
  std::array<Player, kNumPlayers> p;
  Ball ball;
  int owner = -1;
  int lastTouchTeam = -1, lastTouchId = -1;
  bool lastTouchKick = false;
  int noHandleTeam = -1;          // keeper of this team may not pick up with hands (own team kicked last)
  int passTarget = -1, passTeam = -1;
  float passAge = 0;
  uint32_t offsideMask = 0;
  int offsideTeam = -1;
  Phase phase = Phase::Kickoff;
  Restart restart = Restart::KickOff;
  int restartTeam = 0;
  Vec2 restartSpot{kPitchL * 0.5f, kMidY};
  int taker = -1;
  float restartTimer = 0;
  float gameTime = 0;
  int half = 1;
  int score[2] = {0, 0};
  TeamStats stats[2];
  int kickoffTeam = 0;
  int activeHuman = -1;           // player id the human controls (-1 in self-play)
  bool humanControlled = true;
  int refereeProfile = 1;
  bool offsideOn = true, foulsOn = true;
  float since = 0;                // seconds since the last kick (AI timing)
  bool penaltyLive = false;       // a penalty has been struck and is still in play

  static int dir(int team) { return team == 0 ? 1 : -1; }
  Vec2 goalCentre(int attackingTeam) const { return {attackingTeam == 0 ? kPitchL : 0.0f, kMidY}; }
  Vec2 ownGoal(int team) const { return goalCentre(1 - team); }
  float progress(int team, Vec2 pos) const { return team == 0 ? pos.x / kPitchL : 1.0f - pos.x / kPitchL; }
  Vec2 ballXY() const { return ball.pos.xy(); }
};

class TeamAI;

class Match {
 public:
  explicit Match(const MatchConfig& cfg);
  ~Match();

  // Second half: the teams swap ends on screen. The engine keeps home attacking +x, so the snapshot is rotated
  // 180 degrees and the stick is rotated back, which makes the swap invisible to the simulation.
  void setInput(const Input& in) {
    in_ = in;
    if (w_.half == 2) { in_.moveX = -in.moveX; in_.moveY = -in.moveY; }
  }
  void advance(float realDt);        // accumulates real time; honours pause and time scale
  void tick();                        // exactly one fixed step
  void command(int cmd, float arg);
  void writeSnapshot(float* out) const;
  int drainEvents(int32_t* out, int maxEvents);

  const World& world() const { return w_; }
  const DrillState& drill() const { return drill_; }
  const MatchConfig& config() const { return cfg_; }
  Rng& rng() { return rng_; }

  // Commands (see api.h for the numeric ids).
  enum Cmd { kPause = 1, kTimeScale = 2, kReplayStart = 3, kReplayStop = 4, kSelectSkill = 5, kDrillNext = 6,
             kDrillRestart = 7, kSetAssist = 8, kSetFormationHome = 9, kSetDifficulty = 10, kReplaySpeed = 11,
             kSetAdaptive = 12 };

 private:
  friend class MatchTestAccess;
  // --- setup (match_core.cpp)
  void initPlayers();
  void placeFormation(int team, bool kickoffShape);
  // --- flow (match_core.cpp)
  void tickLive();
  void updateClock();
  void applyIntent(Player& p, const Intent& it);
  void movePlayer(Player& p, Vec2 dir, float speedFrac, bool sprint);
  void separatePlayers();
  void updatePossession();
  void tryPickup();
  float oneTwoArm_ = 0;   // CALL pressed with the ball: the next ground pass becomes pass-and-run
  Vec2 prevBallXY_{};   // ball position last tick, for swept (anti-tunnelling) contact tests
  void releaseBall();
  void chooseActive();
  Intent humanIntent(Player& p);
  void recordReplay();
  void advanceReplay();
  void gainPossession(Player& p);
  bool canKick(const Player& p) const;
  int bestPassTarget(const Player& p, Vec2 aim, bool through, Vec2* point) const;
  void updatePreview();
  // --- actions (match_actions.cpp)
  void kick(Player& p, Vec3 vel, Vec3 spin, EventType type, int targetId);
  void doPass(Player& p, Act kind, Vec2 point, float power, int targetId);
  void doShot(Player& p, Vec2 point, float power, float curl, bool humanTaken);
  void doClear(Player& p, Vec2 point, float power);
  void tackleAttempt(Player& p, bool slide);
  void resolveTackleContact(Player& tackler, Player& carrier, bool slide);
  void doSkill(Player& p, int skill);
  void keeperPhysics();
  void headerOrChest(Player& p);
  float pressureOn(const Player& p) const;
  // --- rules (match_rules.cpp)
  void checkBoundaries();
  void scoreGoal(int scoringTeam);
  void collideFrame();
  void beginRestart(Restart type, int team, Vec2 spot);
  void updateRestart();
  void enforceRestartSpacing();
  void foul(Player& offender, Player& victim, bool slide, bool serious);
  void markOffsides(int team, int passerId);
  bool checkOffsideOnTouch(Player& p);
  void endHalfOrMatch();
  void kickoffReset(int team);
  int pickTaker(Restart type, int team, Vec2 spot);
  void pushEvent(EventType t, int team, int player, int arg = 0);
  // --- drills (drills.cpp)
  void setupDrill();
  void updateDrill();
  // reason: 1 goal, 2 saved, 3 missed/off target, 4 lost the ball, 5 time up, 6 escaped the press, 7 held/won the ball, 8 conceded
  void finishAttempt(bool success, int reason);

  MatchConfig cfg_;
  World w_;
  Rng rng_;
  Input in_, prevIn_;
  std::array<Intent, kNumPlayers> intents_{};
  std::unique_ptr<TeamAI> ai_[2];
  Scouting scout_;
  DrillState drill_;
  float accum_ = 0;
  float timeScale_ = 1.0f;
  bool paused_ = false;
  float clockRate_ = 30.0f;       // game seconds per real second
  bool endPending_ = false;
  float endGrace_ = 0;
  // human charge state
  float chargePass_ = 0, chargeShoot_ = 0, chargeLob_ = 0, tackleHold_ = 0;
  bool passHeld_ = false, shootHeld_ = false, lobHeld_ = false, tackleHeld_ = false;
  float switchCD_ = 0, activeStick_ = 0;
  int selectedSkill_ = 0;
  float halfTimeTimer_ = 0, goalTimer_ = 0;
  int postCooldown_ = 0;
  Vec2 lastCarrierPos_;
  bool humanTouched_ = false;
  bool previewActive_ = false;
  bool outInDrill_ = false;
  int lastScorer_ = 0;
  float resultFlash_ = 0;
  // replay
  struct Frame { float v[3 + kNumPlayers * 4]; };
  std::vector<Frame> replay_;
  int replayWrite_ = 0;
  int replayCount_ = 0;
  bool replaying_ = false;
  float replayPos_ = 0, replaySpeed_ = 1.0f;
  int replayStart_ = 0, replayLen_ = 0;
  int tickCount_ = 0;
  std::vector<Event> events_;
  Vec3 predicted_[12];
};

}  // namespace rx
