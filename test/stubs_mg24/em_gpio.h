// Stand-in for the Silicon Labs GPIO driver (em_gpio.h) — host compile check only.
#pragma once
#include <stdint.h>
typedef enum { gpioPortA, gpioPortB, gpioPortC, gpioPortD } GPIO_Port_TypeDef;
void GPIO_PortOutSet(GPIO_Port_TypeDef port, uint32_t pins);
void GPIO_PortOutClear(GPIO_Port_TypeDef port, uint32_t pins);
