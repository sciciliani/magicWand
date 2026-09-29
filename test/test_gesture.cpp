// test_gesture.cpp — host-side test for the gesture engine.
//
// Simulates a hand moving the wand (rigid-body kinematics -> synthetic IMU
// readings with noise, bias and human variation), trains every move with 3
// samples in ONE grip, then tests it in many different grips (rolled around
// the wand axis, including upside down) and with a different IMU mounting.
// Also checks that random handling (picking up, waving about) is rejected.
//
// Build & run:  make test      (from the repo root)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../firmware/common/gesture.h"

using namespace wand;

// ----------------------------------------------------------------- tiny math
struct M3 {
  float m[3][3];
};
static Vec3 mv(const M3& R, Vec3 v) {
  return {R.m[0][0] * v.x + R.m[0][1] * v.y + R.m[0][2] * v.z,
          R.m[1][0] * v.x + R.m[1][1] * v.y + R.m[1][2] * v.z,
          R.m[2][0] * v.x + R.m[2][1] * v.y + R.m[2][2] * v.z};
}
static Vec3 mtv(const M3& R, Vec3 v) {  // R^T v
  return {R.m[0][0] * v.x + R.m[1][0] * v.y + R.m[2][0] * v.z,
          R.m[0][1] * v.x + R.m[1][1] * v.y + R.m[2][1] * v.z,
          R.m[0][2] * v.x + R.m[1][2] * v.y + R.m[2][2] * v.z};
}
static M3 mm(const M3& A, const M3& B) {
  M3 C{};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      for (int k = 0; k < 3; k++) C.m[i][j] += A.m[i][k] * B.m[k][j];
  return C;
}
static M3 axang(Vec3 a, float ang) {  // Rodrigues
  float n = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  M3 R{};
  if (n < 1e-9f) {
    R.m[0][0] = R.m[1][1] = R.m[2][2] = 1;
    return R;
  }
  float x = a.x / n, y = a.y / n, z = a.z / n, c = cosf(ang), s = sinf(ang), t = 1 - c;
  R.m[0][0] = t * x * x + c;     R.m[0][1] = t * x * y - s * z; R.m[0][2] = t * x * z + s * y;
  R.m[1][0] = t * x * y + s * z; R.m[1][1] = t * y * y + c;     R.m[1][2] = t * y * z - s * x;
  R.m[2][0] = t * x * z - s * y; R.m[2][1] = t * y * z + s * x; R.m[2][2] = t * z * z + c;
  return R;
}
static Vec3 cr(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static Vec3 sc(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static Vec3 ad(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 nz(Vec3 a) {
  float n = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  return n > 1e-9f ? sc(a, 1 / n) : a;
}

static unsigned long long rng = 88172645463325252ull;
static float frand() {  // [0,1)
  rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
  return (rng >> 11) * (1.0 / 9007199254740992.0);
}
static float urand(float a, float b) { return a + (b - a) * frand(); }
static float gauss() {
  float u = frand() + 1e-9f, v = frand();
  return sqrtf(-2 * logf(u)) * cosf(6.2831853f * v);
}

// ----------------------------------------------------------------- gestures
// Each move is a time-profile in the *physical* world: how the tip moves
// (right / up / twist, deg/s) and how hard it's pushed along the wand (g),
// as a function of normalized time s in [0,1].
struct Motion {
  float right, up, twist, thrust;
};
static float bump(float s, float a, float b) {  // smooth 0..1..0 on [a,b]
  if (s <= a || s >= b) return 0;
  float x = (s - a) / (b - a);
  return sinf(3.14159265f * x) * sinf(3.14159265f * x);
}

enum MoveId { SWISH_RIGHT, FLICK_UP, TWIST, CIRCLE, STAB, SWISH_FLICK, ZIGZAG, SLASH_DOWN, NUM_MOVES };
static const char* kMoveNames[NUM_MOVES] = {"swish-right", "flick-up", "twist(aperio)", "circle(orbis)",
                                            "stab(stupefy)", "swish-and-flick", "zigzag(Z)", "slash-down"};
static const float kMoveDur[NUM_MOVES] = {0.45f, 0.30f, 0.45f, 0.9f, 0.35f, 0.8f, 0.9f, 0.4f};

static Motion profile(int move, float s) {
  const float P = 420;  // deg/s peak
  switch (move) {
    case SWISH_RIGHT: return {P * bump(s, 0, 1), 0, 0, 0};
    case FLICK_UP: return {0, P * 1.2f * bump(s, 0, 0.55f) - P * 0.5f * bump(s, 0.55f, 1), 0, 0};
    case TWIST: return {0, 0, 1.4f * P * bump(s, 0, 1), 0};
    case CIRCLE: {
      float a = 6.2831853f * s, e = bump(s, 0, 1) * 0.6f + 0.4f * (s > 0.02f && s < 0.98f);
      return {P * 0.8f * e * cosf(a), P * 0.8f * e * sinf(a), 0, 0};
    }
    case STAB: return {0, 0, 0, 2.2f * bump(s, 0, 0.5f) - 2.2f * bump(s, 0.5f, 1)};
    case SWISH_FLICK: return {P * bump(s, 0, 0.55f), -P * 1.1f * bump(s, 0.6f, 1), 0, 0};
    case ZIGZAG:
      return {P * (bump(s, 0, 0.33f) - 0.8f * bump(s, 0.33f, 0.66f) + bump(s, 0.66f, 1)),
              -P * 0.8f * bump(s, 0.33f, 0.66f), 0, 0};
    case SLASH_DOWN: return {-P * 0.3f * bump(s, 0, 1), -P * bump(s, 0, 1), 0, 0};
  }
  return {0, 0, 0, 0};
}

// ----------------------------------------------------------------- simulator
struct SimOpts {
  float rollDeg = 0;             // grip: rotation around the wand axis
  Vec3 mountAxis = {1, 0, 0};    // which IMU axis points to the tip
  float amp = 1, dur = 1;        // human variation
  float yaw0 = 0, pitch0 = 10;   // where the wand points when starting
  bool randomHandling = false;   // not a gesture: random waving/pickup
};

typedef void (*SampleSink)(void* ctx, Vec3 acc, Vec3 gyro);

static void simulate(int move, const SimOpts& o, SampleSink sink, void* ctx) {
  const float dt = 0.01f;
  // Body->world. Build the pose: yaw, pitch, then the grip roll around the
  // wand axis, then the IMU mounting (which body axis is the wand axis).
  Vec3 X = {1, 0, 0};
  M3 mount;  // maps mountAxis -> X
  {
    Vec3 a = nz(o.mountAxis);
    Vec3 c = cr(a, X);
    float s = sqrtf(c.x * c.x + c.y * c.y + c.z * c.z), d = a.x;
    if (s < 1e-6f) mount = axang({0, 0, 1}, d > 0 ? 0 : 3.14159265f);
    else mount = axang(c, atan2f(s, d));
  }
  M3 R = mm(mm(mm(axang({0, 0, 1}, o.yaw0 * 0.0174533f), axang({0, 1, 0}, -o.pitch0 * 0.0174533f)),
               axang(X, o.rollDeg * 0.0174533f)),
            mount);
  Vec3 bias = {gauss() * 1.5f, gauss() * 1.5f, gauss() * 1.5f};  // gyro bias, dps
  float T = kMoveDur[move] * o.dur;
  int pre = 50, n = (int)(T / dt), post = 40;
  if (o.randomHandling) n = 120;
  float wob[6];
  for (float& w : wob) w = urand(-1, 1);

  for (int i = 0; i < pre + n + post; i++) {
    Vec3 tip = nz(mv(R, nz(o.mountAxis)));  // wand direction in the world
    Vec3 upW = {0, 0, 1};
    Vec3 rightW = nz(cr(tip, upW));
    Vec3 tipUpW = nz(cr(rightW, tip));  // direction the tip moves when "up"

    Motion mo = {0, 0, 0, 0};
    if (i >= pre && i < pre + n) {
      float s = (i - pre) / (float)n;
      if (o.randomHandling) {
        mo = {150 * (wob[0] * sinf(5 * s + wob[1])), 150 * wob[2] * sinf(3 * s), 120 * wob[3] * cosf(4 * s),
              0.4f * wob[4] * sinf(7 * s)};
      } else {
        mo = profile(move, s);
        // faster execution -> proportionally higher rates
        float k = o.amp / o.dur;
        mo.right *= k; mo.up *= k; mo.twist *= k; mo.thrust *= k / o.dur;
      }
    }
    // physiological tremor
    mo.right += gauss() * 4; mo.up += gauss() * 4; mo.twist += gauss() * 4;

    // Tip velocity (deg/s) -> world angular velocity: w x tip = v  =>  w = tip x v
    Vec3 v = ad(sc(rightW, mo.right), sc(tipUpW, mo.up));
    Vec3 wW = ad(cr(tip, v), sc(tip, mo.twist));
    Vec3 aLinW = sc(tip, mo.thrust);

    Vec3 gyroB = mtv(R, wW);
    Vec3 accB = mtv(R, ad(upW, aLinW));
    Vec3 gyroMeas = ad(ad(gyroB, bias), {gauss() * 1.0f, gauss() * 1.0f, gauss() * 1.0f});
    Vec3 accMeas = ad(accB, {gauss() * 0.01f, gauss() * 0.01f, gauss() * 0.01f});
    sink(ctx, accMeas, gyroMeas);

    // integrate orientation
    Vec3 wb = sc(gyroB, 0.0174533f);
    float ang = sqrtf(wb.x * wb.x + wb.y * wb.y + wb.z * wb.z) * dt;
    R = mm(R, axang(wb, ang));
  }
}

// ----------------------------------------------------------------- pipeline
struct Pipe {
  FeatureFrame ff;
  Segmenter seg;
  bool rawMode = false;  // baseline: raw sensor axes (NOT grip invariant)
  bool first = true;
  int segments = 0;
  Template last;
};
static void sink(void* ctx, Vec3 acc, Vec3 gyro) {
  Pipe* p = (Pipe*)ctx;
  if (p->first) {
    p->ff.reset(acc);
    p->first = false;
  }
  float f[kFeat];
  float e = p->ff.update(acc, gyro, 0.01f, f);
  if (p->rawMode) {
    f[0] = gyro.x / 150; f[1] = gyro.y / 150; f[2] = gyro.z / 150;
    f[3] = (acc.x - p->ff.up().x) * 1.5f;
  }
  if (p->seg.push(f, e)) {
    resample(p->seg.data(), p->seg.length(), p->last);
    p->segments++;
  }
}

static bool capture(int move, const SimOpts& o, Vec3 axis, bool raw, Template& out) {
  Pipe p;
  p.ff.setAxis(axis);
  p.rawMode = raw;
  simulate(move, o, sink, &p);
  if (p.segments < 1) return false;
  out = p.last;
  return true;
}

static SimOpts human(float roll, Vec3 mount) {
  SimOpts o;
  o.rollDeg = roll;
  o.mountAxis = mount;
  o.amp = urand(0.7f, 1.3f);
  o.dur = urand(0.75f, 1.35f);
  o.yaw0 = urand(-40, 40);
  o.pitch0 = urand(-15, 30);
  return o;
}

static GestureSlot slots[kMaxGestures];

static void train(bool raw, Vec3 mount) {
  memset(slots, 0, sizeof(slots));
  for (int m = 0; m < NUM_MOVES; m++) {
    snprintf(slots[m].name, kNameLen, "%s", kMoveNames[m]);
    int got = 0, tries = 0;
    while (got < 3 && tries++ < 10) {
      Template t;
      if (capture(m, human(0, mount), mount, raw, t)) {
        addSample(slots[m], t, kDefaultThreshold);
        got++;
      }
    }
  }
}

struct Result {
  int ok = 0, wrong = 0, rejected = 0, missed = 0, total = 0;
};

static Result evaluate(bool raw, Vec3 mount, float roll, int trials, bool verbose) {
  Result r;
  for (int m = 0; m < NUM_MOVES; m++) {
    for (int k = 0; k < trials; k++) {
      Template t;
      r.total++;
      if (!capture(m, human(roll, mount), mount, raw, t)) { r.missed++; continue; }
      Match mt = classify(slots, NUM_MOVES, t, kDefaultThreshold);
      if (!mt.accepted) r.rejected++;
      else if (mt.gid == m) r.ok++;
      else {
        r.wrong++;
        if (verbose) printf("    confused %s -> %s (d=%.2f)\n", kMoveNames[m], kMoveNames[mt.gid], mt.dist);
      }
    }
  }
  return r;
}

int main() {
  int failures = 0;
  const Vec3 mounts[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}};
  const char* mountNames[] = {"IMU x->tip", "IMU y->tip", "IMU -z->tip"};

  printf("Gesture engine test: train 3 samples per move in ONE grip, test in 12 grips.\n\n");
  for (int mi = 0; mi < 3; mi++) {
    Vec3 mount = mounts[mi];
    train(false, mount);
    printf("[%s] thresholds:", mountNames[mi]);
    for (int m = 0; m < NUM_MOVES; m++) printf(" %.2f", slots[m].threshold);
    printf("\n");
    int ok = 0, total = 0, wrong = 0;
    for (int roll = 0; roll < 360; roll += 30) {
      Result r = evaluate(false, mount, (float)roll, 10, roll == 180);
      ok += r.ok; total += r.total; wrong += r.wrong;
      printf("  roll %3d deg: %3d/%3d recognized, %d wrong, %d rejected, %d not segmented\n", roll, r.ok,
             r.total, r.wrong, r.rejected, r.missed);
    }
    float acc = 100.0f * ok / total, err = 100.0f * wrong / total;
    printf("  => %.1f%% recognized, %.1f%% misfires\n\n", acc, err);
    if (acc < 85.0f || err > 3.0f) failures++;
  }

  // False-accept check: random handling should not cast spells.
  train(false, mounts[0]);
  int fa = 0, segs = 0, N = 200;
  for (int i = 0; i < N; i++) {
    Template t;
    SimOpts o = human(urand(0, 360), mounts[0]);
    o.randomHandling = true;
    if (!capture(0, o, mounts[0], false, t)) continue;
    segs++;
    if (classify(slots, NUM_MOVES, t, kDefaultThreshold).accepted) fa++;
  }
  printf("Random handling: %d segments detected out of %d, %d accepted as a spell (%.1f%%)\n", segs, N, fa,
         100.0f * fa / N);
  if (fa > N * 0.08f) failures++;

  // Baseline for comparison: the same classifier on raw IMU axes.
  train(true, mounts[0]);
  Result r0 = evaluate(true, mounts[0], 0, 10, false);
  Result r180 = evaluate(true, mounts[0], 180, 10, false);
  printf("\nBaseline (raw IMU axes, not grip-invariant): same grip %d/%d, upside down %d/%d\n", r0.ok,
         r0.total, r180.ok, r180.total);

  printf("\n%s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
