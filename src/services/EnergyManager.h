#pragma once
#include <Arduino.h>
#include "measurements.h"
#include "hardware/PowerMonitor.h"
#include "hardware/FRAM.h"

/* Portable charge/energy counters per channel + battery.
 * Integrates sample measurements and periodically flushes to FRAM in a
 * wear-conscious manner so the battery counter survives MCU reset. */
class EnergyManager {
public:
    EnergyManager(PowerMonitor &mon, FRAM &fram);

    void begin();
    void update();   /* call periodically (uses internal ms clock) */

    float energyWh(PowerChannel ch) const;
    float chargeMah(PowerChannel ch) const;
    void resetChannel(PowerChannel ch);
    void flush();    /* force write to FRAM */

private:
    PowerMonitor &m_mon;
    FRAM &m_fram;
    uint32_t m_lastMs = 0;
};
