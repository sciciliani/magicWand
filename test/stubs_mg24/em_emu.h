// Stand-in for em_emu.h — host compile check only (EMU registers are in the Arduino.h stub).
#pragma once
#include <stdint.h>
typedef enum { emuPinRetentionDisable, emuPinRetentionEm4Exit, emuPinRetentionLatch } EMU_EM4PinRetention_TypeDef;
typedef struct { bool retainLfxo, retainLfrco, retainUlfrco; int em4State; EMU_EM4PinRetention_TypeDef pinRetentionMode; } EMU_EM4Init_TypeDef;
#define EMU_EM4INIT_DEFAULT {false, false, false, 0, emuPinRetentionDisable}
void EMU_EM4Init(const EMU_EM4Init_TypeDef*); void EMU_EnterEM4(void);
