#pragma once
#include "Arduino.h"
#define BANDWIDTH_MAX 3
#define BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE 6
class BLEService {};
class BLEUart : public Stream, public BLEService { public: void begin(); void bufferTXD(bool); bool notifyEnabled(); };
struct BLEConnection { bool requestPHY(); bool requestDataLengthUpdate(); bool requestMtuExchange(uint16_t); };
struct BLEPeriph { void setConnectCallback(void (*)(uint16_t)); void setDisconnectCallback(void (*)(uint16_t, uint8_t)); };
struct BLEAdv { bool addFlags(uint8_t); bool addTxPower(); bool addService(BLEService&); bool addName(); void restartOnDisconnect(bool); void setInterval(uint16_t, uint16_t); void setFastTimeout(uint16_t); bool start(uint16_t); bool stop(); };
struct AdafruitBluefruit { BLEPeriph Periph; BLEAdv Advertising, ScanResponse; void autoConnLed(bool); void configPrphBandwidth(uint8_t); bool begin(); bool setTxPower(int8_t); void setName(const char*); bool connected(); uint16_t connHandle(); bool disconnect(uint16_t); BLEConnection* Connection(uint16_t); };
extern AdafruitBluefruit Bluefruit;
