// comm.h — text-line transport over BLE (Nordic UART Service, on the native
// Silicon Labs stack: Tools > Protocol stack > BLE (Silabs)) and USB Serial.
// Both carry the same protocol (docs/PROTOCOL.md).
#pragma once
#include <stddef.h>
#include <stdio.h>

// Float -> text without printf("%f").  outf("%s", FX(v).s)  /  FX(v, 3).s
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
void poll();       // call every loop (runs the BLE stack)
char* readLine();  // complete incoming line or nullptr; valid until next call
void out(const char* line);
void outf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void serialOut(const char* line);  // USB Serial only (debug dumps), never to the app
bool bleConnected();
bool bleNotifying();
void end();        // before deep sleep

}  // namespace comm
