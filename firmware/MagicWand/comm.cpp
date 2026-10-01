// comm.cpp — Nordic UART Service on the native Silicon Labs Bluetooth stack
// (Tools > Protocol stack > BLE (Silabs)) plus USB Serial.
//
// Why not ArduinoBLE: on this board its writeValue() never returned when the
// app went away mid-send (the loop froze in notify()), and GATT discovery hung
// on the second connection. The native stack is what ArduinoBLE sits on top of
// anyway; here we talk to it directly:
//   - sl_bt_on_event() runs in the stack's own RTOS task. It only updates
//     variables and restarts advertising; no Serial, no waiting.
//   - Sending is sl_bt_gatt_server_send_notification(): it queues and returns
//     at once, with an error if the link is gone or the queue is full. It can't
//     hang the loop.
//   - A disconnect clears every piece of link state, then advertising restarts
//     from the event itself (so it happens even if loop() is busy).
#ifndef ARDUINO_SILABS_STACK_BLE_SILABS
#error "Select Tools > Protocol stack > BLE (Silabs). This firmware uses the native Silicon Labs Bluetooth stack."
#endif

#include "comm.h"

#include <Arduino.h>
#include <sl_bluetooth.h>
#include <stdarg.h>
#include <string.h>

#include "config.h"
#include "power.h"

// 1 = restart the whole chip after every disconnect (the old ArduinoBLE
// workaround). The native stack shouldn't need it; flip it if reconnects fail.
#ifndef BLE_REBOOT_ON_DISCONNECT
#define BLE_REBOOT_ON_DISCONNECT 0
#endif

namespace comm {

// Nordic UART Service, UUIDs in little-endian byte order (as the stack wants).
// 6E400001-B5A3-F393-E0A9-E50E24DCCA9E  service
// 6E400002-...                          RX: the app writes here
// 6E400003-...                          TX: we notify here
static const uuid_128 kNusService = {{0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                                      0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E}};
static const uuid_128 kNusRx = {{0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                                 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E}};
static const uuid_128 kNusTx = {{0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                                 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E}};
static const uint16_t kMaxPayload = 244;  // biggest notification we send (MTU 247 - 3)

static const size_t kLineMax = 4096;
static const size_t kRxRing = 4096;

// ---- written by the Bluetooth event task, read by loop() -----------------
static char rxRing[kRxRing];                 // bytes the app wrote (event task -> loop)
static volatile size_t rxHead = 0;           // only the event task moves this
static volatile size_t rxTail = 0;           // only loop() moves this
static volatile size_t rxCloseMark = 0;      // rxHead when the last link closed
static volatile bool booted = false;         // stack is up (system_boot event)
static volatile bool linkUp = false;
static volatile bool subscribed = false;     // app turned notifications on
static volatile uint8_t conn = SL_BT_INVALID_CONNECTION_HANDLE;
static volatile uint16_t payload = 20;       // notification size for this link
static volatile uint16_t closeReason = 0;
static volatile uint32_t opens = 0, closes = 0;
static volatile bool advFailed = false;      // restart advertising failed: poll() retries
static volatile bool stopping = false;       // end() ran: don't advertise again

// ---- set once at start-up ------------------------------------------------
static uint16_t rxHandle = 0, txHandle = 0;
static uint8_t advSet = 0xFF;
static bool started = false;  // GATT database + advertising are set up
static char advName[24] = "Wand";

// ---- loop() only -----------------------------------------------------------
static char bleLine[kLineMax];
static size_t bleLen = 0;
static char serLine[kLineMax];
static size_t serLen = 0;
static bool bleOverflow = false, serOverflow = false;
static char txBuf[kLineMax + 1];

// Serial-only debug line (never sent to the app). Only called from loop().
static void dbg(const char* a, const char* b = "") {
#if USE_SERIAL && BLE_DEBUG
  if (Serial) { Serial.print(a); Serial.print(b); Serial.print('\n'); }
#else
  (void)a; (void)b;
#endif
}

static sl_status_t startAdvertising() {
  sl_status_t sc = sl_bt_legacy_advertiser_generate_data(advSet, sl_bt_advertiser_general_discoverable);
  if (sc == SL_STATUS_OK) sc = sl_bt_legacy_advertiser_start(advSet, sl_bt_advertiser_connectable_scannable);
  return sc;
}

// Build the GATT database (Generic Access with our name + NUS) and advertise.
// Runs from poll() once the stack has booted and begin() has given us the name.
static void setUp() {
  static uint32_t retryAt = 0;
  if (started || !booted || millis() < retryAt) return;
  retryAt = millis() + 2000;
  const char* step = "gattdb session";
  uint16_t session = 0, gap = 0, nameChar = 0, nus = 0;
  sl_status_t sc = sl_bt_gattdb_new_session(&session);
  if (sc == SL_STATUS_OK) {
    step = "generic access";
    const uint8_t gapUuid[] = {0x00, 0x18};
    sc = sl_bt_gattdb_add_service(session, sl_bt_gattdb_primary_service, SL_BT_GATTDB_ADVERTISED_SERVICE,
                                  sizeof(gapUuid), gapUuid, &gap);
  }
  if (sc == SL_STATUS_OK) {
    step = "device name";
    const sl_bt_uuid_16_t nameUuid = {{0x00, 0x2A}};
    uint16_t len = (uint16_t)strlen(advName);
    sc = sl_bt_gattdb_add_uuid16_characteristic(session, gap, SL_BT_GATTDB_CHARACTERISTIC_READ, 0, 0, nameUuid,
                                                sl_bt_gattdb_fixed_length_value, len, len,
                                                (const uint8_t*)advName, &nameChar);
  }
  if (sc == SL_STATUS_OK) sc = sl_bt_gattdb_start_service(session, gap);
  if (sc == SL_STATUS_OK) {
    step = "uart service";
    // Not flagged "advertised": a 128-bit UUID plus the name don't fit in one
    // advertisement. The app finds the wand by name ("Wand-…").
    sc = sl_bt_gattdb_add_service(session, sl_bt_gattdb_primary_service, 0, sizeof(kNusService.data),
                                  kNusService.data, &nus);
  }
  const uint8_t zero = 0;
  if (sc == SL_STATUS_OK) {
    step = "rx characteristic";
    sc = sl_bt_gattdb_add_uuid128_characteristic(
        session, nus, SL_BT_GATTDB_CHARACTERISTIC_WRITE | SL_BT_GATTDB_CHARACTERISTIC_WRITE_NO_RESPONSE, 0, 0,
        kNusRx, sl_bt_gattdb_variable_length_value, kMaxPayload, 1, &zero, &rxHandle);
  }
  if (sc == SL_STATUS_OK) {
    step = "tx characteristic";
    sc = sl_bt_gattdb_add_uuid128_characteristic(session, nus, SL_BT_GATTDB_CHARACTERISTIC_NOTIFY, 0, 0, kNusTx,
                                                 sl_bt_gattdb_variable_length_value, kMaxPayload, 1, &zero,
                                                 &txHandle);
  }
  if (sc == SL_STATUS_OK) sc = sl_bt_gattdb_start_service(session, nus);
  if (sc == SL_STATUS_OK) { step = "gattdb commit"; sc = sl_bt_gattdb_commit(session); }
  if (sc != SL_STATUS_OK) {
    if (session) sl_bt_gattdb_abort(session);
    outf("ERR BLE setup failed at %s (0x%04lx), retrying", step, (unsigned long)sc);
    return;
  }

  uint16_t mtuOut = 0;
  sl_bt_gatt_server_set_max_mtu(kMaxPayload + 3, &mtuOut);  // best effort

  sc = sl_bt_advertiser_create_set(&advSet);
  if (sc == SL_STATUS_OK) sc = sl_bt_advertiser_set_timing(advSet, 320, 320, 0, 0);  // 200 ms
  if (sc == SL_STATUS_OK) sc = startAdvertising();
  if (sc != SL_STATUS_OK) {
    outf("ERR BLE advertising failed (0x%04lx), retrying", (unsigned long)sc);
    advFailed = true;  // GATT is in place; poll() only retries advertising
  }
  started = true;
  dbg("BLE: advertising as ", advName);
}

void begin(const char* bleName) {
  // (Serial.begin() already ran at the top of setup().)
  strncpy(advName, bleName, sizeof(advName) - 1);
  setUp();  // may be too early (stack not booted yet): poll() finishes the job
}

void poll() {
  power::where(2);  // comm::poll start
  if (!started) {
    setUp();
    return;
  }
  power::where(4);  // comm::poll events
  static uint32_t seenOpens = 0, seenCloses = 0;
  static bool wasSub = false;
  static uint32_t advRetryAt = 0;

  if (closes != seenCloses) {
    seenCloses = closes;
    // Everything from the old link goes: unread bytes (up to where the link
    // closed) and any half-received line. Bytes after the mark belong to a
    // new connection and are kept.
    size_t mark = rxCloseMark, tail = rxTail, head = rxHead;
    if ((mark - tail + kRxRing) % kRxRing <= (head - tail + kRxRing) % kRxRing) rxTail = mark;
    bleLen = 0;
    bleOverflow = false;
    wasSub = false;
#if USE_SERIAL && BLE_DEBUG
    char r[40];
    snprintf(r, sizeof(r), " (reason 0x%04x)", (unsigned)closeReason);
    dbg("BLE: app disconnected", r);
#endif
#if BLE_REBOOT_ON_DISCONNECT
    power::quietRestart();
#endif
  }
  if (opens != seenOpens) {
    seenOpens = opens;
    dbg("BLE: app connected");
  }
  bool sub = linkUp && subscribed;
  if (sub != wasSub) {
    wasSub = sub;
    dbg(sub ? "BLE: app listening (notifications on)" : "BLE: app stopped listening");
  }
  if (advFailed && !linkUp && !stopping && millis() >= advRetryAt) {
    advRetryAt = millis() + 1000;
    if (startAdvertising() == SL_STATUS_OK) {
      advFailed = false;
      dbg("BLE: advertising again");
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

// Queue notifications. Never waits for the other side: if the queue is full we
// retry for at most ~20 ms, if the link is gone we stop at once.
static void notify(const char* p, size_t n) {
  power::where(8);  // notify(): sending to the app
  while (n > 0) {
    uint8_t c = conn;
    if (!linkUp || c == SL_BT_INVALID_CONNECTION_HANDLE) return;
    size_t k = payload;
    if (k > n) k = n;
    int tries = 0;
    sl_status_t sc;
    while ((sc = sl_bt_gatt_server_send_notification(c, txHandle, k, (const uint8_t*)p)) != SL_STATUS_OK) {
      if (sc != SL_STATUS_NO_MORE_RESOURCE || !linkUp || ++tries > 20) return;  // drop the rest
      delay(1);
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
  if (!bleNotifying()) return;
  size_t n = strlen(line);
  if (n < kLineMax) {  // line + '\n' in one go: fewer notifications
    memcpy(txBuf, line, n);
    txBuf[n] = '\n';
    notify(txBuf, n + 1);
  } else {
    notify(line, n);
    notify("\n", 1);
  }
}

void serialOut(const char* line) {
#if USE_SERIAL
  if (Serial) {
    Serial.print(line);
    Serial.print('\n');
  }
#else
  (void)line;
#endif
}

void outf(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  out(buf);
}

bool bleConnected() { return started && linkUp; }
bool bleNotifying() { return bleConnected() && subscribed; }

void end() {
  stopping = true;
  if (started) {
    uint8_t c = conn;
    if (c != SL_BT_INVALID_CONNECTION_HANDLE) sl_bt_connection_close(c);
    sl_bt_advertiser_stop(advSet);
    delay(100);  // let the disconnect reach the app before the radio goes off
  }
  Serial.end();  // an open Serial can stop the MG24 waking from EM4 (Seeed forum)
}

// ---- event task side -----------------------------------------------------
static void onEvent(sl_bt_msg_t* evt) {
  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id:
      booted = true;
      break;

    case sl_bt_evt_connection_opened_id:
      conn = evt->data.evt_connection_opened.connection;
      payload = 20;
      subscribed = false;
      linkUp = true;
      opens = opens + 1;
      break;

    case sl_bt_evt_connection_closed_id:
      if (evt->data.evt_connection_closed.connection != conn) break;
      linkUp = false;
      subscribed = false;
      conn = SL_BT_INVALID_CONNECTION_HANDLE;
      payload = 20;
      closeReason = evt->data.evt_connection_closed.reason;
      rxCloseMark = rxHead;
      closes = closes + 1;
      if (!stopping && startAdvertising() != SL_STATUS_OK) advFailed = true;
      break;

    case sl_bt_evt_gatt_mtu_exchanged_id: {
      uint16_t mtu = evt->data.evt_gatt_mtu_exchanged.mtu;
      uint16_t p = mtu > 3 ? mtu - 3 : 20;
      payload = p > kMaxPayload ? kMaxPayload : p;
      break;
    }

    case sl_bt_evt_gatt_server_characteristic_status_id: {
      const sl_bt_evt_gatt_server_characteristic_status_t& s = evt->data.evt_gatt_server_characteristic_status;
      if (s.characteristic == txHandle && s.status_flags == sl_bt_gatt_server_client_config)
        subscribed = (s.client_config_flags & sl_bt_gatt_server_notification) != 0;
      break;
    }

    case sl_bt_evt_gatt_server_attribute_value_id: {
      const sl_bt_evt_gatt_server_attribute_value_t& a = evt->data.evt_gatt_server_attribute_value;
      if (a.attribute != rxHandle || a.connection != conn) break;
      size_t h = rxHead;
      for (uint8_t i = 0; i < a.value.len; i++) {
        size_t next = (h + 1) % kRxRing;
        if (next == rxTail) break;  // full: drop
        rxRing[h] = (char)a.value.data[i];
        h = next;
      }
      __sync_synchronize();  // bytes land before loop() sees the new head
      rxHead = h;
      break;
    }

    default:
      break;
  }
}

}  // namespace comm

// Called by the Silicon Labs Bluetooth stack for every event (its own task).
void sl_bt_on_event(sl_bt_msg_t* evt) { comm::onEvent(evt); }
