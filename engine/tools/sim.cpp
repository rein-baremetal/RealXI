// Headless self-play: AI tier A (home) vs AI tier B (away). Used to tune and regression-test the AI.
// Usage: rx_sim [matches] [homeTier] [awayTier] [halfSeconds] [seed]
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "realxi/match.h"

int main(int argc, char** argv) {
  int n = argc > 1 ? atoi(argv[1]) : 4;
  int ht = argc > 2 ? atoi(argv[2]) : 1;
  int at = argc > 3 ? atoi(argv[3]) : 1;
  float half = argc > 4 ? static_cast<float>(atof(argv[4])) : 60.0f;
  uint32_t seed0 = argc > 5 ? static_cast<uint32_t>(atoi(argv[5])) : 1;
  int wins[2] = {0, 0}, draws = 0;
  double tot[2][14] = {{0}};
  double goals[2] = {0, 0};
  for (int m = 0; m < n; ++m) {
    rx::MatchConfig c;
    c.seed = seed0 + m;
    c.homeDifficulty = ht;
    c.difficulty = at;
    c.halfSeconds = half;
    c.formationHome = m % rx::kNumFormations;
    c.formationAway = (m * 3 + 1) % rx::kNumFormations;
    rx::Match match(c);
    long ticks = 0;
    const long maxTicks = static_cast<long>(half * 2 * 60 * 3 + 6000);
    while (match.world().phase != rx::Phase::FullTime && ticks < maxTicks) {
      match.tick();
      ++ticks;
      const rx::World& w = match.world();
      if (!std::isfinite(w.ball.pos.x) || !std::isfinite(w.ball.pos.y) || !std::isfinite(w.ball.pos.z)) {
        printf("NaN ball at tick %ld\n", ticks);
        return 2;
      }
      for (const auto& p : w.p)
        if (!std::isfinite(p.pos.x) || !std::isfinite(p.pos.y)) { printf("NaN player %d at tick %ld\n", p.id, ticks); return 2; }
    }
    const rx::World& w = match.world();
    goals[0] += w.score[0]; goals[1] += w.score[1];
    if (w.score[0] > w.score[1]) ++wins[0]; else if (w.score[1] > w.score[0]) ++wins[1]; else ++draws;
    for (int t = 0; t < 2; ++t) {
      const rx::TeamStats& s = w.stats[t];
      double v[14] = {s.possession, (double)s.shots, (double)s.shotsOnTarget, (double)s.passes, (double)s.passesCompleted,
                      (double)s.tackles, (double)s.tacklesWon, (double)s.fouls, (double)s.yellows, (double)s.reds,
                      (double)s.corners, (double)s.offsides, (double)s.saves, (double)s.xg};
      for (int k = 0; k < 14; ++k) tot[t][k] += v[k];
    }
    printf("match %d: %d-%d  phase=%d ticks=%ld shots %d/%d passes %d/%d vs %d/%d\n", m, w.score[0], w.score[1], (int)w.phase, ticks,
           w.stats[0].shots, w.stats[1].shots, w.stats[0].passesCompleted, w.stats[0].passes, w.stats[1].passesCompleted, w.stats[1].passes);
  }
  printf("\nTier %d (home) vs tier %d (away), %d matches: W%d D%d L%d  avg goals %.2f - %.2f\n", ht, at, n, wins[0], draws, wins[1], goals[0] / n, goals[1] / n);
  const char* names[14] = {"possession s", "shots", "on target", "passes", "completed", "tackles", "won", "fouls", "yellows", "reds", "corners", "offsides", "saves", "xG"};
  for (int k = 0; k < 14; ++k) printf("  %-13s %7.1f %7.1f\n", names[k], tot[0][k] / n, tot[1][k] / n);
  return 0;
}
