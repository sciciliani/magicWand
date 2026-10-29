// Host stub: Gecko bootloader API (the real one ships with the core's BLE (Silabs) variant).
#pragma once
#include <stddef.h>
#include <stdint.h>
#define BOOTLOADER_OK 0L
#define BOOTLOADER_CAPABILITY_ENFORCE_UPGRADE_SIGNATURE (1 << 0)
#define BOOTLOADER_CAPABILITY_ENFORCE_UPGRADE_ENCRYPTION (1 << 1)
typedef enum { NO_BOOTLOADER = 0, SL_BOOTLOADER = 1 } BootloaderType_t;
typedef struct { BootloaderType_t type; uint32_t version; uint32_t capabilities; } BootloaderInformation_t;
typedef struct { uint32_t address; uint32_t length; } BootloaderStorageSlot_t;
typedef void (*BootloaderParserCallback_t)(uint32_t, uint32_t, const uint8_t*, size_t, void*);
inline void bootloader_getInfo(BootloaderInformation_t* i) { *i = {SL_BOOTLOADER, 0, 0}; }
inline int32_t bootloader_init(void) { return 0; }
inline int32_t bootloader_getStorageSlotInfo(uint32_t, BootloaderStorageSlot_t* s) { *s = {0x08100000, 0x80000}; return 0; }
inline int32_t bootloader_eraseWriteStorage(uint32_t, uint32_t, uint8_t*, size_t) { return 0; }
inline int32_t bootloader_readStorage(uint32_t, uint32_t, uint8_t*, size_t) { return 0; }
inline int32_t bootloader_verifyImage(uint32_t, BootloaderParserCallback_t) { return 0; }
inline int32_t bootloader_setImageToBootload(int32_t) { return 0; }
inline void bootloader_rebootAndInstall(void) {}
