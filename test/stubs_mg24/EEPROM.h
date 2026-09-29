#pragma once
#include <stdint.h>
#include <stddef.h>
struct EEPROMClass { uint8_t read(int); void update(int, uint8_t); size_t length(); template <class T> T& get(int, T& t) { return t; } };
extern EEPROMClass EEPROM;
