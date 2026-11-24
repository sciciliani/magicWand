// Stand-in for em_timer.h — host compile check only.
#pragma once
#include <stdint.h>
typedef struct { volatile uint32_t CNT; } TIMER_TypeDef;
extern TIMER_TypeDef *TIMER1, *TIMER2, *TIMER3, *TIMER4;
// Series 2 values are DIV-1 (not sequential), like the real header
typedef enum { timerPrescale1 = 0, timerPrescale2 = 1, timerPrescale4 = 3, timerPrescale8 = 7, timerPrescale16 = 15,
               timerPrescale32 = 31, timerPrescale64 = 63, timerPrescale128 = 127, timerPrescale256 = 255,
               timerPrescale512 = 511, timerPrescale1024 = 1023 } TIMER_Prescale_TypeDef;
typedef enum { timerCCModeOff, timerCCModePWM } TIMER_CCMode_TypeDef;
typedef struct { bool enable, debugRun; TIMER_Prescale_TypeDef prescale; } TIMER_Init_TypeDef;
typedef struct { TIMER_CCMode_TypeDef mode; bool coist, outInvert; } TIMER_InitCC_TypeDef;
#define TIMER_INIT_DEFAULT {true, false, timerPrescale1}
#define TIMER_INITCC_DEFAULT {timerCCModeOff, false, false}
void TIMER_Init(TIMER_TypeDef*, const TIMER_Init_TypeDef*);
void TIMER_InitCC(TIMER_TypeDef*, unsigned int ch, const TIMER_InitCC_TypeDef*);
void TIMER_TopSet(TIMER_TypeDef*, uint32_t);
void TIMER_CompareSet(TIMER_TypeDef*, unsigned int ch, uint32_t);
void TIMER_Enable(TIMER_TypeDef*, bool);
void TIMER_TopBufSet(TIMER_TypeDef*, uint32_t);
void TIMER_CompareBufSet(TIMER_TypeDef*, unsigned int ch, uint32_t);
void TIMER_CounterSet(TIMER_TypeDef*, uint32_t);
