// MagicWand.ino — XIAO MG24 Sense smart wand.
//
//   move the wand  ->  grip-independent gesture recognition  ->  IR code (TV / AC)
//
// Configure it from the web app (app/) over Bluetooth, or type the same
// commands in a serial monitor (115200). Protocol: docs/PROTOCOL.md.
//
// Board package: "Silicon Labs" (Tools > Protocol stack > BLE (Silabs)).
// Library: "Seeed Arduino LSM6DS3".
#include <Arduino.h>
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
#include <WatchdogTimer.h>  // Silicon Labs core library
#include "status.h"
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
static uint32_t lastMotionMs = 0;
static uint32_t cooldownUntil = 0;
static uint32_t nextSampleUs = 0;
static uint32_t lastBatteryMs = 0;
static uint32_t restartAt = 0;
static uint32_t sampleCount = 0;

// calibration accumulator
static wand::Vec3 calSum = {0, 0, 0};
static int calN = 0;

static IrCode learnBuf;
// LEARN countdown: 3, 2, 1 (a tick each, 1 s apart), 0 = "go" (long buzz),
// -1 = waiting for the go buzz to end, -2 = receiver on, listening.
static int8_t learnStep = -2;
static uint32_t learnNextMs = 0;

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

// Advertised name: "Wand-<custom name>" or "Wand-XXXX" (last 4 hex digits of the chip ID).
// The app finds wands by the "Wand" prefix, so it is always kept.
static const char* wandName() {
  static char name[20];
  if (settings.name[0]) snprintf(name, sizeof(name), "%s-%s", BLE_NAME_PREFIX, settings.name);
  else snprintf(name, sizeof(name), "%s-%04X", BLE_NAME_PREFIX, (unsigned)(getDeviceUniqueId() & 0xFFFF));
  return name;
}

static void sendInfo() {
  static char buf[1600];
  int n = snprintf(buf, sizeof(buf),
                   "INFO {\"name\":\"%s\",\"fw\":\"%s\",\"bat\":%u,\"volts\":%s,\"chg\":%d,\"axis\":[%s,%s,%s],"
                   "\"thr\":%s,\"sleep\":%lu,\"wake\":%u,\"haptics\":%u,\"motor\":%d,\"irrx\":%d,"
                   "\"store\":\"%s\",\"g\":[",
                   wandName(), FW_VERSION, power::batteryPercent(), FX(power::batteryVolts()).s, power::onCharger(),
                   FX(settings.axis[0], 3).s, FX(settings.axis[1], 3).s, FX(settings.axis[2], 3).s,
                   FX(settings.threshold).s,
                   (unsigned long)(settings.idleSleepMs / 1000), settings.wakeThreshold, settings.hapticsOn,
                   HAS_MOTOR, HAS_IR_RECEIVER, storage::backendName());
  for (int g = 0; g < kMaxGestures; g++)
    n += snprintf(buf + n, sizeof(buf) - n, "%s{\"n\":\"%s\",\"c\":%u,\"t\":%s,\"b\":%d}", g ? "," : "",
                  gestures[g].name, gestures[g].count, FX(gestures[g].threshold).s,
                  settings.bind[g] == kNoBinding ? -1 : settings.bind[g]);
  n += snprintf(buf + n, sizeof(buf) - n, "],\"c\":[");
  for (int c = 0; c < IR_MAX_CODES; c++)
    n += snprintf(buf + n, sizeof(buf) - n, "%s{\"n\":\"%s\",\"k\":%u,\"l\":%u}", c ? "," : "", codes[c].name,
                  codes[c].khz, ir::rawLength(codes[c]));
  snprintf(buf + n, sizeof(buf) - n, "]}");
  comm::out(buf);
}

static uint16_t rawBuf[IR_RAW_MAX];

// "IRCODE 0 AC_20 coolix-like 48 bits x2 B24D1FE048B7" — human-readable summary.
static void describeCode(int c) {
  const IrCode& k = codes[c];
  char h[IR_MAX_BYTES * 2 + 1];
  ir::hex(k, h, sizeof(h));
  comm::outf("IRCODE %d %s %u bits x%u %s  (%s, hdr %u/%u bit %u one %u zero %u gap %u %s %ukHz)", c,
             k.name[0] ? k.name : "-", k.nbits, k.repeats, h, ir::protocolName(k), k.hdrMark, k.hdrSpace,
             k.bitMark, k.one, k.zero, k.gap, k.pw ? "pulse-width" : "pulse-distance", k.khz);
}

// Serial only: just the code, how many times it repeats, and the carrier.
static void printCodeShort(const IrCode& k) {
  char h[IR_MAX_BYTES * 2 + 1], line[100];
  ir::hex(k, h, sizeof(h));
  snprintf(line, sizeof(line), "IRDUMP code %s  repeats %u  %u kHz", k.nbits ? h : "(none)", k.repeats, k.khz);
  comm::serialOut(line);
}

static void dumpCode(int c) {
  // CODE <c> <khz> <name> <d1,d2,...> — same format the app uploads.
  const IrCode& k = codes[c];
  static char buf[4200];
  int n = snprintf(buf, sizeof(buf), "CODE %d %u %s ", c, k.khz, k.name[0] ? k.name : "-");
  int len = ir::toRaw(k, rawBuf, IR_RAW_MAX);
  for (int i = 0; i < len && n < (int)sizeof(buf) - 8; i++)
    n += snprintf(buf + n, sizeof(buf) - n, i ? ",%u" : "%u", rawBuf[i]);
  comm::out(buf);
  if (k.khz) describeCode(c);
}

static void goSleep(const char* why) {
  comm::outf("SLEEP %s", why);
  haptics::play(haptics::kSleep);
  uint32_t t = millis();
  while (haptics::busy() && millis() - t < 300) haptics::update();
  power::deepSleep();
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
  } else if (!strcmp(cmd, "EXPORT")) {  // print a ready-to-paste default_spells.h
    storage::exportDefaults(comm::out);
  } else if (!strcmp(cmd, "IMU")) {  // debug: one raw reading
    wand::Vec3 a, g;
    bool ok = imu::read(a, g);
    comm::outf("IMU %s acc %s %s %s g, gyro %s %s %s dps", ok ? "ok" : "READ-FAILED", FX(a.x).s, FX(a.y).s,
               FX(a.z).s, FX(g.x, 1).s, FX(g.y, 1).s, FX(g.z, 1).s);
  } else if (!strcmp(cmd, "STREAM")) {
    streaming = i1 == 1;
    comm::outf("OK STREAM %d", streaming);
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
    // Countdown first (3, 2, 1, go), run from loop(). The receiver is only
    // switched on after the last buzz: motor and receiver never run at the
    // same time (motor noise on the supply = garbage edges).
    ir::stopLearn();
    haptics::off();
    mode = kLearning;
    modeTarget = i1;
    learnStep = 3;
    learnNextMs = millis();
  } else if (!strcmp(cmd, "CODE")) {  // CODE <c> <khz> <name> <d1,d2,...>  (raw µs, decoded on arrival)
    char* nm = strtok_r(nullptr, " ", &save);
    char* list = strtok_r(nullptr, " ", &save);
    int khz = a2 ? atoi(a2) : 0;
    if (!validC(i1) || khz < 20 || khz > 60 || !nm || !list) return comm::out("ERR CODE args");
    int n = 0;
    for (char* p = list; *p && n < IR_RAW_MAX;) {
      char* end = p;
      unsigned long v = strtoul(p, &end, 10);
      if (end == p) break;  // garbage
      rawBuf[n++] = (uint16_t)(v > 65535 ? 65535 : v);
      p = (*end == ',') ? end + 1 : end;
    }
    IrCode k;
    memset(&k, 0, sizeof(k));
    setName(k.name, sizeof(k.name), strcmp(nm, "-") ? nm : "");
    if (!ir::decode(rawBuf, n, khz, k)) return comm::out("ERR CODE can't decode (unsupported format)");
    codes[i1] = k;
    if (!storage::saveCode(i1)) return comm::out("ERR storage full or failed");
    comm::outf("OK CODE %d %d", i1, ir::rawLength(k));
    describeCode(i1);
  } else if (!strcmp(cmd, "TRY")) {  // TRY <khz> <d1,d2,...>  send once without storing (app: test a preset)
    int khz = a1 ? atoi(a1) : 0;
    char* list = a2;
    if (khz < 20 || khz > 60 || !list) return comm::out("ERR TRY args");
    int n = 0;
    for (char* p = list; *p && n < IR_RAW_MAX;) {
      char* end = p;
      unsigned long v = strtoul(p, &end, 10);
      if (end == p) break;
      rawBuf[n++] = (uint16_t)(v > 65535 ? 65535 : v);
      p = (*end == ',') ? end + 1 : end;
    }
    static IrCode k;
    memset(&k, 0, sizeof(k));
    if (!ir::decode(rawBuf, n, khz, k)) return comm::out("ERR TRY can't decode");
    status::solid(true);
    ir::send(k);
    status::solid(false);
    comm::out("OK TRY");
  } else if (!strcmp(cmd, "HEX")) {  // HEX <c> <nec|samsung|coolix|sony|rca> <name> <hex> [repeats]
    char* nm = strtok_r(nullptr, " ", &save);
    char* hx = strtok_r(nullptr, " ", &save);
    char* rp = strtok_r(nullptr, " ", &save);
    if (!validC(i1) || !a2 || !nm || !hx) return comm::out("ERR HEX args: HEX <c> <nec|samsung|coolix|sony|rca> <name> <hex> [repeats]");
    IrCode k;
    memset(&k, 0, sizeof(k));
    if (!ir::setProtocol(a2, k)) return comm::out("ERR HEX protocol: nec, samsung, coolix, sony or rca");
    if (!ir::fromHex(hx, k)) return comm::out("ERR HEX value");
    if (rp) {
      int r = atoi(rp);
      k.repeats = r < 1 ? 1 : (r > IR_MAX_REPEATS ? IR_MAX_REPEATS : r);
    }
    setName(k.name, sizeof(k.name), strcmp(nm, "-") ? nm : "");
    codes[i1] = k;
    if (!storage::saveCode(i1)) return comm::out("ERR storage full or failed");
    comm::outf("OK CODE %d %d", i1, ir::rawLength(k));
    describeCode(i1);
  } else if (!strcmp(cmd, "IRDUMP")) {  // IRDUMP = last learned code, IRDUMP <c> = slot c (Serial only)
    if (a1 && validC(i1)) printCodeShort(codes[i1]);
    else printCodeShort(learnBuf);
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
    status::solid(true);
    ir::send(codes[i1]);
    status::solid(false);
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
    if (!strcmp(a1, "name")) {  // SET name <name|->  (max 11 chars, "-" = default Wand-XXXX)
      if (!strcmp(a2, "-")) settings.name[0] = 0;
      else setName(settings.name, sizeof(settings.name), a2);
      storage::saveSettings();
      // The new name is advertised after a restart. Restarting while the app
      // is still connected hung the wand, so wait for the app to disconnect
      // (it does so on this reply), or 5 s at most (see loop()).
      comm::outf("OK SET name %s (restarting)", wandName());
      restartAt = millis() + 5000;
      return;
    }
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
      return comm::out("ERR SET: thr 0.1-2 | sleep <seconds 5-3600> | wake 1-63 | haptics 0/1 | name <name>");
    }
    storage::saveSettings();
    if (!strcmp(a1, "sleep")) comm::outf("OK SET sleep %s (sleeps after %s s without motion)", a2, a2);
    else comm::outf("OK SET %s %s", a1, a2);
  } else if (!strcmp(cmd, "BUZZ")) {
    if (mode == kLearning) return comm::out("ERR BUZZ busy learning");
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
    if (!storage::saveGesture(modeTarget)) comm::out("ERR storage full or failed");
    comm::outf("REC %d ok %u %s", modeTarget, s.count, FX(s.threshold).s);
    haptics::play(haptics::kSaved);
    status::flash(status::kSaved);
    mode = kNormal;
    cooldownUntil = millis() + COOLDOWN_MS;
    return;
  }

  wand::Match m = wand::classify(gestures, kMaxGestures, t, settings.threshold);
  comm::outf("MATCH %d %s %s %d", m.gid, FX(m.dist, 3).s, FX(m.second >= 1e8f ? -1.0f : m.second, 3).s, m.accepted);
  if (!m.accepted) return;  // unrecognized moves stay silent
  int c = settings.bind[m.gid] == kNoBinding ? -1 : settings.bind[m.gid];
  bool fire = c >= 0 && codes[c].khz && (CAST_WHILE_CHARGING || !power::onCharger());
  haptics::play(haptics::kCast);
  status::flash(status::kCast);
  if (fire) {
    status::solid(true);
    ir::send(codes[c]);
    status::solid(false);
  }
  comm::outf("CAST %d %d %d", m.gid, c, fire);
  cooldownUntil = millis() + COOLDOWN_MS;
}

static void sampleOnce() {
  wand::Vec3 acc, gyro;
  static uint32_t lastImuComplaint = 0;
  if (!imu::read(acc, gyro)) {
    if (millis() - lastImuComplaint > 5000) {
      lastImuComplaint = millis();
      comm::out("ERR IMU read failed");
    }
    return;
  }
  // All zeros = IMU not answering (unpowered or wrong I2C bus).
  if (acc.x == 0 && acc.y == 0 && acc.z == 0 && millis() - lastImuComplaint > 5000) {
    lastImuComplaint = millis();
    comm::out("ERR IMU returns zeros");
  }
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
      status::flash(status::kSaved);
      mode = kNormal;
    }
    return;
  }

  // While learning an IR code: no spell detection, so no buzz and no IR send
  // while the receiver is listening (you move the wand to aim the remote).
  if (mode == kLearning) {
    segmenter.reset();
    return;
  }
  if (mode == kRecording && now < recArmAt) return;  // still counting down
  if (now < cooldownUntil) {
    segmenter.reset();
    return;
  }
  if (segmenter.push(f, energy)) onSegment();
}



// ------------------------------------------------------------------ button
// Microswitch between PIN_BUTTON and GND.
//   short press  -> restart          hold BUTTON_FACTORY_MS -> factory reset
static void flushOut() {
#if USE_SERIAL
  Serial.flush();  // let the message leave before the board resets
#endif
  delay(100);      // and give BLE a moment to send it too
}

static void pollButton() {
#if HAS_BUTTON
  static uint32_t downSince = 0;
  static bool down = false, fired = false, armed = false;
  static uint8_t lastCountdown = 0;
  bool pressed = digitalRead(PIN_BUTTON) == LOW;
  uint32_t now = millis();
  // If the button woke us (still held at boot), wait for it to be released
  // first, or releasing it would count as a "restart" press.
  if (!armed) {
    if (!pressed) {
      armed = true;
      comm::out("BUTTON ready (tap = restart, hold 5 s = factory reset)");
    }
    return;
  }
  if (pressed && !down) {
    down = true;
    fired = false;
    downSince = now;
    lastCountdown = 0;
    comm::out("BUTTON pressed (release = restart, keep holding = factory reset)");
  } else if (pressed && down && !fired) {
    uint32_t held = now - downSince;
    uint8_t sec = held / 1000;
    if (sec > lastCountdown && held < BUTTON_FACTORY_MS) {  // 1 s, 2 s, ...
      lastCountdown = sec;
      comm::outf("BUTTON held %u s: factory reset in %lu s", sec,
                 (unsigned long)((BUTTON_FACTORY_MS - held + 999) / 1000));
    }
    if (held >= BUTTON_FACTORY_MS) {
      fired = true;
      comm::out("BUTTON factory reset: erasing spells and codes, then restarting");
      haptics::off();
      storage::factoryReset();
      haptics::play(haptics::kSaved);
      status::flash(status::kSaved);
      uint32_t t = millis();
      while (haptics::busy() && millis() - t < 1000) haptics::update();
      comm::out("BUTTON factory reset done: restarting now");
      flushOut();
      systemReset();
    }
  } else if (!pressed && down) {
    down = false;
    lastMotionMs = now;
    uint32_t held = now - downSince;
    if (fired) {
      // factory reset already handled
    } else if (held > 50) {  // debounced short press
      comm::outf("BUTTON released after %lu ms: restarting now", (unsigned long)held);
      flushOut();
      systemReset();
    } else {
      comm::out("BUTTON glitch ignored (< 50 ms)");
    }
  }
#endif
}

// ------------------------------------------------------------------ setup / loop
// Boot trace (config.h BOOT_TRACE): 3 quick yellow blinks = setup() started,
// LED stays ON while setup runs and goes OFF when it finishes. Each step is
// printed on the Serial Monitor, so a hang shows exactly where it stopped.
#if BOOT_TRACE && USE_SERIAL
#define TRACE(msg) do { Serial.println("trace: " msg); Serial.flush(); } while (0)
#else
#define TRACE(msg) do { } while (0)
#endif

void setup() {
  power::earlyBoot();  // WAKE_POLL: may go straight back to sleep if not moved

  // ALWAYS start the UART first and wait 1.5 s, on every boot and every
  // wake-up, whatever USE_SERIAL / BOOT_TRACE say. This is exactly what the
  // BOOT_TRACE build did, and it's the only start-up where the app
  // (Bluetooth) worked; every build that skipped it had a dead app link.
  // USE_SERIAL now only decides whether lines are printed to the monitor.
  Serial.begin(115200);
  delay(1500);

#if BOOT_TRACE
  pinMode(PIN_USER_LED, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_USER_LED, LOW); delay(100);
    digitalWrite(PIN_USER_LED, HIGH); delay(100);
  }
  digitalWrite(PIN_USER_LED, LOW);  // ON until setup() finishes
#endif
  TRACE("setup started");
  power::begin();
#if BOOT_TRACE
  digitalWrite(PIN_USER_LED, LOW);  // power::begin() turned it off
#endif
#if HAS_BUTTON
  pinMode(PIN_BUTTON, INPUT_PULLUP);
#endif
  TRACE("haptics");
  haptics::begin();
  TRACE("ir");
  ir::begin();
  TRACE("storage");
  storage::begin();
  haptics::setEnabled(settings.hapticsOn);

  const char* name = wandName();
  TRACE("bluetooth");
  comm::begin(name);

  TRACE("imu");
  if (!imu::begin()) {
    comm::out("ERR IMU not found");
    haptics::play(haptics::kError);
    status::flash(status::kError);
  }
  frame.setAxis({settings.axis[0], settings.axis[1], settings.axis[2]});
  wand::Vec3 acc, gyro;
  if (imu::read(acc, gyro)) frame.reset(acc);
  TRACE("setup done");
#if BOOT_TRACE
  digitalWrite(PIN_USER_LED, HIGH);  // OFF: setup() finished
#endif
  status::begin();

  // Watchdog: if the loop ever freezes for 2 s (e.g. stuck inside the Bluetooth
  // library), the chip restarts itself quietly; the BOOT line then says WATCHDOG.
  WatchdogTimer.begin(WDOG_PERIOD_2_S);
  if (!power::quietBoot()) haptics::play(haptics::kWake);  // no buzz after a Bluetooth restart
  lastMotionMs = millis();
  nextSampleUs = micros();
  comm::outf("BOOT %s %s wake=%d (%s)", name, FW_VERSION, power::wokeFromSleep(), power::wakeReason());
  comm::outf("IR timing: %s", ir::timingInfo());
}

void loop() {
  WatchdogTimer.feed();
  haptics::update();
  {
    static uint32_t lastBatCheck = 0;
    static bool lowBat = false;
    if (lastBatCheck == 0 || millis() - lastBatCheck > 30000) {
      lastBatCheck = millis() | 1;
      lowBat = power::batteryPercent() < LOW_BATTERY_PCT;
    }
    status::set(mode == kLearning                         ? status::kLearning
                : (mode == kRecording || mode == kCalibrating) ? status::kBusy
                : lowBat                                  ? status::kLowBattery
                : comm::bleConnected()                    ? status::kConnected
                                                          : status::kAdvertising);
    status::update();
  }
  power::where(1);  // loop: before comm::poll
  comm::poll();

  power::where(5);  // loop: readLine/handle
  if (char* line = comm::readLine()) handle(line);

  uint32_t now = millis();

  // Mode timeouts
  if (mode == kRecording && now > modeDeadline) {
    comm::outf("REC %d timeout", modeTarget);
    haptics::play(haptics::kError);
    status::flash(status::kError);
    mode = kNormal;
  }
  if (mode == kCalibrating && now > modeDeadline) {
    comm::out("CAL fail (not still)");
    haptics::play(haptics::kError);
    status::flash(status::kError);
    mode = kNormal;
  }
  power::where(6);  // loop: learn
  if (mode == kLearning && learnStep > -2) {
    if ((int32_t)(now - learnNextMs) >= 0) {
      if (learnStep > 0) {
        comm::outf("LEARN %d count %d", modeTarget, learnStep);
        haptics::pulse(60);
        learnNextMs = now + 1000;
        learnStep--;
      } else if (learnStep == 0) {
        comm::outf("LEARN %d go", modeTarget);
        haptics::pulse(250);
        learnNextMs = now + 300;  // buzz + 50 ms for the supply to settle
        learnStep = -1;
      } else if (!haptics::busy()) {
        haptics::off();
        ir::startLearn();
        comm::outf("LEARN %d waiting", modeTarget);
        learnStep = -2;
      }
    }
  } else if (mode == kLearning) {
    ir::LearnState s = ir::pollLearn(learnBuf);
    if (s == ir::kDone) {
      codes[modeTarget] = learnBuf;
      if (!storage::saveCode(modeTarget)) comm::out("ERR storage full or failed");
      {
        char h[IR_MAX_BYTES * 2 + 1];
        ir::hex(learnBuf, h, sizeof(h));
        comm::outf("LEARN %d ok %u %s %u %u", modeTarget, learnBuf.nbits, h, learnBuf.repeats, learnBuf.khz);
      }
#if IR_DEBUG
      printCodeShort(learnBuf);
#else
      describeCode(modeTarget);
#endif
      haptics::play(haptics::kSaved);
      status::flash(status::kSaved);
      mode = kNormal;
    } else if (s == ir::kTimeout || s == ir::kUnsupported) {
      comm::outf("LEARN %d %s", modeTarget, s == ir::kTimeout ? "timeout" : "unsupported");
      haptics::play(haptics::kError);
      status::flash(status::kError);
      mode = kNormal;
    }
  }

  power::where(7);  // loop: sampling
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

  power::where(9);  // loop: button
  pollButton();

  // Pending restart (rename): once the app has let go, or after 5 s anyway.
  if (restartAt && (!comm::bleConnected() || (int32_t)(millis() - restartAt) >= 0)) {
    comm::out("Restarting with the new name");
    delay(300);
    systemReset();
  }

  // Idle -> deep sleep, but never while the app is connected: then the wand
  // only sleeps when asked (SLEEP command / "Sleep now" in the app).
  static bool wasConnected = false;
  bool connected = comm::bleConnected();
  if (connected != wasConnected) lastMotionMs = millis();  // connect/disconnect = activity
  if (!connected && wasConnected) streaming = false;       // app gone: stop the motion stream
  wasConnected = connected;
  // Fresh millis() and signed math: lastMotionMs may have been set a moment
  // AFTER `now` (sampleOnce ran in between); unsigned "now - later" wraps to a
  // huge number and used to put the wand to sleep while it was moving.
  int32_t idleFor = (int32_t)(millis() - lastMotionMs);
  if (!connected && mode == kNormal && !haptics::busy() && idleFor > (int32_t)settings.idleSleepMs) goSleep("idle");

  power::where(10);  // loop: end (idle delay)
  // Let FreeRTOS idle the CPU until the next sample is due.
  int32_t waitUs = (int32_t)(nextSampleUs - micros());
  if (waitUs > 1500 && !haptics::busy()) delay(waitUs / 1000);
}
