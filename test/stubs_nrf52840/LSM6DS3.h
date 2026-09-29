#pragma once
#include <stdint.h>
enum status_t { IMU_SUCCESS = 0, IMU_HW_ERROR };
enum { I2C_MODE };
class LSM6DS3 { public: LSM6DS3(int, uint8_t); status_t begin(); status_t writeRegister(uint8_t, uint8_t); status_t readRegister(uint8_t*, uint8_t); status_t readRegisterRegion(uint8_t*, uint8_t, uint8_t); };
