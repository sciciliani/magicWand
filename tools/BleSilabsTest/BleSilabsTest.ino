// BleSilabsTest.ino — the wand's Bluetooth link on the native Silicon Labs
// stack, and nothing else (no IMU, no IR, no sleep).
//
//   Tools > Board > Seeed Studio XIAO MG24 (Sense)
//   Tools > Protocol stack > BLE (Silabs)          <-- required
//   Serial Monitor 115200
//
// Then open http://localhost:8000/bletest.html (make app), connect to
// "Wand-TEST" and try:
//   PING -> PONG, HELLO -> INFO
//   STREAM 1 -> 50 lines/s, like the Live tab. Disconnect WHILE it streams:
//   that's what froze the ArduinoBLE version. The loop must keep printing
//   "alive" and the next connection must work. Repeat 10+ times.
#ifndef ARDUINO_SILABS_STACK_BLE_SILABS
#error "Select Tools > Protocol stack > BLE (Silabs)"
#endif

static const char kName[] = "Wand-TEST";

// NUS UUIDs, little-endian.
static const uuid_128 kSvc = {{0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E}};
static const uuid_128 kRx = {{0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E}};
static const uuid_128 kTx = {{0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E}};

static uint16_t rxHandle, txHandle;
static uint8_t advSet = 0xFF;

// Shared with the Bluetooth event task (flags and counters only).
static volatile bool booted = false, linkUp = false, subscribed = false;
static volatile uint8_t conn = SL_BT_INVALID_CONNECTION_HANDLE;
static volatile uint16_t payload = 20, mtu = 23, closeReason = 0;
static volatile uint32_t opens = 0, closes = 0, notifyFails = 0;
static char ring[1024];
static volatile size_t head = 0, tail = 0;

static bool streaming = false;

static sl_status_t advertise() {
  sl_status_t sc = sl_bt_legacy_advertiser_generate_data(advSet, sl_bt_advertiser_general_discoverable);
  if (!sc) sc = sl_bt_legacy_advertiser_start(advSet, sl_bt_advertiser_connectable_scannable);
  return sc;
}

static void check(sl_status_t sc, const char* what) {
  if (sc == SL_STATUS_OK) return;
  Serial.printf("FAILED %s: 0x%04lx\n", what, (unsigned long)sc);
}

static void setUpGatt() {
  uint16_t s, gap, nameChar, svc;
  const uint8_t zero = 0;
  const uint8_t gapUuid[] = {0x00, 0x18};
  const sl_bt_uuid_16_t nameUuid = {{0x00, 0x2A}};
  check(sl_bt_gattdb_new_session(&s), "new_session");
  check(sl_bt_gattdb_add_service(s, sl_bt_gattdb_primary_service, SL_BT_GATTDB_ADVERTISED_SERVICE, 2, gapUuid, &gap), "gap service");
  check(sl_bt_gattdb_add_uuid16_characteristic(s, gap, SL_BT_GATTDB_CHARACTERISTIC_READ, 0, 0, nameUuid,
        sl_bt_gattdb_fixed_length_value, sizeof(kName) - 1, sizeof(kName) - 1, (const uint8_t*)kName, &nameChar), "name");
  check(sl_bt_gattdb_start_service(s, gap), "start gap");
  check(sl_bt_gattdb_add_service(s, sl_bt_gattdb_primary_service, 0, 16, kSvc.data, &svc), "nus service");
  check(sl_bt_gattdb_add_uuid128_characteristic(s, svc, SL_BT_GATTDB_CHARACTERISTIC_WRITE | SL_BT_GATTDB_CHARACTERISTIC_WRITE_NO_RESPONSE,
        0, 0, kRx, sl_bt_gattdb_variable_length_value, 244, 1, &zero, &rxHandle), "rx");
  check(sl_bt_gattdb_add_uuid128_characteristic(s, svc, SL_BT_GATTDB_CHARACTERISTIC_NOTIFY,
        0, 0, kTx, sl_bt_gattdb_variable_length_value, 244, 1, &zero, &txHandle), "tx");
  check(sl_bt_gattdb_start_service(s, svc), "start nus");
  check(sl_bt_gattdb_commit(s), "commit");
  uint16_t m;
  sl_bt_gatt_server_set_max_mtu(247, &m);
  check(sl_bt_advertiser_create_set(&advSet), "adv set");
  check(sl_bt_advertiser_set_timing(advSet, 320, 320, 0, 0), "adv timing");
  check(advertise(), "advertise");
  Serial.printf("Advertising as %s\n", kName);
}

// Never blocks: queue full for > 20 ms or link gone -> the line is dropped.
static void sendLine(const char* s) {
  char buf[256];
  int n = snprintf(buf, sizeof(buf), "%s\n", s);
  if (!streaming) Serial.printf("TX> %s", buf);
  const char* p = buf;
  while (n > 0) {
    uint8_t c = conn;
    if (!linkUp || !subscribed || c == SL_BT_INVALID_CONNECTION_HANDLE) return;
    int k = n < payload ? n : payload;
    int tries = 0;
    sl_status_t sc;
    while ((sc = sl_bt_gatt_server_send_notification(c, txHandle, k, (const uint8_t*)p)) != SL_STATUS_OK) {
      if (sc != SL_STATUS_NO_MORE_RESOURCE || !linkUp || ++tries > 20) { notifyFails++; return; }
      delay(1);
    }
    p += k;
    n -= k;
  }
}

static void handle(const char* line) {
  Serial.printf("RX> %s\n", line);
  if (!strcmp(line, "PING")) sendLine("PONG");
  else if (!strcmp(line, "HELLO")) sendLine("INFO {\"name\":\"Wand-TEST\"}");
  else if (!strcmp(line, "STREAM 1")) { streaming = true; sendLine("OK STREAM 1"); }
  else if (!strcmp(line, "STREAM 0")) { streaming = false; sendLine("OK STREAM 0"); }
  else sendLine("ERR unknown");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== BleSilabsTest (native Silicon Labs stack) ===");
}

void loop() {
  static bool gattDone = false;
  static uint32_t seenOpens = 0, seenCloses = 0, lastAlive = 0, lastSample = 0, sampleNo = 0;
  static bool wasSub = false;
  static char line[256];
  static size_t len = 0;

  if (booted && !gattDone) { gattDone = true; setUpGatt(); }

  if (opens != seenOpens) { seenOpens = opens; Serial.printf("BLE: connected (#%lu)\n", (unsigned long)opens); }
  if (closes != seenCloses) {
    seenCloses = closes;
    streaming = false;
    len = 0;
    Serial.printf("BLE: disconnected, reason 0x%04x, advertising again (notify drops so far: %lu)\n",
                  (unsigned)closeReason, (unsigned long)notifyFails);
  }
  bool sub = linkUp && subscribed;
  if (sub != wasSub) { wasSub = sub; Serial.printf("BLE: notifications %s (MTU %u)\n", sub ? "ON" : "off", (unsigned)mtu); }

  while (tail != head) {
    char c = ring[tail];
    tail = (tail + 1) % sizeof(ring);
    if (c == '\n') { line[len] = 0; len = 0; handle(line); }
    else if (c != '\r' && len < sizeof(line) - 1) line[len++] = c;
  }

  if (streaming && millis() - lastSample >= 20) {  // 50 lines/s, like the Live tab
    lastSample = millis();
    char f[96];
    snprintf(f, sizeof(f), "F %lu 0.12 -0.98 0.05 1.5 -2.25 0.75", (unsigned long)++sampleNo);
    sendLine(f);
  }

  if (millis() - lastAlive >= 5000) {  // proves loop() never froze
    lastAlive = millis();
    Serial.printf("alive %lus, link %s, opens %lu, closes %lu, drops %lu\n", (unsigned long)(millis() / 1000),
                  linkUp ? "up" : "down", (unsigned long)opens, (unsigned long)closes, (unsigned long)notifyFails);
  }
  delay(1);
}

// Bluetooth stack events (their own task): update variables, nothing else.
void sl_bt_on_event(sl_bt_msg_t* evt) {
  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id:
      booted = true;
      break;
    case sl_bt_evt_connection_opened_id:
      conn = evt->data.evt_connection_opened.connection;
      payload = 20; mtu = 23; subscribed = false; linkUp = true;
      opens = opens + 1;
      break;
    case sl_bt_evt_connection_closed_id:
      if (evt->data.evt_connection_closed.connection != conn) break;
      linkUp = false; subscribed = false; conn = SL_BT_INVALID_CONNECTION_HANDLE;
      closeReason = evt->data.evt_connection_closed.reason;
      closes = closes + 1;
      advertise();
      break;
    case sl_bt_evt_gatt_mtu_exchanged_id:
      mtu = evt->data.evt_gatt_mtu_exchanged.mtu;
      payload = mtu - 3 > 244 ? 244 : mtu - 3;
      break;
    case sl_bt_evt_gatt_server_characteristic_status_id: {
      const auto& s = evt->data.evt_gatt_server_characteristic_status;
      if (s.characteristic == txHandle && s.status_flags == sl_bt_gatt_server_client_config)
        subscribed = (s.client_config_flags & sl_bt_gatt_server_notification) != 0;
      break;
    }
    case sl_bt_evt_gatt_server_attribute_value_id: {
      const auto& a = evt->data.evt_gatt_server_attribute_value;
      if (a.attribute != rxHandle) break;
      size_t h = head;
      for (uint8_t i = 0; i < a.value.len; i++) {
        size_t next = (h + 1) % sizeof(ring);
        if (next == tail) break;
        ring[h] = (char)a.value.data[i];
        h = next;
      }
      __sync_synchronize();
      head = h;
      break;
    }
    default:
      break;
  }
}
