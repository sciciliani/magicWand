// gen_training_motions.cpp — synthetic F streams for the Training tab's move
// checker (app/training-moves.js), using the hand simulator of test_gesture.cpp.
//
// Prints JSON: [{ "move": "circle", "kind": "ok|slow|fast", "f": [[r,u,t,th,e], ...] }, ...]
// where each row is exactly what the wand streams as "F r u t th e" (x100,
// 25 Hz = every 4th sample at 100 Hz). test/test_training.mjs reads it.
#define main test_gesture_main
#include "test_gesture.cpp"
#undef main

struct Stream {
  FeatureFrame ff;
  bool first = true;
  int n = 0;
  bool comma = false;
};

static void streamSink(void* ctx, Vec3 acc, Vec3 gyro) {
  Stream* s = (Stream*)ctx;
  if (s->first) { s->ff.reset(acc); s->first = false; }
  float f[kFeat];
  float e = s->ff.update(acc, gyro, 0.01f, f);
  if (s->n++ % 4) return;  // 25 Hz, like the firmware's stream
  printf("%s[%d,%d,%d,%d,%d]", s->comma ? "," : "", (int)(f[0] * 100), (int)(f[1] * 100), (int)(f[2] * 100),
         (int)(f[3] * 100), (int)e);
  s->comma = true;
}

static bool firstItem = true;
static void emit(const char* name, const char* kind, int move, SimOpts o) {
  printf("%s\n{\"move\":\"%s\",\"kind\":\"%s\",\"f\":[", firstItem ? "" : ",", name, kind);
  firstItem = false;
  Stream s;
  s.ff.setAxis(o.mountAxis);
  simulate(move, o, streamSink, &s);
  printf("]}");
}

int main() {
  const Vec3 mounts[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}};
  struct { const char* name; int move; } targets[] = {{"circle", CIRCLE}, {"hit", FLICK_UP}, {"zigzag", ZIGZAG}};
  struct { const char* name; int move; } others[] = {{"swish-right", SWISH_RIGHT}, {"twist", TWIST},
                                                       {"stab", STAB}, {"slash-down", SLASH_DOWN},
                                                       {"swish-flick", SWISH_FLICK}};
  printf("[");
  for (int k = 0; k < 40; k++) {
    Vec3 m = mounts[k % 3];
    float roll = urand(0, 360);
    for (auto& t : targets) emit(t.name, "ok", t.move, human(roll, m));
    for (auto& t : targets) {  // 3x slower than normal
      SimOpts o = human(roll, m);
      o.dur *= 3.0f;
      emit(t.name, "slow", t.move, o);
    }
    {  // circle rushed in ~0.35 s
      SimOpts o = human(roll, m);
      o.dur = 0.4f;
      emit("circle", "fast", CIRCLE, o);
    }
    for (auto& t : others) emit(t.name, "other", t.move, human(roll, m));
    SimOpts r = human(roll, m);
    r.randomHandling = true;
    emit("random", "other", 0, r);
  }
  printf("\n]\n");
  return 0;
}
