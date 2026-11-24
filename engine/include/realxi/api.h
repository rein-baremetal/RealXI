// Flat C API over Match. The JNI bridge and the desktop tools both use only this.
#pragma once
#include <cstdint>

#include "snapshot.h"

extern "C" {
// flags: bit0 offside, bit1 fouls, bit2 adaptive AI, bit3 infinite stamina.
void* rx_create(uint32_t seed, int mode, int drillId, int difficulty, int formationHome, int formationAway,
                float halfSeconds, int referee, float assist, int flags, int drillAttempts, int homeDifficulty);
void rx_destroy(void* h);
// Input buffer (floats): 0 moveX, 1 moveY, 2 sprint, 3 pass, 4 shoot, 5 lob, 6 tackle, 7 finesse, 8 call, 9 switch, 10 skill.
float* rx_input_buffer(void* h);
float* rx_snapshot_buffer(void* h);     // rx::snap::kFloats floats, refreshed by rx_step
int32_t* rx_event_buffer(void* h);      // kMaxEvents * 4 ints: type, team, player, arg
int rx_event_count(void* h);
void rx_step(void* h, float dtSeconds);
void rx_command(void* h, int cmd, float arg);
int rx_snapshot_floats();
}
