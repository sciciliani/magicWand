// Stand-in for em_cmu.h — host compile check only.
#pragma once
#include <stdint.h>
typedef enum { cmuClock_EM4GRPACLK, cmuClock_BURTC, cmuClock_BURAM, cmuClock_TIMER1, cmuClock_TIMER2, cmuClock_TIMER3, cmuClock_TIMER4 } CMU_Clock_TypeDef;
typedef enum { cmuSelect_ULFRCO } CMU_Select_TypeDef;
void CMU_ClockSelectSet(CMU_Clock_TypeDef, CMU_Select_TypeDef);
void CMU_ClockEnable(CMU_Clock_TypeDef, bool);
uint32_t CMU_ClockFreqGet(CMU_Clock_TypeDef);
