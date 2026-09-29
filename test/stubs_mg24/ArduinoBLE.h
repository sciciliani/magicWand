#pragma once
#include <stdint.h>
enum { BLEWrite = 1, BLEWriteWithoutResponse = 2, BLENotify = 4 };
enum BLEDeviceEvent { BLEConnected, BLEDisconnected };
enum BLECharacteristicEvent { BLEWritten };
class BLEDevice {};
class BLECharacteristic { public: BLECharacteristic(const char*, uint8_t, int); const uint8_t* value(); int valueLength(); int writeValue(const uint8_t*, int); bool subscribed(); bool written(); void setEventHandler(BLECharacteristicEvent, void (*)(BLEDevice, BLECharacteristic)); };
class BLEService { public: BLEService(const char*); void addCharacteristic(BLECharacteristic&); };
class BLELocalDevice { public: int begin(); void end(); void poll(); bool connected(); bool disconnect(); bool setLocalName(const char*); bool setDeviceName(const char*); void addService(BLEService&); void setEventHandler(BLEDeviceEvent, void (*)(BLEDevice)); void setAdvertisingInterval(uint16_t); int advertise(); void stopAdvertise(); };
extern BLELocalDevice BLE;
