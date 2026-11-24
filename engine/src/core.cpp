#include "realxi/core.h"

namespace rx {

namespace {
//                         name       think  react  pass  shot  line  press aggr  risk  pat   shoot sprint gkRct gkRch gkRd  mark  run   trap  feint adapt setP
const AIProfile kProfiles[kNumTiers] = {
    {"Regular", 0.26f, 0.40f, 7.0f, 8.0f, 0.40f, 0.35f, 0.35f, 0.30f, 0.60f, 1.30f, 0.50f, 0.34f, 0.85f, 0.35f, 0.45f, 0.40f, 0.10f, 0.20f, 0.00f, 0.40f},
    {"Hard",    0.16f, 0.22f, 4.5f, 5.5f, 0.55f, 0.60f, 0.50f, 0.45f, 0.50f, 1.55f, 0.70f, 0.24f, 0.93f, 0.60f, 0.70f, 0.65f, 0.35f, 0.50f, 0.35f, 0.65f},
    {"Pro",     0.12f, 0.17f, 3.0f, 3.8f, 0.65f, 0.75f, 0.55f, 0.55f, 0.45f, 1.65f, 0.85f, 0.19f, 1.00f, 0.80f, 0.85f, 0.80f, 0.55f, 0.70f, 0.60f, 0.80f},
    {"Legend",  0.09f, 0.12f, 1.8f, 2.6f, 0.72f, 0.90f, 0.60f, 0.62f, 0.40f, 1.75f, 1.00f, 0.15f, 1.00f, 1.00f, 1.00f, 1.00f, 0.75f, 0.85f, 0.90f, 1.00f},
};

const Formation kFormations[kNumFormations] = {
#include "formations_table.inc"
};

// Rolling resistance: v' = -k v - c (matches stepBall so AI timing predictions stay accurate).
constexpr float kRollK = 0.40f;
constexpr float kRollC = 0.28f;
constexpr float kDragQuad = 0.0133f;   // air drag, a = -kDragQuad * |v| * v
constexpr float kMagnus = 0.00583f;    // a = kMagnus * (spin x v)
}  // namespace

const AIProfile& aiProfile(int tier) { return kProfiles[std::clamp(tier, 0, kNumTiers - 1)]; }
const Formation& formation(int index) { return kFormations[std::clamp(index, 0, kNumFormations - 1)]; }

Vec2 slotToWorld(const FormationSlot& s, int team) {
  if (team == 0) return {s.y * kPitchL, s.x * kPitchW};
  return {kPitchL - s.y * kPitchL, kPitchW - s.x * kPitchW};
}

void stepBall(Ball& b, float dt) {
  Vec3& p = b.pos;
  Vec3& v = b.vel;
  const bool grounded = p.z <= kBallR + 0.003f && std::fabs(v.z) < 0.25f;
  if (grounded) {
    p.z = kBallR;
    v.z = 0;
    float s = std::sqrt(v.x * v.x + v.y * v.y);
    if (s > 1e-4f) {
      float ns = std::max(0.0f, s - (kRollK * s + kRollC) * dt);
      float f = ns / s;
      v.x *= f;
      v.y *= f;
    } else {
      v.x = v.y = 0;
    }
    b.spin.z *= std::exp(-1.2f * dt);
    p.x += v.x * dt;
    p.y += v.y * dt;
    return;
  }
  float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  // Quadratic air drag.
  float ax = -kDragQuad * speed * v.x;
  float ay = -kDragQuad * speed * v.y;
  float az = -kDragQuad * speed * v.z - kGravity;
  // Magnus force = k * (spin x velocity).
  const Vec3& w = b.spin;
  ax += kMagnus * (w.y * v.z - w.z * v.y);
  ay += kMagnus * (w.z * v.x - w.x * v.z);
  az += kMagnus * (w.x * v.y - w.y * v.x);
  v.x += ax * dt;
  v.y += ay * dt;
  v.z += az * dt;
  p.x += v.x * dt;
  p.y += v.y * dt;
  p.z += v.z * dt;
  b.spin.x *= std::exp(-0.08f * dt);
  b.spin.y *= std::exp(-0.08f * dt);
  b.spin.z *= std::exp(-0.08f * dt);
  if (p.z < kBallR) {
    p.z = kBallR;
    if (v.z < -1.3f) {
      v.z = -v.z * 0.62f;
      v.x *= 0.88f;
      v.y *= 0.88f;
      b.spin.z *= 0.65f;
    } else {
      v.z = 0;
    }
  }
}

void predictBall(const Ball& b, float step, int count, Vec3* out) {
  Ball c = b;
  out[0] = c.pos;
  const int sub = std::max(1, static_cast<int>(std::lround(step / kDt)));
  for (int i = 1; i < count; ++i) {
    for (int k = 0; k < sub; ++k) stepBall(c, kDt);
    out[i] = c.pos;
  }
}

namespace {
float distAtSpeed(float v0, float arrive) {
  const float ck = kRollC / kRollK;
  float T = std::log((v0 + ck) / (arrive + ck)) / kRollK;
  return (v0 - arrive) / kRollK - kRollC * T / kRollK;
}
}  // namespace

float groundSpeedFor(float d, float arrive) {
  float lo = arrive + 0.01f, hi = 45.0f;
  if (distAtSpeed(hi, arrive) < d) return hi;
  for (int i = 0; i < 24; ++i) {
    float mid = 0.5f * (lo + hi);
    if (distAtSpeed(mid, arrive) < d) lo = mid; else hi = mid;
  }
  return 0.5f * (lo + hi);
}

float groundTimeFor(float v0, float d) {
  const float ck = kRollC / kRollK;
  if (distAtSpeed(v0, 0.0f) < d) return 1e9f;
  float arrive = 0.0f;
  float lo = 0.0f, hi = v0;  // search the arrival speed that yields distance d
  for (int i = 0; i < 24; ++i) {
    arrive = 0.5f * (lo + hi);
    if (distAtSpeed(v0, arrive) > d) lo = arrive; else hi = arrive;
  }
  return std::log((v0 + ck) / (arrive + ck)) / kRollK;
}

}  // namespace rx
