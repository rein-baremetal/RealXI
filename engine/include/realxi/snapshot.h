// Layout of the float snapshot the engine writes every frame. Kotlin mirrors these offsets in engine/Snapshot.kt;
// the unit test tests/test_layout.py-style check (see tools/check_layout.py) keeps both in sync.
#pragma once

namespace rx {
namespace snap {

enum : int {
  kPhase = 0, kRestart, kRestartTeam, kClock, kHalf, kScoreHome, kScoreAway, kActive,
  kBallX, kBallY, kBallZ, kBallVX, kBallVY, kBallVZ, kOwner, kReplaying,
  kReplayProgress, kTimeScale, kChargePass, kChargeShoot, kActiveStamina, kDrillId, kDrillAttempt, kDrillSuccess,
  kDrillFail, kDrillLast, kDrillTimeLeft, kDrillStreak, kDrillBest, kPaused, kTaker, kDrillAttemptsMax,
  kDrillComplete, kPreview, kEndPending,
  kHeaderSize = 40,
  kPlayerStride = 10,
  kPlayers = kHeaderSize,                       // 22 players x 10 floats
  kStatsStride = 14,
  kStats = kPlayers + 22 * kPlayerStride,       // 2 teams x 14 floats
  kTargets = kStats + 2 * kStatsStride,         // 22 x (x, y) AI movement targets (debug overlay)
  kPredict = kTargets + 44,                     // 12 x (x, y, z) predicted ball / shot preview path
  kFloats = kPredict + 36,
  kMaxEvents = 48
};

}  // namespace snap
}  // namespace rx
