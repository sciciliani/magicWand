// default_spells.h — factory spells, IR codes and bindings.
//
// Loaded on the very first boot (empty storage) and after a factory reset.
// To fill it with what's on a wand right now: open the Serial Monitor
// (115200, Newline), type EXPORT, and paste everything between the
// "BEGIN default_spells.h" and "END default_spells.h" lines over this file.
#pragma once

#define DEFAULT_GESTURES 0
#define DEFAULT_CODES 0
static const uint8_t kDefaultBind[8] = {255, 255, 255, 255, 255, 255, 255, 255};
