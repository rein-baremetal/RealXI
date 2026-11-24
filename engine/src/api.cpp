#include "realxi/api.h"

#include <cstring>

#include "realxi/match.h"

namespace {
struct Handle {
  rx::Match match;
  float in[16] = {0};
  float snap[rx::snap::kFloats] = {0};
  int32_t ev[rx::snap::kMaxEvents * 4] = {0};
  int evCount = 0;
  explicit Handle(const rx::MatchConfig& c) : match(c) {}
};
}  // namespace

extern "C" {

void* rx_create(uint32_t seed, int mode, int drillId, int difficulty, int formationHome, int formationAway,
                float halfSeconds, int referee, float assist, int flags, int drillAttempts, int homeDifficulty) {
  rx::MatchConfig c;
  c.seed = seed;
  c.mode = mode;
  c.drillId = drillId;
  c.difficulty = difficulty;
  c.homeDifficulty = homeDifficulty;
  c.formationHome = formationHome;
  c.formationAway = formationAway;
  c.halfSeconds = halfSeconds;
  c.referee = referee;
  c.assist = assist;
  c.offside = (flags & 1) != 0;
  c.fouls = (flags & 2) != 0;
  c.adaptiveAI = (flags & 4) != 0;
  c.infiniteStamina = (flags & 8) != 0;
  c.drillAttempts = drillAttempts;
  Handle* h = new Handle(c);
  h->match.writeSnapshot(h->snap);
  return h;
}

void rx_destroy(void* h) { delete static_cast<Handle*>(h); }
float* rx_input_buffer(void* h) { return static_cast<Handle*>(h)->in; }
float* rx_snapshot_buffer(void* h) { return static_cast<Handle*>(h)->snap; }
int32_t* rx_event_buffer(void* h) { return static_cast<Handle*>(h)->ev; }
int rx_event_count(void* h) { return static_cast<Handle*>(h)->evCount; }
int rx_snapshot_floats() { return rx::snap::kFloats; }

void rx_step(void* hp, float dt) {
  Handle* h = static_cast<Handle*>(hp);
  rx::Input in;
  in.moveX = h->in[0]; in.moveY = h->in[1];
  in.sprint = h->in[2] > 0.5f; in.pass = h->in[3] > 0.5f; in.shoot = h->in[4] > 0.5f; in.lob = h->in[5] > 0.5f;
  in.tackle = h->in[6] > 0.5f; in.finesse = h->in[7] > 0.5f; in.call = h->in[8] > 0.5f; in.sw = h->in[9] > 0.5f;
  in.skill = h->in[10] > 0.5f;
  h->match.setInput(in);
  h->match.advance(dt);
  h->match.writeSnapshot(h->snap);
  int n = h->match.drainEvents(h->ev, rx::snap::kMaxEvents);
  h->evCount = n;
}

void rx_command(void* hp, int cmd, float arg) {
  Handle* h = static_cast<Handle*>(hp);
  h->match.command(cmd, arg);
  h->match.writeSnapshot(h->snap);
}

}  // extern "C"
