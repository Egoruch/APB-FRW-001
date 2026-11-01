#pragma once
#include <Arduino.h>
#include "hardware/PowerMonitor.h"
#include "hardware/FRAM.h"

/* Battery calculations: derived battery rail (BAT = SYS+IP1+IP2+ELOAD),
 * SOC estimation from voltage + Wh, energy remaining. Model/calibration is
 * stored in FRAM and adjustable. */
class BatteryManager {
public:
    BatteryManager(PowerMonitor &mon, FRAM &fram);

    void begin();

    float voltage() const;    /* battery voltage (SYS INA bus voltage) */
    float current() const;    /* battery current (SYS+IP1+IP2+ELOAD) */
    float power() const;      /* battery power */
    float soc() const;        /* 0..100, voltage + Wh calibrated */
    float capacityMah() const;
    float energyWh() const;   /* accumulated */
    float energyRemainingWh() const;

    void recalibrate();       /* set SOC reference from current voltage */
    void setCapacityMah(float mah);

private:
    PowerMonitor &m_mon;
    FRAM &m_fram;
    float m_socEst = 50.0f;
    float liIonSOC(float voltage) const;
};
