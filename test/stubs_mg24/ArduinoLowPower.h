#pragma once
#include <stdint.h>
class LowPowerClass { public: void deepSleep(uint32_t ms = 0); bool wokeUpFromDeepSleep(); void attachInterruptWakeup(int, void (*)(), int); void deepSleepMemoryWrite(uint32_t, uint32_t); uint32_t deepSleepMemoryRead(uint32_t); uint32_t deepSleepMemorySize(); };
extern LowPowerClass LowPower;
