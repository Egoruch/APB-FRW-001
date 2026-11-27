#pragma once
#include <Arduino.h>
#include "hardware/PowerMonitor.h"
#include "hardware/FRAM.h"

/* Battery calculations: derived battery rail (BAT = SYS+IP1+IP2+ELOAD),
 * SOC estimation from voltage + Wh, energy remaining. Model/calibration is
 * stored in FRAM and adjustable. */
class BatteryManager {
public:
    enum SocMethod : uint8_t {
        SOC_VOLTAGE = 0,   /* OCV lookup table */
        SOC_ENERGY  = 1,   /* mAh coulomb counter */
    };

    BatteryManager(PowerMonitor &mon, FRAM &fram);

    void begin();

    float voltage() const;    /* battery voltage (SYS INA bus voltage) */
    float current() const;    /* battery current (SYS+IP1+IP2+ELOAD) */
    float power() const;      /* battery power */
    float soc() const;        /* 0..100, selected method */
    float capacityMah() const;
    float energyWh() const;   /* accumulated */
    float energyRemainingWh() const;

    SocMethod socMethod() const { return m_method; }
    void setSocMethod(SocMethod m);

    /* Coulomb counter: charge-positive current (A), integrates mAh and
     * applies SOC with a 0.1% deadband. Call periodically. */
    void update(float chargeCurrentA);
    void anchorFull();        /* charge complete: learn full mAh, SOC=100 */
    void anchorEmpty();       /* fully discharged: SOC=0 */
    float fullCapacityMah() const { return m_fullMah; }
    float countedMah() const { return m_socMah; }
    float socPercent() const { return m_socPct; }

    void recalibrate();       /* set SOC reference from current voltage */
    void setCapacityMah(float mah);

private:
    PowerMonitor &m_mon;
    FRAM &m_fram;
    float m_socEst = 50.0f;
    SocMethod m_method = SOC_VOLTAGE;
    float m_fullMah = 0.0f;   /* learned full-charge capacity (mAh) */
    float m_socMah = 0.0f;    /* charge counted since last empty anchor */
    float m_socPct = 0.0f;    /* displayed/committed coulomb SOC */
    bool m_anchored = false;  /* empty anchor seen in this cycle */
    uint32_t m_lastMs = 0;
    uint32_t m_lastSaveMs = 0;
    void saveState();
    float liIonSOC(float voltage) const;
};
