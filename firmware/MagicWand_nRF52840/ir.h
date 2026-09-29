// ir.h — raw infrared send (38 kHz carrier via PWM + EasyDMA, jitter-free
// even while BLE is busy) and learning from an existing remote (TSOP receiver).
//
// A code is stored "raw": a list of durations in microseconds, alternating
// mark (LED blinking at the carrier) / space (LED off), starting with a mark.
// Any protocol (NEC, Samsung, Sony, air-conditioner blobs...) fits this format;
// the web app converts TV presets to raw before uploading them.
#pragma once
#include <stdint.h>

#include "config.h"

struct IrCode {
  char name[16];
  uint16_t khz;  // carrier, 0 = empty slot
  uint16_t n;    // number of durations
  uint16_t d[IR_MAX_EDGES];
};

namespace ir {

void begin();
// Blocking; a TV code takes ~70 ms, a long AC code up to ~500 ms.
void send(const IrCode& code);

// Learning (non-blocking). startLearn() powers the receiver and waits for a
// button press; poll pollLearn() from loop().
enum LearnState { kIdle, kWaiting, kCapturing, kDone, kTimeout, kOverflow };
void startLearn();
LearnState pollLearn(IrCode& out);  // fills `out` (keeps its name) when kDone
void stopLearn();                   // also powers the receiver down

}  // namespace ir
