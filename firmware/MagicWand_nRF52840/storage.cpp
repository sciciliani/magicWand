#include "storage.h"

#include <Adafruit_LittleFS.h>
#include <Arduino.h>
#include <InternalFileSystem.h>
#include <string.h>

using namespace Adafruit_LittleFS_Namespace;

Settings settings;
wand::GestureSlot gestures[wand::kMaxGestures];
IrCode codes[IR_MAX_CODES];

namespace storage {

static const uint32_t kMagic = 0x57414E44;  // "WAND"
static const uint16_t kVersion = 1;

static void defaults() {
  memset(&settings, 0, sizeof(settings));
  settings.magic = kMagic;
  settings.version = kVersion;
  settings.wakeThreshold = WAKE_THRESHOLD;
  settings.hapticsOn = 1;
  settings.axis[0] = WAND_AXIS_X;
  settings.axis[1] = WAND_AXIS_Y;
  settings.axis[2] = WAND_AXIS_Z;
  settings.threshold = GLOBAL_THRESHOLD;
  settings.idleSleepMs = IDLE_SLEEP_MS;
  memset(settings.bind, kNoBinding, sizeof(settings.bind));
}

static bool readFile(const char* path, void* buf, size_t len) {
  File f(InternalFS);
  if (!f.open(path, FILE_O_READ)) return false;
  size_t got = f.read((uint8_t*)buf, len);
  f.close();
  return got == len;
}

static bool writeFile(const char* path, const void* a, size_t alen, const void* b = nullptr,
                      size_t blen = 0) {
  InternalFS.remove(path);  // FILE_O_WRITE appends, so start fresh
  File f(InternalFS);
  if (!f.open(path, FILE_O_WRITE)) return false;
  f.write((const uint8_t*)a, alen);
  if (b && blen) f.write((const uint8_t*)b, blen);
  f.close();
  return true;
}

void begin() {
  InternalFS.begin();
  memset(gestures, 0, sizeof(gestures));
  memset(codes, 0, sizeof(codes));

  if (!readFile("/cfg", &settings, sizeof(settings)) || settings.magic != kMagic ||
      settings.version != kVersion) {
    defaults();
    saveSettings();
  }
  char path[8];
  for (int i = 0; i < wand::kMaxGestures; i++) {
    snprintf(path, sizeof(path), "/g%d", i);
    if (!readFile(path, &gestures[i], sizeof(wand::GestureSlot))) memset(&gestures[i], 0, sizeof(wand::GestureSlot));
  }
  const size_t hdr = offsetof(IrCode, d);
  for (int i = 0; i < IR_MAX_CODES; i++) {
    snprintf(path, sizeof(path), "/c%d", i);
    File f(InternalFS);
    if (!f.open(path, FILE_O_READ)) continue;
    if (f.read((uint8_t*)&codes[i], hdr) == hdr && codes[i].n <= IR_MAX_EDGES) {
      f.read((uint8_t*)codes[i].d, codes[i].n * sizeof(uint16_t));
    } else {
      memset(&codes[i], 0, sizeof(IrCode));
    }
    f.close();
  }
}

void saveSettings() { writeFile("/cfg", &settings, sizeof(settings)); }

void saveGesture(int i) {
  char path[8];
  snprintf(path, sizeof(path), "/g%d", i);
  if (gestures[i].count == 0) InternalFS.remove(path);
  else writeFile(path, &gestures[i], sizeof(wand::GestureSlot));
}

void saveCode(int i) {
  char path[8];
  snprintf(path, sizeof(path), "/c%d", i);
  if (codes[i].khz == 0) InternalFS.remove(path);
  else writeFile(path, &codes[i], offsetof(IrCode, d), codes[i].d, codes[i].n * sizeof(uint16_t));
}

void factoryReset() {
  InternalFS.format();
  defaults();
  memset(gestures, 0, sizeof(gestures));
  memset(codes, 0, sizeof(codes));
  saveSettings();
}

}  // namespace storage
