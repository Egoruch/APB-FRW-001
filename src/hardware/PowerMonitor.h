#pragma once
#include <Arduino.h>
#include "INA226.h"
#include "measurements.h"

/* Centralized power monitoring over the INA226 rail monitors.
 * Each channel maps to a dedicated INA226; BAT is derived by the battery
 * manager (BAT = SYS + IP1 + IP2 + ELOAD). */

class PowerMonitor {
public:
    PowerMonitor();

    /* begin() probes every INA226, reports missing units. Returns number found. */
    uint8_t begin();

    /* Sample all rails into m_meas[]. Caller should hold the I2C mutex. */
    void sampleAll();

    /* Sample a single channel. */
    void sampleChannel(PowerChannel ch);

    bool present(PowerChannel ch) const        { return m_ok[ch]; }
    bool hasAll() const;                  /* every real INA226 found? */
    const PowerMeasurement &get(PowerChannel ch) const { return m_meas[ch]; }
    PowerMeasurement &get(PowerChannel ch)             { return m_meas[ch]; }

    /* reset accumulated counters of a channel to zero */
    void resetChannel(PowerChannel ch);

private:
    INA226 m_ina[CH_COUNT];
    bool m_ok[CH_COUNT];
    PowerMeasurement m_meas[CH_COUNT];
    static const uint8_t kAddr[CH_COUNT];
};
