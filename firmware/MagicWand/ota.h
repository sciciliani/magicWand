// ota.h — firmware update over Bluetooth.
//
// The XIAO MG24 ships with the Silicon Labs Gecko bootloader ("storage,
// internal, single 512k"). This module receives a .gbl file from the app into
// the bootloader's storage slot, checks it, and asks the bootloader to install
// it on the next reboot. The running firmware is never touched until the new
// image has fully arrived and passed the bootloader's own check, so a dropped
// link or a bad file just leaves the wand on its current version.
//
// Protocol (text on the NUS channel, bytes on the OTA characteristic):
//   OTA                       -> OTA INFO ok|no <reason|-> slot=.. len=.. app=.. nvm=.. btl=.. chunk=..
//   OTA BEGIN <size> <crc32>  -> OTA READY <chunk>   (or OTA ERR <why>)
//   <size> raw bytes on 6E400004-...                 <- OTA ACK <bytes received> every 2 KB
//   OTA END                   -> OTA OK (then reboots into the bootloader, which installs and starts it)
//   OTA ABORT                 -> OTA ABORTED
#pragma once
#include <stdint.h>

namespace ota {

void command(const char* a1, const char* a2, const char* a3);
void poll();      // call every loop while active()
bool active();    // an update is being received: no sampling, no sleep

}  // namespace ota
