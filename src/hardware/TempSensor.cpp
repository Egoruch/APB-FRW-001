#include "hardware/TempSensor.h"
#include "config/addresses.h"
#include "infra/Logger.h"
#include "infra/I2CBus.h"
#include <math.h>

TempSensor::TempSensor() : m_s{TI_TMP102(TMP102_PB1_ADDR), TI_TMP102(TMP102_PB2_ADDR),
                             TI_TMP102(TMP102_ELOAD_ADDR), TI_TMP102(TMP102_AIR_ADDR)} {
    for (int i = 0; i < 4; i++) m_ok[i] = true;
}

void TempSensor::begin() {
    LOG_INFO("TempSensor ready");
}

float TempSensor::readC(uint8_t idx) {
    if (idx > 3) return NAN;
    if (!i2cBusLock(100)) return NAN;
    float t = m_s[idx].readTemperatureC();
    i2cBusUnlock();
    if (isnan(t)) m_ok[idx] = false;
    return t;
}
