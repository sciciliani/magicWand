// comm.h — text-line transport over BLE (Nordic UART Service) and USB Serial.
// Both carry the same protocol (docs/PROTOCOL.md), so you can drive the wand
// from the web app or from a serial monitor in VS Code.
#pragma once
#include <stddef.h>
#include <stdio.h>

// Float -> text without relying on printf("%f") (newlib-nano may not link
// float support). Use as: outf("%s", FX(v).s)  or  FX(v, 3).s for 3 decimals.
struct FX {
  char s[16];
  explicit FX(float v, int dec = 2) {
    char* p = s;
    if (v < 0) { *p++ = '-'; v = -v; }
    long scale = dec >= 3 ? 1000 : (dec == 2 ? 100 : 10);
    long x = (long)(v * scale + 0.5f);
    int w = dec >= 3 ? 3 : (dec == 2 ? 2 : 1);
    snprintf(p, sizeof(s) - (p - s), "%ld.%0*ld", x / scale, w, x % scale);
  }
};

namespace comm {

void begin(const char* bleName);
// Returns a complete incoming line (without '\n') or nullptr. The buffer is
// valid until the next call.
char* poll();
void out(const char* line);                     // sends line + '\n' to both
void outf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
bool bleConnected();
bool bleNotifying();  // app subscribed: OK to stream

}  // namespace comm
