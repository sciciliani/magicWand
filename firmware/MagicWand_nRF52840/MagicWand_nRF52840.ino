// MagicWand.ino — XIAO nRF52840 Sense smart wand.
//
//   move the wand  ->  grip-independent gesture recognition  ->  IR code (TV / AC)
//
// Configure it from the web app (app/) over Bluetooth, or type the same
// commands in a serial monitor (115200). Protocol: docs/PROTOCOL.md.
//
// Board package: "Seeed nRF52 Boards" (non-mbed).  Library: "Seeed Arduino LSM6DS3".
#include <Arduino.h>
#include <bluefruit.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "config.h"
#include "gesture.h"
#include "haptics.h"
#include "imu.h"
#include "ir.h"
#include "power.h"
#include "storage.h"

using wand::kFeat;
using wand::kMaxGestures;

// ------------------------------------------------------------------ state
static wand::FeatureFrame frame;
static wand::Segmenter segmenter;

enum Mode { kNormal, kRecording, kLearning, kCalibrating };
static Mode mode = kNormal;
static int modeTarget = -1;       // gesture / code slot being recorded or learned
static uint32_t modeDeadline = 0;
static uint32_t recArmAt = 0;     // recording starts after the countdown buzz

static bool streaming = false;
static bool testMode = false;
static uint32_t lastMotionMs = 0;
static uint32_t cooldownUntil = 0;
static uint32_t nextSampleUs = 0;
static uint32_t lastBatteryMs = 0;
static uint32_t sampleCount = 0;

// calibration accumulator
static wand::Vec3 calSum = {0, 0, 0};
static int calN = 0;

static IrCode learnBuf;

// ------------------------------------------------------------------ helpers
static bool validG(int g) { return g >= 0 && g < kMaxGestures; }
static bool validC(int c) { return c >= 0 && c < IR_MAX_CODES; }

// Names travel without spaces on the wire ("Living_room_TV").
// Only [A-Za-z0-9_-] are kept, so names are always safe inside JSON.
static void setName(char* dst, size_t cap, const char* src) {
  size_t i = 0;
  for (; src && src[i] && i < cap - 1; i++) {
    char ch = src[i];
    bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-';
    dst[i] = ok ? ch : '_';
  }
  dst[i] = 0;
}

static void sendInfo() {
  static char buf[1600];
  int n = snprintf(buf, sizeof(buf),
                   "INFO {\"fw\":\"%s\",\"bat\":%u,\"volts\":%s,\"chg\":%d,\"axis\":[%s,%s,%s],"
                   "\"thr\":%s,\"sleep\":%lu,\"wake\":%u,\"haptics\":%u,\"motor\":%d,\"irrx\":%d,"
                   "\"test\":%d,\"g\":[",
                   FW_VERSION, power::batteryPercent(), FX(power::batteryVolts()).s, power::onCharger(),
                   FX(settings.axis[0], 3).s, FX(settings.axis[1], 3).s, FX(settings.axis[2], 3).s,
                   FX(settings.threshold).s,
                   (unsigned long)(settings.idleSleepMs / 1000), settings.wakeThreshold, settings.hapticsOn,
                   HAS_MOTOR, HAS_IR_RECEIVER, testMode);
  for (int g = 0; g < kMaxGestures; g++)
    n += snprintf(buf + n, sizeof(buf) - n, "%s{\"n\":\"%s\",\"c\":%u,\"t\":%s,\"b\":%d}", g ? "," : "",
                  gestures[g].name, gestures[g].count, FX(gestures[g].threshold).s,
                  settings.bind[g] == kNoBinding ? -1 : settings.bind[g]);
  n += snprintf(buf + n, sizeof(buf) - n, "],\"c\":[");
  for (int c = 0; c < IR_MAX_CODES; c++)
    n += snprintf(buf + n, sizeof(buf) - n, "%s{\"n\":\"%s\",\"k\":%u,\"l\":%u}", c ? "," : "", codes[c].name,
                  codes[c].khz, codes[c].n);
  snprintf(buf + n, sizeof(buf) - n, "]}");
  comm::out(buf);
}

static void dumpCode(int c) {
  // CODE <c> <khz> <name> <d1,d2,...> — same format the app uploads.
  const IrCode& k = codes[c];
  static char buf[4200];
  int n = snprintf(buf, sizeof(buf), "CODE %d %u %s ", c, k.khz, k.name[0] ? k.name : "-");
  for (int i = 0; i < k.n && n < (int)sizeof(buf) - 8; i++) n += snprintf(buf + n, sizeof(buf) - n, i ? ",%u" : "%u", k.d[i]);
  comm::out(buf);
}

static void goSleep(const char* why) {
  comm::outf("SLEEP %s", why);
  haptics::play(haptics::kSleep);
  uint32_t t = millis();
  while (haptics::busy() && millis() - t < 300) haptics::update();
  power::deepSleep(settings.wakeThreshold);
}

// ------------------------------------------------------------------ commands
static void handle(char* line) {
  char* save = nullptr;
  char* cmd = strtok_r(line, " ", &save);
  if (!cmd) return;
  char* a1 = strtok_r(nullptr, " ", &save);
  char* a2 = strtok_r(nullptr, " ", &save);
  int i1 = a1 ? atoi(a1) : -1;
  lastMotionMs = millis();  // talking to the wand keeps it awake

  if (!strcmp(cmd, "HELLO") || !strcmp(cmd, "INFO")) {
    sendInfo();
  } else if (!strcmp(cmd, "PING")) {
    comm::out("PONG");
  } else if (!strcmp(cmd, "STREAM")) {
    streaming = i1 == 1;
    comm::outf("OK STREAM %d", streaming);
  } else if (!strcmp(cmd, "TEST")) {
    testMode = i1 == 1;
    comm::outf("OK TEST %d", testMode);
  } else if (!strcmp(cmd, "REC")) {  // REC <g> [name]
    if (!validG(i1)) return comm::out("ERR REC slot");
    if (a2) setName(gestures[i1].name, wand::kNameLen, a2);
    mode = kRecording;
    modeTarget = i1;
    haptics::play(haptics::kRecord);
    recArmAt = millis() + 650;  // after the "tick, tick, GO" pattern
    modeDeadline = recArmAt + 5000;
    segmenter.reset();
    comm::outf("REC %d armed", i1);
  } else if (!strcmp(cmd, "GNAME")) {
    if (!validG(i1) || !a2) return comm::out("ERR GNAME");
    setName(gestures[i1].name, wand::kNameLen, a2);
    storage::saveGesture(i1);
    comm::outf("OK GNAME %d", i1);
  } else if (!strcmp(cmd, "GCLR")) {
    if (!validG(i1)) return comm::out("ERR GCLR");
    memset(&gestures[i1], 0, sizeof(wand::GestureSlot));
    storage::saveGesture(i1);
    comm::outf("OK GCLR %d", i1);
  } else if (!strcmp(cmd, "LEARN")) {  // LEARN <c> [name]
    if (!validC(i1)) return comm::out("ERR LEARN slot");
    if (!HAS_IR_RECEIVER) return comm::out("ERR LEARN no receiver fitted");
    memset(&learnBuf, 0, sizeof(learnBuf));
    setName(learnBuf.name, sizeof(learnBuf.name), a2 ? a2 : codes[i1].name);
    mode = kLearning;
    modeTarget = i1;
    ir::startLearn();
    comm::outf("LEARN %d waiting", i1);
  } else if (!strcmp(cmd, "CODE")) {  // CODE <c> <khz> <name> <d1,d2,...>
    char* nm = strtok_r(nullptr, " ", &save);
    char* list = strtok_r(nullptr, " ", &save);
    int khz = a2 ? atoi(a2) : 0;
    if (!validC(i1) || khz < 20 || khz > 60 || !nm || !list) return comm::out("ERR CODE args");
    IrCode& k = codes[i1];
    memset(&k, 0, sizeof(k));
    setName(k.name, sizeof(k.name), strcmp(nm, "-") ? nm : "");
    k.khz = khz;
    for (char* p = list; *p && k.n < IR_MAX_EDGES;) {
      char* end = p;
      unsigned long v = strtoul(p, &end, 10);
      if (end == p) break;  // garbage
      k.d[k.n++] = (uint16_t)(v > 65535 ? 65535 : v);
      p = (*end == ',') ? end + 1 : end;
    }
    if (k.n == 0) {
      k.khz = 0;
      return comm::out("ERR CODE empty");
    }
    storage::saveCode(i1);
    comm::outf("OK CODE %d %u", i1, k.n);
  } else if (!strcmp(cmd, "DUMPC")) {
    if (!validC(i1)) return comm::out("ERR DUMPC");
    dumpCode(i1);
  } else if (!strcmp(cmd, "CNAME")) {
    if (!validC(i1) || !a2) return comm::out("ERR CNAME");
    setName(codes[i1].name, sizeof(codes[i1].name), a2);
    storage::saveCode(i1);
    comm::outf("OK CNAME %d", i1);
  } else if (!strcmp(cmd, "CCLR")) {
    if (!validC(i1)) return comm::out("ERR CCLR");
    memset(&codes[i1], 0, sizeof(IrCode));
    storage::saveCode(i1);
    for (int g = 0; g < kMaxGestures; g++)
      if (settings.bind[g] == i1) settings.bind[g] = kNoBinding;
    storage::saveSettings();
    comm::outf("OK CCLR %d", i1);
  } else if (!strcmp(cmd, "SEND")) {
    if (!validC(i1) || codes[i1].khz == 0) return comm::out("ERR SEND empty");
    ir::send(codes[i1]);
    comm::outf("OK SEND %d", i1);
  } else if (!strcmp(cmd, "BIND")) {  // BIND <g> <c | ->
    if (!validG(i1) || !a2) return comm::out("ERR BIND");
    int c = strcmp(a2, "-") ? atoi(a2) : -1;
    settings.bind[i1] = validC(c) ? (uint8_t)c : kNoBinding;
    storage::saveSettings();
    comm::outf("OK BIND %d %d", i1, validC(c) ? c : -1);
  } else if (!strcmp(cmd, "CAL")) {
    mode = kCalibrating;
    calSum = {0, 0, 0};
    calN = 0;
    modeDeadline = millis() + 6000;
    comm::out("CAL hold the wand still, tip pointing straight up");
  } else if (!strcmp(cmd, "SET")) {  // SET <key> <value>
    if (!a1 || !a2) return comm::out("ERR SET");
    float v = atof(a2);
    if (!strcmp(a1, "thr") && v > 0.1f && v < 2.0f) {
      settings.threshold = v;
      for (int g = 0; g < kMaxGestures; g++) wand::updateThreshold(gestures[g], v);
      for (int g = 0; g < kMaxGestures; g++) storage::saveGesture(g);
    } else if (!strcmp(a1, "sleep") && v >= 5 && v <= 3600) {
      settings.idleSleepMs = (uint32_t)v * 1000;
    } else if (!strcmp(a1, "wake") && v >= 1 && v <= 63) {
      settings.wakeThreshold = (uint8_t)v;
    } else if (!strcmp(a1, "haptics")) {
      settings.hapticsOn = v != 0;
      haptics::setEnabled(settings.hapticsOn);
    } else {
      return comm::out("ERR SET key/value");
    }
    storage::saveSettings();
    comm::outf("OK SET %s %s", a1, a2);
  } else if (!strcmp(cmd, "BUZZ")) {
    haptics::pulse(i1 > 0 ? i1 : 100);
    comm::out("OK BUZZ");
  } else if (!strcmp(cmd, "BAT")) {
    comm::outf("BAT %u %s %d", power::batteryPercent(), FX(power::batteryVolts()).s, power::onCharger());
  } else if (!strcmp(cmd, "SLEEP")) {
    goSleep("requested");
  } else if (!strcmp(cmd, "RESET")) {
    if (!a1 || strcmp(a1, "yes")) return comm::out("ERR type RESET yes");
    storage::factoryReset();
    comm::out("OK RESET");
  } else {
    comm::outf("ERR unknown %s", cmd);
  }
}

// ------------------------------------------------------------------ gestures
static void onSegment() {
  wand::Template t;
  wand::resample(segmenter.data(), segmenter.length(), t);
  comm::outf("SEG %d", segmenter.length());

  if (mode == kRecording) {
    wand::GestureSlot& s = gestures[modeTarget];
    if (!s.name[0]) snprintf(s.name, wand::kNameLen, "move%d", modeTarget);
    wand::addSample(s, t, settings.threshold);
    storage::saveGesture(modeTarget);
    comm::outf("REC %d ok %u %s", modeTarget, s.count, FX(s.threshold).s);
    haptics::play(haptics::kSaved);
    mode = kNormal;
    cooldownUntil = millis() + COOLDOWN_MS;
    return;
  }

  wand::Match m = wand::classify(gestures, kMaxGestures, t, settings.threshold);
  comm::outf("MATCH %d %s %s %d", m.gid, FX(m.dist, 3).s, FX(m.second >= 1e8f ? -1.0f : m.second, 3).s, m.accepted);
  if (!m.accepted) {
    // Only complain about near misses, not every bump.
    if (m.gid >= 0 && m.dist < gestures[m.gid].threshold * 1.5f) haptics::play(haptics::kUnknown);
    return;
  }
  int c = settings.bind[m.gid] == kNoBinding ? -1 : settings.bind[m.gid];
  bool fire = c >= 0 && codes[c].khz && !testMode && (CAST_WHILE_CHARGING || !power::onCharger());
  haptics::play(haptics::kCast);
  if (fire) ir::send(codes[c]);
  comm::outf("CAST %d %d %d", m.gid, c, fire);
  cooldownUntil = millis() + COOLDOWN_MS;
}

static void sampleOnce() {
  wand::Vec3 acc, gyro;
  if (!imu::read(acc, gyro)) return;
  float f[kFeat];
  float energy = frame.update(acc, gyro, 1.0f / SAMPLE_HZ, f);
  imu::trackBias(energy);
  sampleCount++;

  uint32_t now = millis();
  if (energy > STILL_ENERGY) lastMotionMs = now;

  if (streaming && comm::bleNotifying() && (sampleCount % 4) == 0) {
    comm::outf("F %d %d %d %d %d", (int)(f[0] * 100), (int)(f[1] * 100), (int)(f[2] * 100), (int)(f[3] * 100),
               (int)energy);
  }

  if (mode == kCalibrating) {
    if (energy < 10) {
      calSum = {calSum.x + acc.x, calSum.y + acc.y, calSum.z + acc.z};
      calN++;
    } else {
      calSum = {0, 0, 0};
      calN = 0;
    }
    if (calN >= SAMPLE_HZ) {  // one still second
      float n = sqrtf(calSum.x * calSum.x + calSum.y * calSum.y + calSum.z * calSum.z);
      settings.axis[0] = calSum.x / n;
      settings.axis[1] = calSum.y / n;
      settings.axis[2] = calSum.z / n;
      frame.setAxis({settings.axis[0], settings.axis[1], settings.axis[2]});
      storage::saveSettings();
      comm::outf("CAL ok %s %s %s", FX(settings.axis[0], 3).s, FX(settings.axis[1], 3).s, FX(settings.axis[2], 3).s);
      haptics::play(haptics::kSaved);
      mode = kNormal;
    }
    return;
  }

  if (mode == kRecording && now < recArmAt) return;  // still counting down
  if (now < cooldownUntil) {
    segmenter.reset();
    return;
  }
  if (segmenter.push(f, energy)) onSegment();
}

// ------------------------------------------------------------------ setup / loop
void setup() {
  power::begin();  // before the SoftDevice starts
  haptics::begin();
  ir::begin();
  storage::begin();
  haptics::setEnabled(settings.hapticsOn);

  char name[16];
  snprintf(name, sizeof(name), "%s-%04X", BLE_NAME_PREFIX, (unsigned)(NRF_FICR->DEVICEADDR[0] & 0xFFFF));
  comm::begin(name);

  if (!imu::begin()) {
    comm::out("ERR IMU not found");
    haptics::play(haptics::kError);
  }
  frame.setAxis({settings.axis[0], settings.axis[1], settings.axis[2]});
  wand::Vec3 acc, gyro;
  if (imu::read(acc, gyro)) frame.reset(acc);

  haptics::play(haptics::kWake);
  lastMotionMs = millis();
  nextSampleUs = micros();
  comm::outf("BOOT %s %s wake=%d", name, FW_VERSION, power::wokeFromSleep());
}

void loop() {
  haptics::update();

  if (char* line = comm::poll()) handle(line);

  uint32_t now = millis();

  // Mode timeouts
  if (mode == kRecording && now > modeDeadline) {
    comm::outf("REC %d timeout", modeTarget);
    haptics::play(haptics::kError);
    mode = kNormal;
  }
  if (mode == kCalibrating && now > modeDeadline) {
    comm::out("CAL fail (not still)");
    haptics::play(haptics::kError);
    mode = kNormal;
  }
  if (mode == kLearning) {
    ir::LearnState s = ir::pollLearn(learnBuf);
    if (s == ir::kDone) {
      codes[modeTarget] = learnBuf;
      storage::saveCode(modeTarget);
      comm::outf("LEARN %d ok %u", modeTarget, learnBuf.n);
      haptics::play(haptics::kSaved);
      mode = kNormal;
    } else if (s == ir::kTimeout || s == ir::kOverflow) {
      comm::outf("LEARN %d %s", modeTarget, s == ir::kTimeout ? "timeout" : "overflow");
      haptics::play(haptics::kError);
      mode = kNormal;
    }
  }

  // 100 Hz sampling
  if ((int32_t)(micros() - nextSampleUs) >= 0) {
    nextSampleUs += 1000000 / SAMPLE_HZ;
    if ((int32_t)(micros() - nextSampleUs) > 50000) nextSampleUs = micros();  // fell behind (IR send)
    sampleOnce();
  }

  // Battery report every 30 s while connected
  if (comm::bleConnected() && now - lastBatteryMs > 30000) {
    lastBatteryMs = now;
    comm::outf("BAT %u %s %d", power::batteryPercent(), FX(power::batteryVolts()).s, power::onCharger());
  }

  // Idle -> deep sleep
  uint32_t idleLimit = comm::bleConnected() ? CONNECTED_SLEEP_MS : settings.idleSleepMs;
  if (mode == kNormal && !haptics::busy() && now - lastMotionMs > idleLimit) goSleep("idle");

  // Let FreeRTOS idle the CPU until the next sample is due.
  int32_t waitUs = (int32_t)(nextSampleUs - micros());
  if (waitUs > 1500 && !haptics::busy()) delay(waitUs / 1000);
}
