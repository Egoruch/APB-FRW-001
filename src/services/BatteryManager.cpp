#include "services/BatteryManager.h"
#include "config/battery_config.h"
#include "infra/Logger.h"

BatteryManager::BatteryManager(PowerMonitor &mon, FRAM &fram) : m_mon(mon), m_fram(fram) {}

void BatteryManager::begin() {
    m_socEst = liIonSOC(voltage());
}

float BatteryManager::voltage() const {
    return m_mon.get(CH_SYS).voltage;
}

float BatteryManager::current() const {
    return m_mon.get(CH_SYS).current + m_mon.get(CH_IP1).current +
           m_mon.get(CH_IP2).current + m_mon.get(CH_ELOAD).current;
}

float BatteryManager::power() const {
    return voltage() * current();
}

float BatteryManager::liIonSOC(float v) const {
    const float vt[] = {3.00f,3.30f,3.50f,3.60f,3.65f,3.70f,3.75f,3.80f,3.85f,3.90f,
                        3.95f,4.00f,4.05f,4.10f,4.15f,4.20f};
    const float st[] = {0,2,5,10,15,20,30,40,50,60,70,80,85,90,95,100};
    const int n = (int)(sizeof(vt)/sizeof(vt[0]));
    if (v <= vt[0]) return 0.0f;
    if (v >= vt[n-1]) return 100.0f;
    for (int i = 1; i < n; i++)
        if (v < vt[i]) {
            float f = (v - vt[i-1]) / (vt[i] - vt[i-1]);
            return st[i-1] + f * (st[i] - st[i-1]);
        }
    return 0.0f;
}

float BatteryManager::soc() const {
    float vsoc = liIonSOC(voltage());
    /* after full charge, improve by Wh. Simple blend: keep voltage estimate
     * plus a persistent calibration offset stored in FRAM. */
    float off = m_fram.data().batteryCalOffset;
    float s = vsoc + off;
    if (s < 0) s = 0;
    if (s > 100) s = 100;
    return s;
}

float BatteryManager::capacityMah() const {
    return m_fram.data().batteryCapacityMah;
}

float BatteryManager::energyWh() const {
    return m_fram.data().energyWh[CH_BAT];
}

float BatteryManager::energyRemainingWh() const {
    return capacityMah() / 1000.0f * BATTERY_NOMINAL_VOLTAGE * soc() / 100.0f;
}

void BatteryManager::recalibrate() {
    m_fram.data().batteryCalOffset = 0.0f;   /* realign to pure voltage curve */
    m_fram.save();
}

void BatteryManager::setCapacityMah(float mah) {
    if (mah < 100) mah = 100;
    if (mah > 100000) mah = 100000;
    m_fram.data().batteryCapacityMah = mah;
    m_fram.save();
}
