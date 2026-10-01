// Minimal stand-ins for the Silicon Labs Arduino core — ONLY for the host
// compile check (make check-fw). Not a simulator.
#pragma once
#define ARDUINO_SILABS_STACK_BLE_SILABS 1  // Tools > Protocol stack > BLE (Silabs)
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define CHANGE 2
#define RISING 3
#define FALLING 4
#define INPUT_PULLUP 5
#define LED_BUILTIN PA7
enum PinName { PA0, PA3, PA5, PA7, PC0, PC1, PC2, PC3, PC6, PC7, PD3, PD4, PD5 };
enum { D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10 };  // as the real variant: D6 = 6 (PC6)
void pinMode(int, int); void digitalWrite(int, int); int digitalRead(int);
uint32_t millis(); uint32_t micros(); void delay(uint32_t); void delayMicroseconds(uint32_t);
void attachInterrupt(int, void (*)(), int); void detachInterrupt(int);
inline int digitalPinToInterrupt(int p) { return p; }
void noInterrupts(); void interrupts();
int analogRead(int); void analogReadResolution(int);
uint32_t getCPUCycleCount(); uint32_t getCPUClock(); uint64_t getDeviceUniqueId(); void systemReset();
class Print { public: size_t print(const char*); size_t print(char); size_t print(int); size_t print(unsigned int); size_t print(long); size_t println(const char*); size_t println(); size_t printf(const char*, ...) __attribute__((format(printf, 2, 3))); };
class Stream : public Print { public: int available(); int read(); };
class UARTSerial : public Stream { public: void begin(int); void end(); void flush(); explicit operator bool() const; };
extern UARTSerial Serial;
struct EMU_TypeDef { volatile uint32_t EM4CTRL; };
extern EMU_TypeDef* EMU;
struct DWT_Type { volatile uint32_t CTRL, CYCCNT; }; extern DWT_Type* DWT;
struct DCB_Type { volatile uint32_t DEMCR; }; extern DCB_Type* DCB;
#define DCB_DEMCR_TRCENA_Msk (1u << 24)
#define DWT_CTRL_CYCCNTENA_Msk 1u
extern uint32_t SystemCoreClock;
#define _EMU_EM4CTRL_EM4IORETMODE_MASK 0x30u
#define EMU_EM4CTRL_EM4IORETMODE_EM4EXIT 0x10u
