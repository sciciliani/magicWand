#include "comm.h"

#include <Arduino.h>
#include <bluefruit.h>
#include <stdarg.h>

namespace comm {

static BLEUart bleuart;
static const size_t kLineMax = 4096;  // a long AC code upload is ~3.6 KB
static char bleLine[kLineMax];
static size_t bleLen = 0;
static char serLine[kLineMax];
static size_t serLen = 0;
static bool bleOverflow = false, serOverflow = false;

static void onConnect(uint16_t conn) {
  BLEConnection* c = Bluefruit.Connection(conn);
  c->requestPHY();
  c->requestDataLengthUpdate();
  c->requestMtuExchange(247);
  bleLen = 0;
}

static void onDisconnect(uint16_t, uint8_t) { bleLen = 0; }

void begin(const char* bleName) {
  Serial.begin(115200);

  Bluefruit.autoConnLed(false);  // the blinking blue LED costs power
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setTxPower(0);
  Bluefruit.setName(bleName);
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  bleuart.bufferTXD(true);
  bleuart.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 1600);  // 100 ms fast, 1 s slow (x0.625 ms)
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

// Accumulate bytes from a stream into a line buffer.
static char* feed(Stream& s, char* buf, size_t& len, bool& overflow) {
  while (s.available()) {
    int c = s.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = 0;
      bool bad = overflow;
      len = 0;
      overflow = false;
      if (bad) {
        out("ERR line too long");
        continue;
      }
      return buf;
    }
    if (len < kLineMax - 1) buf[len++] = (char)c;
    else overflow = true;
  }
  return nullptr;
}

char* poll() {
  char* l = feed(bleuart, bleLine, bleLen, bleOverflow);
  if (l) return l;
  return feed(Serial, serLine, serLen, serOverflow);
}

void out(const char* line) {
  if (Serial) {
    Serial.print(line);
    Serial.print('\n');
  }
  if (Bluefruit.connected() && bleuart.notifyEnabled()) {
    bleuart.write((const uint8_t*)line, strlen(line));
    bleuart.write((const uint8_t*)"\n", 1);
  }
}

void outf(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  out(buf);
}

bool bleConnected() { return Bluefruit.connected(); }
bool bleNotifying() { return Bluefruit.connected() && bleuart.notifyEnabled(); }

}  // namespace comm
