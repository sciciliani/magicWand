// Stand-in for em_burtc.h — host compile check only.
#pragma once
#include <stdint.h>
typedef struct { bool start; int debugRun; uint32_t clkDiv; bool compare0Top; bool em4comp; bool em4overflow; } BURTC_Init_TypeDef;
#define BURTC_INIT_DEFAULT {true, 0, 1, false, false, false}
#define BURTC_IEN_COMP 0x2u
enum IRQn_Type { BURTC_IRQn };
void BURTC_Init(const BURTC_Init_TypeDef*); void BURTC_CounterReset(void); void BURTC_CompareSet(unsigned, uint32_t);
void BURTC_IntEnable(uint32_t); void BURTC_Enable(bool); void NVIC_EnableIRQ(IRQn_Type);
