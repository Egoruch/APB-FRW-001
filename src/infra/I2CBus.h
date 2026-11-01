#pragma once
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Serializes access to the shared I2C bus. Every I2C transaction across all
 * tasks must be wrapped in i2cBusLock()/i2cBusUnlock() (requirement 33). */
void i2cBusInit();
bool i2cBusLock(uint32_t timeoutMs = 1000);
void i2cBusUnlock();

