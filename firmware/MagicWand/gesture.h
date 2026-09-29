// gesture.h — grip-independent gesture engine (pure C++, no Arduino deps).
//
// Pipeline (runs at ~100 Hz):
//   raw accel/gyro -> FeatureFrame -> Segmenter -> resample(32) -> DTW classify
//
// WHY THIS IS GRIP-INDEPENDENT
// Raw IMU axes rotate with your hand: hold the wand upside down and "up" on the
// sensor becomes "down". Instead we describe motion using only things that do
// not care how the wand is rolled in the hand:
//   * the wand's long axis  A  (handle -> tip, fixed relative to the PCB), and
//   * gravity "up"          U  (tracked with a small complementary filter).
// From those we build a "tip frame" and emit 4 features per sample:
//   right  = sideways speed of the tip  (tip moving left/right)
//   upv    = vertical speed of the tip  (tip moving up/down)
//   twist  = spin around the wand axis  (key-turn)
//   thrust = acceleration along the axis (poke / stab)
// Rolling the wand around its own axis leaves all four numbers unchanged.
//
// This file is compiled both into the firmware and into test/test_gesture.cpp.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace wand {

constexpr int kFeat = 4;         // right, upv, twist, thrust
constexpr int kTplLen = 32;      // every gesture is resampled to this many points
constexpr int kMaxGestures = 8;
constexpr int kMaxSamples = 4;   // training samples kept per gesture
constexpr int kMaxSegLen = 250;  // 2.5 s at 100 Hz
constexpr int kPreRoll = 6;      // samples kept from just before motion started
constexpr int kNameLen = 16;
constexpr float kQ = 40.0f;      // quantization: int8 = round(feature * kQ)

struct Vec3 {
  float x, y, z;
};

// A resampled, amplitude-normalized gesture, quantized to int8 (128 bytes).
struct Template {
  int8_t f[kTplLen][kFeat];
};

struct GestureSlot {
  char name[kNameLen];
  uint8_t count;      // valid samples (0 = empty slot)
  uint8_t next;       // ring index: the sample that gets replaced next
  uint8_t pad[2];
  float threshold;    // accept distance, recomputed on every training sample
  Template samples[kMaxSamples];
};

// ---------------------------------------------------------------------------
class FeatureFrame {
 public:
  // Unit vector pointing from handle to tip, in IMU coordinates.
  void setAxis(Vec3 a);
  Vec3 axis() const { return axis_; }
  // Seed the gravity estimate from an accelerometer reading (in g).
  void reset(const Vec3& accG);
  // accG in g, gyroDps in deg/s, dt in seconds. Writes kFeat features and
  // returns "motion energy" (roughly deg/s) used for segmentation.
  float update(const Vec3& accG, const Vec3& gyroDps, float dt, float out[kFeat]);
  Vec3 up() const { return up_; }

  float gravAlpha = 0.02f;  // how fast the accelerometer corrects "up"

 private:
  Vec3 axis_{1, 0, 0};
  Vec3 up_{0, 0, 1};
  Vec3 lastSide_{0, -1, 0};
};

// ---------------------------------------------------------------------------
struct SegParams {
  float startEnergy = 120.0f;  // energy above this (for startHold samples) starts a gesture
  float stopEnergy = 45.0f;    // energy below this (for stopHold samples) ends it
  int startHold = 2;
  int stopHold = 12;           // 120 ms of calm
  int minLen = 14;             // shorter bursts are ignored (bumps, taps)
};

class Segmenter {
 public:
  SegParams p;
  // Feed one sample. Returns true when a complete gesture segment is ready;
  // read it with data()/length() before the next push().
  bool push(const float f[kFeat], float energy);
  bool active() const { return state_ == kActive; }
  int length() const { return len_; }
  const float (*data() const)[kFeat] { return buf_; }
  void reset();

 private:
  enum State { kIdle, kActive };
  State state_ = kIdle;
  float pre_[kPreRoll][kFeat] = {};
  int preHead_ = 0, preCount_ = 0;
  float buf_[kMaxSegLen][kFeat] = {};
  int len_ = 0, above_ = 0, quiet_ = 0;
};

// Resample a segment to kTplLen points, normalize its amplitude (so a gentle
// and a vigorous version of the same move look alike) and quantize.
void resample(const float (*seg)[kFeat], int n, Template& out);

// Dynamic-time-warping distance (Sakoe-Chiba band), normalized per step.
float dtw(const Template& a, const Template& b);

struct Match {
  int gid = -1;         // best gesture, -1 if none trained
  float dist = 1e9f;    // distance to best
  float second = 1e9f;  // distance to runner-up gesture
  bool accepted = false;
};

constexpr float kDefaultThreshold = 0.45f;

// Best match among trained slots; `accepted` applies the per-slot threshold
// and requires a clear margin over the runner-up.
Match classify(const GestureSlot* slots, int n, const Template& t, float globalThreshold);

// Recompute a slot's acceptance threshold from the spread of its own samples.
void updateThreshold(GestureSlot& s, float globalThreshold);

// Add a training sample (replaces the oldest when full) and update threshold.
void addSample(GestureSlot& s, const Template& t, float globalThreshold);

}  // namespace wand
