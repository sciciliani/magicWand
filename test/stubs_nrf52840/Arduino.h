// Minimal stand-ins for the Seeed nRF52 (Adafruit-based) core — ONLY for a
// host-side compile check (make check-fw). Not a simulator.
#pragma once
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
enum { D0 = 0, D1, D2, D3 };
#define LED_RED 11
#define LED_GREEN 13
#define LED_BLUE 12
#define VBAT_ENABLE 14
#define PIN_VBAT 32
#define PIN_CHARGING_CURRENT 22
#define PIN_LSM6DS3TR_C_POWER 15
#define PIN_LSM6DS3TR_C_INT1 18
enum { AR_DEFAULT, AR_INTERNAL_3_0 };
extern const uint32_t g_ADigitalPinMap[];
void pinMode(uint32_t, uint32_t); void digitalWrite(uint32_t, uint32_t);
uint32_t millis(); uint32_t micros(); void delay(uint32_t); void delayMicroseconds(uint32_t);
void attachInterrupt(uint32_t, void (*)(), uint32_t); void detachInterrupt(uint32_t);
inline uint32_t digitalPinToInterrupt(uint32_t p) { return p; }
void noInterrupts(); void interrupts();
int analogRead(uint32_t); void analogReference(int); void analogReadResolution(int);
template <class T, class L, class H> T constrain(T x, L a, H b) { return x < a ? a : (x > b ? b : x); }
class Print { public: size_t print(const char*); size_t print(char); size_t write(const uint8_t*, size_t); };
class Stream : public Print { public: int available(); int read(); };
class USBSerial : public Stream { public: void begin(int); explicit operator bool() const; };
extern USBSerial Serial;
struct NRF_PWM_SEQ { volatile uint32_t PTR, CNT, REFRESH, ENDDELAY; };
struct NRF_PWM_PSEL { volatile uint32_t OUT[4]; };
struct NRF_PWM_Type { volatile uint32_t TASKS_STOP, TASKS_SEQSTART[2], EVENTS_STOPPED, EVENTS_SEQEND[2], SHORTS, ENABLE, MODE, COUNTERTOP, PRESCALER, DECODER, LOOP; NRF_PWM_SEQ SEQ[2]; NRF_PWM_PSEL PSEL; };
extern NRF_PWM_Type* NRF_PWM3;
#define PWM_SHORTS_SEQEND0_STOP_Msk 1
#define PWM_MODE_UPDOWN_Up 0
#define PWM_PRESCALER_PRESCALER_DIV_1 0
#define PWM_DECODER_LOAD_Common 0
#define PWM_DECODER_MODE_RefreshCount 0
struct NRF_POWER_Type { volatile uint32_t RESETREAS, SYSTEMOFF; };
extern NRF_POWER_Type* NRF_POWER;
#define POWER_RESETREAS_OFF_Msk (1u << 16)
#define POWER_RESETREAS_VBUS_Msk (1u << 20)
#define POWER_USBREGSTATUS_VBUSDETECT_Msk 1u
struct NRF_FICR_Type { uint32_t DEVICEADDR[2]; };
extern NRF_FICR_Type* NRF_FICR;
enum { NRF_GPIO_PIN_PULLDOWN }; enum { NRF_GPIO_PIN_SENSE_HIGH };
void nrf_gpio_cfg_sense_input(uint32_t, int, int);
uint32_t sd_power_usbregstatus_get(uint32_t*); uint32_t sd_power_system_off();
