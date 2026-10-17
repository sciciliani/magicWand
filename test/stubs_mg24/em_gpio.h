// Stand-in for the Silicon Labs GPIO driver (em_gpio.h) — host compile check only.
#pragma once
#include <stdint.h>
typedef enum { gpioPortA, gpioPortB, gpioPortC, gpioPortD } GPIO_Port_TypeDef;
typedef enum { gpioModeDisabled, gpioModeInput, gpioModeInputPull, gpioModePushPull } GPIO_Mode_TypeDef;
#define GPIO_PORT_MAX 3
#define GPIO_PORT_PIN_VALID(port, pin) ((pin) < 16)
void GPIO_PortOutSet(GPIO_Port_TypeDef port, uint32_t pins);
void GPIO_PortOutClear(GPIO_Port_TypeDef port, uint32_t pins);
void GPIO_PinModeSet(GPIO_Port_TypeDef port, unsigned int pin, GPIO_Mode_TypeDef mode, unsigned int out);
unsigned int GPIO_PinInGet(GPIO_Port_TypeDef port, unsigned int pin);
// Peripheral routing (DBUS) registers, as in efr32mg24_gpio.h.
typedef struct {
  volatile uint32_t ROUTEEN, CC0ROUTE, CC1ROUTE, CC2ROUTE, CDTI0ROUTE, CDTI1ROUTE, CDTI2ROUTE;
  uint32_t RESERVED0[1];
} GPIO_TIMERROUTE_TypeDef;
typedef struct {
  GPIO_TIMERROUTE_TypeDef TIMERROUTE[5], TIMERROUTE_SET[5], TIMERROUTE_CLR[5];
} GPIO_TypeDef;
extern GPIO_TypeDef* GPIO;
#define _GPIO_TIMER_CC0ROUTE_PORT_SHIFT 0
#define _GPIO_TIMER_CC0ROUTE_PIN_SHIFT 16
#define GPIO_TIMER_ROUTEEN_CC0PEN (0x1UL << 0)
