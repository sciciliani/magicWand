#include "ota.h"

#include <Arduino.h>
#include <WatchdogTimer.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// The bootloader header has no extern "C" guard: without this the C++ compiler looks
// for mangled names and the link fails ("undefined reference to bootloader_...").
extern "C" {
#include "btl_interface.h"  // Gecko bootloader API (in the core's BLE (Silabs) variant)
}
#include "comm.h"
#include "config.h"

// Linker symbols (variants/xiao_mg24/ble_silabs/linkerfile.ld)
extern "C" {
extern char __etext[];
extern char __data_start__[];
extern char __data_end__[];
extern char linker_nvm_begin[];
}

namespace ota {

static const uint32_t kFlashBase = 0x08000000;
static const uint32_t kAppStart = 0x08006000;  // the core links every sketch here, after the bootloader
static const uint32_t kSlot = 0;
static const uint32_t kAckEvery = 2048;
static const uint32_t kQuietAbortMs = 15000;   // no data for this long: give up

static bool on = false;
static uint32_t total = 0, want = 0, got = 0, written = 0, nextAck = 0, lastData = 0, crc = 0;
alignas(4) static uint8_t buf[1024];
static size_t bufLen = 0;
static BootloaderStorageSlot_t slot = {0, 0};

static uint32_t crc32(uint32_t c, const uint8_t* p, size_t n) {
  c = ~c;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static uint32_t appEnd() { return (uint32_t)(uintptr_t)__etext + (uint32_t)(__data_end__ - __data_start__); }
static uint32_t nvmStart() { return (uint32_t)(uintptr_t)linker_nvm_begin; }

// Can an image of `size` bytes go through the storage slot? Empty string = yes.
static const char* check(uint32_t size, BootloaderInformation_t& bi) {
  bootloader_getInfo(&bi);
  if (bi.type == NO_BOOTLOADER) return "no-bootloader";
  if (bootloader_init() != BOOTLOADER_OK) return "bootloader-init";
  if (bootloader_getStorageSlotInfo(kSlot, &slot) != BOOTLOADER_OK || slot.length == 0) return "no-slot";
  if (slot.address < kFlashBase) slot.address += kFlashBase;  // some bootloaders report it relative to flash
  if (bi.capabilities & BOOTLOADER_CAPABILITY_ENFORCE_UPGRADE_SIGNATURE) return "needs-signed";
  if (bi.capabilities & BOOTLOADER_CAPABILITY_ENFORCE_UPGRADE_ENCRYPTION) return "needs-encrypted";
  if (slot.address < appEnd()) return "slot-overlaps-app";            // writing it would overwrite us
  if (slot.address + slot.length > nvmStart()) return "slot-overlaps-data";  // would wipe spells/codes
  if (size && size > slot.length) return "too-big";
  if (size && kAppStart + size > slot.address) return "too-big";     // the new app would land on the slot
  return "";
}

static void stop(const char* why) {
  on = false;
  bufLen = 0;
  comm::otaClear();
  if (why) comm::outf("OTA ERR %s", why);
}

static bool flush(size_t n) {  // write buf[0..n) (multiple of 4) at `written`
  if (!n) return true;
  WatchdogTimer.feed();
  int32_t r = bootloader_eraseWriteStorage(kSlot, written, buf, n);
  if (r != BOOTLOADER_OK) {
    char m[32];
    snprintf(m, sizeof(m), "write 0x%lx", (unsigned long)r);
    stop(m);
    return false;
  }
  written += n;
  return true;
}

static void finish() {
  // last bytes: pad to 4 with 0xFF (the GBL parser stops at the end tag)
  if (bufLen) {
    size_t n = (bufLen + 3) & ~3u;
    memset(buf + bufLen, 0xFF, n - bufLen);
    if (!flush(n)) return;
    bufLen = 0;
  }
  if (got != total) {
    char m[40];
    snprintf(m, sizeof(m), "short %lu/%lu", (unsigned long)got, (unsigned long)total);
    return stop(m);
  }
  if (crc != want) return stop("crc (transfer)");
  // read it back from flash: catches a bad write
  uint32_t c = 0;
  for (uint32_t o = 0; o < total; o += sizeof(buf)) {
    WatchdogTimer.feed();
    size_t n = total - o < sizeof(buf) ? total - o : sizeof(buf);
    if (bootloader_readStorage(kSlot, o, buf, n) != BOOTLOADER_OK) return stop("read back");
    c = crc32(c, buf, n);
  }
  if (c != want) return stop("crc (flash)");
  WatchdogTimer.feed();
  int32_t v = bootloader_verifyImage(kSlot, nullptr);
  if (v != BOOTLOADER_OK) {
    char m[32];
    snprintf(m, sizeof(m), "verify 0x%lx", (unsigned long)v);
    return stop(m);
  }
  if (bootloader_setImageToBootload(kSlot) != BOOTLOADER_OK) return stop("set image");
  comm::out("OTA OK installing, the wand restarts");
  on = false;
  uint32_t t0 = millis();
  while (millis() - t0 < 600) { comm::poll(); WatchdogTimer.feed(); delay(10); }  // let the reply go out
  comm::end();
  bootloader_rebootAndInstall();  // does not return
}

void command(const char* a1, const char* a2, const char* a3) {
  BootloaderInformation_t bi = {};
  if (!a1 || !strcmp(a1, "INFO")) {
    const char* why = check(0, bi);
    comm::outf("OTA INFO %s %s slot=0x%lx len=%lu app=0x%lx nvm=0x%lx btl=0x%lx cap=0x%lx chunk=%u", *why ? "no" : "ok",
               *why ? why : "-", (unsigned long)slot.address, (unsigned long)slot.length, (unsigned long)appEnd(),
               (unsigned long)nvmStart(), (unsigned long)bi.version, (unsigned long)bi.capabilities,
               (unsigned)comm::otaChunk());
  } else if (!strcmp(a1, "BEGIN")) {
    uint32_t size = a2 ? strtoul(a2, nullptr, 10) : 0;
    if (!size || !a3) return comm::out("OTA ERR usage: OTA BEGIN <size> <crc32 hex>");
    const char* why = check(size, bi);
    if (*why) return comm::outf("OTA ERR %s", why);
    total = size;
    want = strtoul(a3, nullptr, 16);
    got = written = bufLen = 0;
    crc = 0;
    nextAck = kAckEvery;
    lastData = millis();
    comm::otaClear();
    on = true;
    comm::outf("OTA READY %u", (unsigned)comm::otaChunk());
  } else if (!strcmp(a1, "END")) {
    if (!on) return comm::out("OTA ERR not started");
    poll();  // take whatever is still in the buffer
    if (on) finish();
  } else if (!strcmp(a1, "ABORT")) {
    stop(nullptr);
    comm::out("OTA ABORTED");
  } else {
    comm::out("OTA ERR unknown (OTA, OTA BEGIN, OTA END, OTA ABORT)");
  }
}

void poll() {
  if (!on) return;
  if (!comm::bleConnected()) return stop("link lost");
  if (comm::otaDropped()) return stop("overflow (app sent too fast)");
  for (int round = 0; round < 4; round++) {  // up to 4 KB per loop
    size_t n = comm::otaRead(buf + bufLen, sizeof(buf) - bufLen);
    if (!n) break;
    lastData = millis();
    if (got + n > total) return stop("too much data");
    crc = crc32(crc, buf + bufLen, n);
    got += n;
    bufLen += n;
    if (bufLen == sizeof(buf)) {
      if (!flush(bufLen)) return;
      bufLen = 0;
    }
  }
  if (got >= nextAck || (got == total && nextAck != 0xFFFFFFFF)) {
    comm::outf("OTA ACK %lu", (unsigned long)got);
    nextAck = got == total ? 0xFFFFFFFF : (got / kAckEvery + 1) * kAckEvery;
  }
  if (millis() - lastData > kQuietAbortMs) stop("timeout");
}

bool active() { return on; }

}  // namespace ota
