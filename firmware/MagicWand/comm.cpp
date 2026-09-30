#include "comm.h"

#include <Arduino.h>
#include <ArduinoBLE.h>  // needs Tools > Protocol stack > BLE (Arduino)
#include <stdarg.h>
#include <string.h>

#include "config.h"

namespace comm {

// Nordic UART Service — same UUIDs the web app looks for.
static BLEService nus("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static BLECharacteristic rxChar("6E400002-B5A3-F393-E0A9-E50E24DCCA9E", BLEWrite | BLEWriteWithoutResponse, 244);
static BLECharacteristic txChar("6E400003-B5A3-F393-E0A9-E50E24DCCA9E", BLENotify, 244);

static const size_t kLineMax = 4096;
static const size_t kRxRing = 4096;
static char rxRing[kRxRing];  // bytes written by the app, drained by readLine()
static volatile size_t rxHead = 0, rxTail = 0;

static char bleLine[kLineMax];
static size_t bleLen = 0;
static char serLine[kLineMax];
static size_t serLen = 0;
static bool bleOverflow = false, serOverflow = false;
static bool bleOk = false;
static volatile bool linkUp = false;  // tracked from connect/disconnect events

// Copy whatever the app just wrote into the RX ring.
static void takeWrite() {
  const uint8_t* v = rxChar.value();
  int n = rxChar.valueLength();
  for (int i = 0; i < n; i++) {
    size_t next = (rxHead + 1) % kRxRing;
    if (next == rxTail) break;  // full: drop
    rxRing[rxHead] = (char)v[i];
    rxHead = next;
  }
}

static volatile bool evConnect = false, evDisconnect = false;
static void onConnect(BLEDevice) { linkUp = true; evConnect = true; }

static void onDisconnect(BLEDevice) {
  linkUp = false;
  evDisconnect = true;
  bleLen = 0;
  rxHead = rxTail = 0;
}

static uint32_t lastAdvCheck = 0;

void begin(const char* bleName) {
  // (Serial.begin() already ran at the top of setup().)
  if (!BLE.begin()) {
    out("ERR BLE.begin failed: select Tools > Protocol stack > BLE (Arduino)");
    return;
  }
  bleOk = true;
  BLE.setLocalName(bleName);
  BLE.setDeviceName(bleName);
  nus.addCharacteristic(rxChar);
  nus.addCharacteristic(txChar);
  BLE.addService(nus);
  BLE.setEventHandler(BLEConnected, onConnect);
  BLE.setEventHandler(BLEDisconnected, onDisconnect);
  // The 128-bit service UUID + name don't both fit in one advertisement;
  // the app finds the wand by name ("Wand-…") and then opens the service.
  BLE.setAdvertisingInterval(320);  // 200 ms (units of 0.625 ms)
  BLE.advertise();
}

// Serial-only debug line (never sent to the app).
static void dbg(const char* a, const char* b = "") {
#if USE_SERIAL && BLE_DEBUG
  if (Serial) { Serial.print(a); Serial.print(b); Serial.print('\n'); }
#else
  (void)a; (void)b;
#endif
}

void poll() {
  if (!bleOk) return;
  BLE.poll();
  static bool wasSub = false;
  if (evConnect) { evConnect = false; dbg("BLE: app connected"); }
  if (evDisconnect) { evDisconnect = false; wasSub = false; dbg("BLE: app disconnected, advertising again"); }
  bool sub = linkUp && txChar.subscribed();
  if (sub != wasSub) { wasSub = sub; dbg(sub ? "BLE: app listening (notifications on)" : "BLE: app stopped listening"); }
  // Polled instead of an event handler: works the same on every ArduinoBLE port.
  if (rxChar.written()) takeWrite();
  // Safety net: after a disconnect (or a USB plug/unplug glitch) advertising
  // doesn't always restart by itself. Kick it every 5 s while nobody is connected.
  if (millis() - lastAdvCheck > 5000) {
    lastAdvCheck = millis();
    if (!BLE.connected()) {
      linkUp = false;
      BLE.advertise();
    }
  }
}

static char* feedChar(char c, char* buf, size_t& len, bool& overflow) {
  if (c == '\r') return nullptr;
  if (c == '\n') {
    buf[len] = 0;
    bool bad = overflow;
    len = 0;
    overflow = false;
    if (bad) {
      out("ERR line too long");
      return nullptr;
    }
    return buf;
  }
  if (len < kLineMax - 1) buf[len++] = c;
  else overflow = true;
  return nullptr;
}

char* readLine() {
  while (rxTail != rxHead) {
    char c = rxRing[rxTail];
    rxTail = (rxTail + 1) % kRxRing;
    if (char* l = feedChar(c, bleLine, bleLen, bleOverflow)) {
      char head[48];
      snprintf(head, sizeof(head), "%.40s%s", l, strlen(l) > 40 ? "..." : "");
      dbg("BLE> ", head);
      return l;
    }
  }
#if USE_SERIAL
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    if (char* l = feedChar((char)c, serLine, serLen, serOverflow)) return l;
  }
#endif
  return nullptr;
}

static void notify(const char* p, size_t n) {
  while (n > 0) {
    size_t k = n < BLE_NOTIFY_CHUNK ? n : BLE_NOTIFY_CHUNK;
    // The stack has only a few TX buffers: if one isn't free, let it send.
    for (int tries = 0; tries < 50 && !txChar.writeValue((const uint8_t*)p, k); tries++) {
      BLE.poll();
      delay(2);
    }
    p += k;
    n -= k;
  }
}

void out(const char* line) {
#if USE_SERIAL
  if (Serial) {
    Serial.print(line);
    Serial.print('\n');
  }
#endif
  if (bleNotifying()) {
    notify(line, strlen(line));
    notify("\n", 1);
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

bool bleConnected() { return bleOk && (linkUp || BLE.connected()); }
bool bleNotifying() { return bleConnected() && txChar.subscribed(); }

void end() {
  if (bleOk) {
    if (BLE.connected()) BLE.disconnect();
    BLE.stopAdvertise();
    BLE.end();
  }
  Serial.end();  // an open Serial can stop the MG24 waking from EM4 (Seeed forum)
}

}  // namespace comm
