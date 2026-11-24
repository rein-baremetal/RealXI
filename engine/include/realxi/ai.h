// Team AI: one instance per side. Produces an Intent per player each tick; the Match turns intents into physics.
// All "hard level" decision logic lives in src/ai_*.cpp. The AI sees only what a player on the pitch could see.
#pragma once
#include "match.h"

namespace rx {

class TeamAI {
 public:
  struct Analysis;
  TeamAI(int team, int tier, int formationIdx, uint32_t seed);
  void setTier(int tier);
  void setFormation(int idx) { formation_ = idx; }
  const AIProfile& profile() const { return *prof_; }
  int formationIndex() const { return formation_; }

  // Fills `out` for every live player of this team except humanId (-1 = AI controls all).
  void think(const World& w, float dt, int humanId, const Scouting* scout, std::array<Intent, kNumPlayers>& out);

  // Formation anchor for a slot in world coordinates (own half shape, kickoff positions).
  Vec2 baseAnchor(const World& w, int slot) const;

 private:
  struct Per {
    float thinkT = 0;       // time until the next decision
    float haveBallT = 0;    // how long this player has held the ball (reaction delay)
    float runHold = 0;      // time until the off-ball run target is re-evaluated
    Vec2 run;               // current off-ball target
    float holdT = 0;        // keeper hold timer
    float reactT = -1;      // keeper: time since the current shot was first seen
    int markId = -1;
    float tackleWait = 0;
    float guess = 0;        // keeper penalty guess (-1 low-y, +1 high-y, 0 none)
    bool diveSent = false;
    Intent last;
  };
  void analyse(const World& w, Analysis& a) const;
  void defend(const World& w, const Analysis& a, int humanId, const Scouting* sc, std::array<Intent, kNumPlayers>& out);
  void attackOffBall(const World& w, const Analysis& a, int humanId, const Scouting* sc, std::array<Intent, kNumPlayers>& out);
  void setPiece(const World& w, const Analysis& a, int humanId, const Scouting* sc, std::array<Intent, kNumPlayers>& out);
  void keeper(const World& w, const Analysis& a, Per& st, Intent& it);
  void carrier(const World& w, const Analysis& a, const Player& me, Per& st, Intent& it, bool restartTaker);
  void looseBall(const World& w, const Analysis& a, int humanId, std::array<Intent, kNumPlayers>& out);
  Vec2 shapeTarget(const World& w, const Analysis& a, int slot, bool attacking, const Scouting* sc) const;

  int team_;
  const AIProfile* prof_;
  int tier_;
  int formation_;
  Rng rng_;
  Per per_[kTeamSize];
  float trapT_ = 0;
  bool trapOn_ = false;
  float trapCheck_ = 0;
  float dt_ = kDt;
  float sinceOwn_ = 0;
  float sinceWin_ = 99.0f;   // seconds since this team won the ball (counter-attack window)
  bool hadBall_ = false;
  float runRequest_ = 0;
  float pressRequest_ = 0;
  int oneTwoId_ = -1;
  float oneTwoT_ = 0;
  const Scouting* scout_ = nullptr;
 public:
  void requestRun(float seconds) { runRequest_ = seconds; }
  void requestOneTwo(int playerId, float seconds) { oneTwoId_ = playerId; oneTwoT_ = seconds; }   // pass-and-run: the passer sprints for the return
  void requestPress(float seconds) { pressRequest_ = seconds; }   // human "team press": everyone nearby closes the ball down  // human "call for pass": teammates make forward runs
};

}  // namespace rx
