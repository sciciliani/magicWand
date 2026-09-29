// storage.h — everything the wand remembers, persisted in the MG24's flash.
// All data lives in RAM; save*() persists one item.
//
// Backend (config.h STORAGE_NVM3):
//   1 = NVM3 key/value store (default): one object per item, chunked
//   0 = EEPROM library: everything packed into one blob
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
  uint8_t bind[wand::kMaxGestures];
  char name[12];  // custom wand name ("" = Wand-XXXX); advertised as "Wand-<name>"
};

extern Settings settings;
extern wand::GestureSlot gestures[wand::kMaxGestures];
extern IrCode codes[IR_MAX_CODES];

namespace storage {

void begin();
bool saveSettings();
bool saveGesture(int i);
bool saveCode(int i);
void factoryReset();
// Print everything as a ready-to-paste default_spells.h (one line per call).
void exportDefaults(void (*emit)(const char*));
const char* backendName();
// Bytes the saved data needs / bytes available (0 = unknown, NVM3).
size_t usedBytes();
size_t capacityBytes();
uint32_t lastError();  // 0 = ok

}  // namespace storage
