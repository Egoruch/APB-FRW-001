#pragma once
#include <Arduino.h>
#include "Temperature_LM75_Derived.h"

/* TMP102 temperature sensors (4). Addresses are fixed per the schematic. */
class TempSensor {
public:
    TempSensor();
    void begin();
    float readC(uint8_t idx);      /* idx 0..3; returns NAN if absent */
    bool present(uint8_t idx) const { return m_ok[idx]; }

private:
    TI_TMP102 m_s[4];
    bool m_ok[4];
    static const uint8_t kAddr[4];
    static const char *kName[4];
};
