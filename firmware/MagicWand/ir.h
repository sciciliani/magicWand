// ir.h — infrared codes: learn from a remote, store compactly, send.
//
// A code is stored DECODED, not as raw pulses: the bit timings of its
// protocol plus the data bits (as hex, in the order they are sent) and how
// many times the frame is repeated. Example, a Midea/Coolix AC:
//   header 4692/4692 us, bit mark 552, "1" space 1656, "0" space 552,
//   48 bits B24D1FE048B7, sent 2x with a 5244 us gap.
// This covers NEC/LG, Samsung, Coolix/Midea and most single-frame ACs
// (pulse-distance) and Sony (pulse-width). Codes whose frames all differ
// (e.g. some 3-part Daikin frames) aren't supported: only the first frame
// and its identical repeats are kept.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "config.h"

constexpr int IR_MAX_BYTES = 32;   // up to 256 bits per frame
constexpr int IR_MAX_REPEATS = 4;

struct IrCode {
  char name[16];
  uint16_t khz;        // carrier, 0 = empty slot
  uint16_t hdrMark;    // 0 = no header
  uint16_t hdrSpace;
  uint16_t bitMark;    // pulse-distance: mark of every bit; pulse-width: space of every bit
  uint16_t one;        // pulse-distance: space of a "1"; pulse-width: mark of a "1"
  uint16_t zero;       // same for "0"
  uint16_t gap;        // space between repeated frames
  uint8_t pw;          // 0 = pulse-distance (NEC, Coolix...), 1 = pulse-width (Sony)
  uint8_t nbits;       // data bits per frame (1..255)
  uint8_t repeats;     // frames sent per press (1..IR_MAX_REPEATS)
  uint8_t pad;
  uint8_t data[IR_MAX_BYTES];  // bits MSB-first, in the order they are sent
};

namespace ir {

void begin();
void send(const IrCode& code);   // blocking; at least IR_HOLD_MS (like a held button)
const char* timingInfo();        // IR timing method (for the BOOT line)

// Raw pulses (µs: mark, space, mark, ...) <-> decoded code.
bool decode(const uint16_t* d, int n, uint16_t khz, IrCode& out);  // keeps out.name
int rawLength(const IrCode& c);                                    // number of durations
int toRaw(const IrCode& c, uint16_t* out, int max);                // returns count
void hex(const IrCode& c, char* out, size_t cap);                  // "B24D1FE048B7"
bool fromHex(const char* hex, IrCode& c);                          // sets data + nbits
// Protocol presets for the HEX command: nec, samsung, coolix, sony
bool setProtocol(const char* name, IrCode& c);

// Learning (non-blocking). startLearn() powers the receiver and waits for a
// button press; poll pollLearn() from loop().
enum LearnState { kIdle, kWaiting, kCapturing, kDone, kTimeout, kUnsupported };
void startLearn();
LearnState pollLearn(IrCode& out);  // fills `out` (keeps its name) when kDone
void stopLearn();                   // also powers the receiver down

}  // namespace ir
