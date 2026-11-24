// Geometry and valuation helpers shared by the AI source files.
#pragma once
#include "realxi/ai.h"

namespace rx {
namespace aiutil {

constexpr int kTrackN = 41;            // 2 s of ball prediction at 0.05 s
constexpr float kTrackStep = 0.05f;

inline float goalAngle(Vec2 pos, Vec2 goal) {
  Vec2 a{goal.x, kMidY - kGoalHalfW}, b{goal.x, kMidY + kGoalHalfW};
  Vec2 va = a - pos, vb = b - pos;
  return std::fabs(std::atan2(cross(va, vb), dot(va, vb)));
}

// Expected goal probability for an open shot from pos at the goal centred on `goal`.
inline float xgAt(Vec2 pos, Vec2 goal) {
  float d = dist(pos, goal);
  return sigmoid(-1.1f - 0.12f * d + 2.2f * goalAngle(pos, goal));
}

// Value of having the ball at pos (rough chance it turns into a goal). Scale: ~0.02 own half .. ~0.9 on the six-yard box.
inline float possessionValue(const World& w, int team, Vec2 pos) {
  float prog = w.progress(team, pos);
  return sat(0.02f + 0.85f * xgAt(pos, w.goalCentre(team)) + 0.28f * prog * prog + 0.08f * prog);
}

inline float lossCost(const World& w, int team, Vec2 pos, const AIProfile& pr) {
  float prog = w.progress(team, pos);
  return lerpf(0.55f, 0.10f, sat(prog * 1.3f)) * lerpf(1.3f, 0.7f, pr.riskTolerance);
}

inline Vec2 clampPitch(Vec2 p, float margin = 0.5f) {
  return {clampf(p.x, margin, kPitchL - margin), clampf(p.y, margin, kPitchW - margin)};
}

// Time for a player to reach point q from rest-ish state (simple accel-limited approximation).
inline float timeTo(const Player& pl, Vec2 q, float reach = 0.0f) {
  float d = std::max(0.0f, dist(pl.pos, q) - reach);
  float v = 0.9f * pl.maxSpeed();
  // Existing velocity toward the target helps a little.
  float along = dot(pl.vel, norm(q - pl.pos));
  return d / v + 0.12f - 0.03f * clampf(along, 0.0f, 6.0f);
}

// Earliest interception of a moving ball by a player (ball below head height). Returns +inf if none within the track.
inline float interceptTime(const Player& pl, const Vec3* track, int n, Vec2* where) {
  for (int i = 0; i < n; ++i) {
    if (track[i].z > 2.0f) continue;
    float t = timeTo(pl, track[i].xy(), 0.7f);
    if (t <= i * kTrackStep + 0.02f) {
      if (where) *where = track[i].xy();
      return std::max(t, i * kTrackStep);
    }
  }
  return 1e9f;
}

// Probability that the opposition intercepts a pass from -> to. Ground balls are checked along the whole path;
// lofted balls only near the landing zone (they fly over players in the middle).
inline float interceptRisk(const World& w, int team, Vec2 from, Vec2 to, float v0, bool lofted, int receiver) {
  Vec2 d = to - from;
  float L = d.len();
  if (L < 0.5f) return 0.0f;
  float pSafe = 1.0f;
  const int N = 10;
  for (int k = 1; k <= N; ++k) {
    float f = static_cast<float>(k) / N;
    if (lofted && f < 0.8f) continue;
    Vec2 s = from + d * f;
    float ts = lofted ? (L * f) / std::max(v0 * 0.8f, 5.0f) : groundTimeFor(v0, L * f);
    if (ts > 50.0f) ts = 50.0f;
    // Ball speed at this point decides how hard it is to take cleanly.
    float vHere = lofted ? 8.0f : std::max(5.0f, v0 * (1.0f - 0.55f * f));
    float pickup = sat(1.0f - (vHere - 8.0f) / 26.0f);
    for (int i = 0; i < kNumPlayers; ++i) {
      const Player& o = w.p[i];
      if (o.team == team || !o.live()) continue;
      float tOpp = timeTo(o, s, lofted ? 1.2f : 0.9f);
      float margin = ts + 0.15f - tOpp;
      if (margin > 0) pSafe *= 1.0f - sigmoid(margin * 5.0f - 0.3f) * pickup;
    }
  }
  (void)receiver;
  return 1.0f - pSafe;
}

inline float nearestOpponent(const World& w, int team, Vec2 pos, int* idOut = nullptr) {
  float best = 1e9f;
  for (int i = 0; i < kNumPlayers; ++i) {
    const Player& o = w.p[i];
    if (o.team == team || !o.live()) continue;
    float d = dist(o.pos, pos);
    if (d < best) { best = d; if (idOut) *idOut = i; }
  }
  return best;
}

// Position (in attack progress terms) of the offside line for `team` attackers: second-last opponent.
inline float offsideLineProgress(const World& w, int team) {
  float a = -1, b = -1;  // two deepest opponent progress values from the attackers' viewpoint
  for (int i = 0; i < kNumPlayers; ++i) {
    const Player& o = w.p[i];
    if (o.team == team || !o.live()) continue;
    float pr = w.progress(team, o.pos);
    if (pr > a) { b = a; a = pr; } else if (pr > b) { b = pr; }
  }
  return std::max(b, 0.5f);
}

inline bool offsideAt(const World& w, int team, Vec2 pos, Vec2 ballPos) {
  float line = std::max(offsideLineProgress(w, team), w.progress(team, ballPos));
  return w.progress(team, pos) > line + 0.002f;
}


// ---------------------------------------------------------------------------------------------------------------------
// Ideas ported from PitchBot: an expected-threat (xT) grid, crowd detection, triangle support and a game-state plan.
// ---------------------------------------------------------------------------------------------------------------------

// Expected-threat grid, built once by value iteration (probability the possession ends in a goal from each zone).
// 12 zones along the pitch (attacking direction) x 8 across. Row/col are in attack-progress terms, so the same table
// serves both teams.
struct XtGrid {
  static constexpr int kCols = 12, kRows = 8;
  float v[kRows][kCols];
  XtGrid() {
    float shot[kRows][kCols], mv[kRows][kCols];
    for (int r = 0; r < kRows; ++r)
      for (int c = 0; c < kCols; ++c) {
        Vec2 pos{(c + 0.5f) / kCols * kPitchL, (r + 0.5f) / kRows * kPitchW};
        float xg = sigmoid(-1.1f - 0.12f * dist(pos, {kPitchL, kMidY}) + 2.2f * goalAngle(pos, {kPitchL, kMidY}));
        float prog = (c + 0.5f) / kCols;
        shot[r][c] = sat(0.15f + 0.75f * prog * prog) * (dist(pos, {kPitchL, kMidY}) < 32.0f ? 1.0f : 0.15f);
        v[r][c] = shot[r][c] * xg * 0.9f;
        mv[r][c] = 1.0f - shot[r][c];
      }
    for (int it = 0; it < 6; ++it) {
      float nv[kRows][kCols];
      for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c) {
          Vec2 a{(c + 0.5f) / kCols * kPitchL, (r + 0.5f) / kRows * kPitchW};
          float xg = sigmoid(-1.1f - 0.12f * dist(a, {kPitchL, kMidY}) + 2.2f * goalAngle(a, {kPitchL, kMidY}));
          float acc = 0;
          for (int r2 = 0; r2 < kRows; ++r2)
            for (int c2 = 0; c2 < kCols; ++c2) {
              Vec2 b{(c2 + 0.5f) / kCols * kPitchL, (r2 + 0.5f) / kRows * kPitchW};
              float d = dist(a, b);
              if (d < 4.0f || d > 40.0f) continue;
              float w = std::exp(-d / 12.0f) * (0.55f + 0.45f * sat((b.x - a.x) / 18.0f + 0.4f));
              acc += w * v[r2][c2] * std::exp(-d / 55.0f);
            }
          float norm = 0;
          for (int r2 = 0; r2 < kRows; ++r2)
            for (int c2 = 0; c2 < kCols; ++c2) {
              Vec2 b{(c2 + 0.5f) / kCols * kPitchL, (r2 + 0.5f) / kRows * kPitchW};
              float d = dist(a, b);
              if (d < 4.0f || d > 40.0f) continue;
              norm += std::exp(-d / 12.0f) * (0.55f + 0.45f * sat((b.x - a.x) / 18.0f + 0.4f));
            }
          nv[r][c] = shot[r][c] * xg * 0.9f + mv[r][c] * (norm > 0 ? acc / norm : 0.0f) * 0.92f;
        }
      for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c) v[r][c] = std::max(v[r][c], nv[r][c]);
    }
    // Normalise so the best zone is ~0.45 (comparable to possessionValue differences).
    float mx = 1e-6f;
    for (int r = 0; r < kRows; ++r) for (int c = 0; c < kCols; ++c) mx = std::max(mx, v[r][c]);
    for (int r = 0; r < kRows; ++r) for (int c = 0; c < kCols; ++c) v[r][c] = v[r][c] / mx * 0.45f;
  }
};

inline const XtGrid& xtGrid() {
  static const XtGrid g;
  return g;
}

// xT at a world position for the team attacking toward +x (team 0) or -x (team 1); bilinear between zone centres.
inline float xtAt(const World& w, int team, Vec2 pos) {
  const XtGrid& g = xtGrid();
  float prog = clampf(w.progress(team, pos), 0.0f, 0.9999f);
  float fx = prog * XtGrid::kCols - 0.5f;
  float fy = clampf(pos.y / kPitchW, 0.0f, 0.9999f) * XtGrid::kRows - 0.5f;
  int c0 = clampf(std::floor(fx), 0.0f, XtGrid::kCols - 1.0f), r0 = clampf(std::floor(fy), 0.0f, XtGrid::kRows - 1.0f);
  int c1 = std::min(c0 + 1, XtGrid::kCols - 1), r1 = std::min(r0 + 1, XtGrid::kRows - 1);
  float tx = clampf(fx - c0, 0.0f, 1.0f), ty = clampf(fy - r0, 0.0f, 1.0f);
  float a = lerpf(g.v[r0][c0], g.v[r0][c1], tx), b = lerpf(g.v[r1][c0], g.v[r1][c1], tx);
  return lerpf(a, b, ty);
}

// How crowded the ball carrier is: 0 free .. 1 swarmed. `count` receives opponents inside the crowd radius.
inline float crowdAround(const World& w, int team, Vec2 pos, int* count = nullptr) {
  float sum = 0;
  int n = 0;
  for (int i = 0; i < kNumPlayers; ++i) {
    const Player& o = w.p[i];
    if (o.team == team || !o.live() || o.role == Role::GK) continue;
    float d = dist(o.pos, pos);
    if (d < 7.0f) { ++n; sum += sat((7.0f - d) / 5.0f); }
  }
  if (count) *count = n;
  return sat(sum / 2.5f);
}

// Triangle bonus for a pass to `recv`: another team-mate stands 6-20 m from the receiver so that carrier, receiver
// and that team-mate form a 40-80 degree angle at the receiver, and the receiver->team-mate lane is not blocked.
inline float triangleSupport(const World& w, int team, const Player& me, const Player& recv, Vec2 P) {
  Vec2 toMe = norm(me.pos - P);
  float best = 0;
  for (int s = 0; s < kTeamSize; ++s) {
    int tid = team * kTeamSize + s;
    const Player& u = w.p[tid];
    if (tid == me.id || tid == recv.id || !u.live() || u.role == Role::GK) continue;
    float d = dist(u.pos, P);
    if (d < 6.0f || d > 20.0f) continue;
    Vec2 toU = norm(u.pos - P);
    float ang = std::acos(clampf(dot(toMe, toU), -1.0f, 1.0f)) * 180.0f / kPi;
    if (ang < 40.0f || ang > 80.0f) continue;
    float lane = 1.0f;
    for (int i = 0; i < kNumPlayers; ++i) {
      const Player& o = w.p[i];
      if (o.team == team || !o.live()) continue;
      if (distToSegment(o.pos, P, u.pos) < 1.8f) { lane = 0; break; }
    }
    best = std::max(best, lane * (1.0f - 0.5f * std::fabs(ang - 60.0f) / 20.0f));
  }
  return best;
}

// Game-state plan from the score and the clock. 0 neutral, 1 early press (level/behind early), 2 protect a lead, 3 chase.
inline int gameMode(const World& w, int team) {
  float ph = sat(w.gameTime / 5400.0f);
  int lead = w.score[team] - w.score[1 - team];
  if (lead >= 2 && ph >= 0.3f) return 2;
  if (lead >= 1 && ph >= 0.55f) return 2;
  if (lead <= -2 && ph >= 0.3f) return 3;
  if (lead <= -1 && ph >= 0.5f) return 3;
  if (ph < 0.5f && lead <= 0) return 1;
  return 0;
}

}  // namespace aiutil

struct TeamAI::Analysis {
  int carrierId = -1;
  bool ours = false, theirs = false, loose = false;
  Vec3 track[aiutil::kTrackN];
  float tBall[kNumPlayers];
  Vec2 ballPt[kNumPlayers];
  int presser = -1, cover = -1, interceptor = -1, oppInterceptor = -1;
  float ballProg = 0.5f;     // attack progress of the ball for this team (0 own goal .. 1 opposition goal)
  Vec2 ballXY;
  float oppBest = 1e9f, ourBest = 1e9f;
};

}  // namespace rx
