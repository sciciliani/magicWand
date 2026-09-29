#pragma once
#include <stdint.h>
#include <stddef.h>
namespace Adafruit_LittleFS_Namespace {
enum { FILE_O_READ, FILE_O_WRITE };
class Adafruit_LittleFS { public: bool begin(); bool remove(const char*); bool format(); };
class File { public: File(Adafruit_LittleFS&); bool open(const char*, uint8_t); int read(void*, uint16_t); size_t write(const uint8_t*, size_t); void close(); };
}
