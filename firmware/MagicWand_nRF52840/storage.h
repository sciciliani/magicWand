// storage.h — everything the wand remembers, persisted to internal flash
// (LittleFS, ~28 KB on the XIAO). All data lives in RAM; save*() writes one
// item so flash wear stays low.
//
//   /cfg      Settings (axis, thresholds, sleep, bindings)
//   /g<i>     GestureSlot i (trained samples)
//   /c<i>     IrCode i (header + only the used durations)
#pragma once
#include "config.h"
#include "gesture.h"
#include "ir.h"

constexpr uint8_t kNoBinding = 0xFF;

struct Settings {
  uint32_t magic;
  uint16_t version;
  uint8_t wakeThreshold;
  uint8_t hapticsOn;
  float axis[3];
  float threshold;
  uint32_t idleSleepMs;
  uint8_t bind[wand::kMaxGestures];  // gesture -> IR code index (kNoBinding = none)
};

extern Settings settings;
extern wand::GestureSlot gestures[wand::kMaxGestures];
extern IrCode codes[IR_MAX_CODES];

namespace storage {

void begin();  // mounts the file system and loads everything (defaults if empty)
void saveSettings();
void saveGesture(int i);
void saveCode(int i);
void factoryReset();  // formats flash and restores defaults

}  // namespace storage
