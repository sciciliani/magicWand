// gesture.cpp — see gesture.h for the big picture.
#include "gesture.h"

#include <math.h>
#include <string.h>

namespace wand {

// ---------------------------------------------------------------- vector math
static inline Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline Vec3 mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static inline float norm(Vec3 a) { return sqrtf(dot(a, a)); }
static inline Vec3 unit(Vec3 a, Vec3 fallback) {
  float n = norm(a);
  return n > 1e-6f ? mul(a, 1.0f / n) : fallback;
}

// Feature scaling: rotation features are in deg/s (divide so a brisk 300 deg/s
// swing is ~2), thrust is in g.
constexpr float kRotScale = 1.0f / 150.0f;
constexpr float kThrustScale = 1.5f;
constexpr float kDeg2Rad = 0.01745329252f;

// ---------------------------------------------------------------- FeatureFrame
void FeatureFrame::setAxis(Vec3 a) { axis_ = unit(a, Vec3{1, 0, 0}); }

void FeatureFrame::reset(const Vec3& accG) {
  up_ = unit(accG, Vec3{0, 0, 1});
  Vec3 side = cross(axis_, up_);
  if (norm(side) > 0.2f) lastSide_ = unit(side, lastSide_);
}

float FeatureFrame::update(const Vec3& acc, const Vec3& gyro, float dt, float out[kFeat]) {
  // 1) Track gravity in the sensor frame. A world-fixed vector seen from a
  //    rotating body evolves as dv/dt = -w x v.
  Vec3 w = mul(gyro, kDeg2Rad);
  up_ = sub(up_, mul(cross(w, up_), dt));
  // Accelerometer correction, trusted less while the wand is accelerating.
  float an = norm(acc);
  float trust = 1.0f - fabsf(an - 1.0f) * 4.0f;
  if (trust > 0 && an > 1e-3f) {
    up_ = add(mul(up_, 1.0f - gravAlpha * trust), mul(acc, gravAlpha * trust / an));
  }
  up_ = unit(up_, Vec3{0, 0, 1});

  // 2) Tip frame: A (along wand), U (up), L (horizontal, to the wand's right).
  const Vec3& A = axis_;
  Vec3 side = cross(A, up_);
  if (norm(side) > 0.2f) {
    lastSide_ = unit(side, lastSide_);
  } else {
    // Wand pointing (nearly) straight up/down: "sideways" is ill-defined, keep
    // the last good direction, re-orthogonalized against the axis.
    lastSide_ = unit(sub(lastSide_, mul(A, dot(lastSide_, A))), lastSide_);
  }
  const Vec3& L = lastSide_;

  // 3) Features.
  Vec3 tipVel = cross(gyro, A);                 // deg/s, direction the tip moves
  float right = dot(tipVel, L);
  float upv = dot(tipVel, up_);
  float twist = dot(gyro, A);
  float thrust = dot(acc, A) - dot(up_, A);     // remove gravity along the axis

  out[0] = right * kRotScale;
  out[1] = upv * kRotScale;
  out[2] = twist * kRotScale;
  out[3] = thrust * kThrustScale;

  return norm(gyro) + 150.0f * fabsf(thrust);
}

// ---------------------------------------------------------------- Segmenter
void Segmenter::reset() {
  state_ = kIdle;
  preHead_ = preCount_ = 0;
  len_ = above_ = quiet_ = 0;
}

bool Segmenter::push(const float f[kFeat], float energy) {
  if (state_ == kIdle) {
    memcpy(pre_[preHead_], f, sizeof(float) * kFeat);
    preHead_ = (preHead_ + 1) % kPreRoll;
    if (preCount_ < kPreRoll) preCount_++;

    above_ = energy > p.startEnergy ? above_ + 1 : 0;
    if (above_ >= p.startHold) {
      // Start: copy the pre-roll (oldest first) into the segment buffer.
      len_ = 0;
      int start = (preHead_ - preCount_ + kPreRoll) % kPreRoll;
      for (int i = 0; i < preCount_; i++) {
        memcpy(buf_[len_++], pre_[(start + i) % kPreRoll], sizeof(float) * kFeat);
      }
      state_ = kActive;
      quiet_ = 0;
    }
    return false;
  }

  // Active
  if (len_ >= kMaxSegLen) {  // kept moving too long: not a gesture, discard
    reset();
    return false;
  }
  memcpy(buf_[len_++], f, sizeof(float) * kFeat);
  quiet_ = energy < p.stopEnergy ? quiet_ + 1 : 0;
  if (quiet_ >= p.stopHold) {
    len_ -= (quiet_ - 2);  // trim the calm tail, keep 2 samples of it
    int n = len_;
    state_ = kIdle;
    above_ = 0;
    preCount_ = 0;
    preHead_ = 0;
    quiet_ = 0;
    return n >= p.minLen;
  }
  return false;
}

// ---------------------------------------------------------------- resample
void resample(const float (*seg)[kFeat], int n, Template& out) {
  float tmp[kTplLen][kFeat];
  for (int i = 0; i < kTplLen; i++) {
    float pos = (n <= 1) ? 0 : (float)i * (n - 1) / (kTplLen - 1);
    int i0 = (int)pos;
    int i1 = i0 + 1 < n ? i0 + 1 : i0;
    float fr = pos - i0;
    for (int k = 0; k < kFeat; k++) tmp[i][k] = seg[i0][k] * (1 - fr) + seg[i1][k] * fr;
  }
  // Amplitude normalization (RMS over the whole gesture).
  float ss = 0;
  for (int i = 0; i < kTplLen; i++)
    for (int k = 0; k < kFeat; k++) ss += tmp[i][k] * tmp[i][k];
  float rms = sqrtf(ss / (kTplLen * kFeat));
  float scale = 1.0f / (rms > 0.1f ? rms : 0.1f);
  for (int i = 0; i < kTplLen; i++)
    for (int k = 0; k < kFeat; k++) {
      float q = roundf(tmp[i][k] * scale * kQ);
      out.f[i][k] = (int8_t)(q > 127 ? 127 : (q < -127 ? -127 : q));
    }
}

// ---------------------------------------------------------------- DTW
static inline float stepCost(const int8_t* a, const int8_t* b) {
  float s = 0;
  for (int k = 0; k < kFeat; k++) {
    float d = (float)(a[k] - b[k]);
    s += d * d;
  }
  return sqrtf(s) / kQ;
}

float dtw(const Template& a, const Template& b) {
  constexpr int N = kTplLen;
  constexpr int W = 8;  // warping band
  constexpr float INF = 1e9f;
  float prev[N + 1], cur[N + 1];
  for (int j = 0; j <= N; j++) prev[j] = INF;
  prev[0] = 0;
  for (int i = 1; i <= N; i++) {
    for (int j = 0; j <= N; j++) cur[j] = INF;
    int lo = i - W < 1 ? 1 : i - W;
    int hi = i + W > N ? N : i + W;
    for (int j = lo; j <= hi; j++) {
      float best = prev[j - 1];
      if (prev[j] < best) best = prev[j];
      if (cur[j - 1] < best) best = cur[j - 1];
      cur[j] = stepCost(a.f[i - 1], b.f[j - 1]) + best;
    }
    for (int j = 0; j <= N; j++) prev[j] = cur[j];
  }
  return prev[N] / (2.0f * N);
}

// ---------------------------------------------------------------- classify
Match classify(const GestureSlot* slots, int n, const Template& t, float globalThreshold) {
  Match m;
  for (int g = 0; g < n; g++) {
    const GestureSlot& s = slots[g];
    if (s.count == 0) continue;
    float d = 1e9f;
    for (int k = 0; k < s.count && k < kMaxSamples; k++) {
      float dk = dtw(t, s.samples[k]);
      if (dk < d) d = dk;
    }
    if (d < m.dist) {
      m.second = m.dist;
      m.dist = d;
      m.gid = g;
    } else if (d < m.second) {
      m.second = d;
    }
  }
  if (m.gid >= 0) {
    float thr = slots[m.gid].threshold > 0 ? slots[m.gid].threshold : globalThreshold;
    bool clearWinner = m.second >= 1e8f || m.dist < 0.85f * m.second;
    m.accepted = m.dist < thr && clearWinner;
  }
  return m;
}

void updateThreshold(GestureSlot& s, float globalThreshold) {
  int n = s.count < kMaxSamples ? s.count : kMaxSamples;
  if (n < 2) {
    s.threshold = globalThreshold;
    return;
  }
  float sum = 0;
  int pairs = 0;
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++) {
      sum += dtw(s.samples[i], s.samples[j]);
      pairs++;
    }
  float spread = sum / pairs;
  // A sloppy-but-consistent user gets a looser threshold, capped so one
  // gesture can't swallow everything.
  float thr = 1.5f * spread + 0.1f;
  if (thr < globalThreshold) thr = globalThreshold;
  if (thr > globalThreshold * 1.8f) thr = globalThreshold * 1.8f;
  s.threshold = thr;
}

void addSample(GestureSlot& s, const Template& t, float globalThreshold) {
  s.samples[s.next] = t;
  s.next = (s.next + 1) % kMaxSamples;
  if (s.count < kMaxSamples) s.count++;
  updateThreshold(s, globalThreshold);
}

}  // namespace wand
