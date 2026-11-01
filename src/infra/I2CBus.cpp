#include "infra/I2CBus.h"

static SemaphoreHandle_t s_i2cMutex = nullptr;

void i2cBusInit() {
    if (s_i2cMutex == nullptr)
        s_i2cMutex = xSemaphoreCreateMutex();
}

bool i2cBusLock(uint32_t timeoutMs) {
    if (s_i2cMutex == nullptr) return false;
    if (timeoutMs == 0) return (xSemaphoreTake(s_i2cMutex, 0) == pdTRUE);
    return (xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE);
}

void i2cBusUnlock() {
    if (s_i2cMutex) xSemaphoreGive(s_i2cMutex);
}
