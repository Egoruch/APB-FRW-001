#include "services/EnergyManager.h"
#include "config/battery_config.h"
#include "infra/Logger.h"

EnergyManager::EnergyManager(PowerMonitor &mon, FRAM &fram) : m_mon(mon), m_fram(fram) {}

void EnergyManager::begin() {
    /* counters already restored from FRAM by FRAM::begin() */
    m_lastMs = millis();
}

float EnergyManager::energyWh(PowerChannel ch) const {
    return m_fram.data().energyWh[ch];
}
float EnergyManager::chargeMah(PowerChannel ch) const {
    return m_fram.data().chargeMah[ch];
}

void EnergyManager::resetChannel(PowerChannel ch) {
    if (ch < 0 || ch >= CH_COUNT) return;
    m_fram.data().energyWh[ch] = 0;
    m_fram.data().chargeMah[ch] = 0;
    m_fram.save();
}

void EnergyManager::flush() {
    m_fram.save();
}

void EnergyManager::update() {
    uint32_t now = millis();
    if (now - m_lastMs < BATTERY_SAMPLE_MS) return;
    float dt = (float)(now - m_lastMs) / 1000.0f;   /* seconds since last sample */
    m_lastMs = now;
    if (dt <= 0.0f) return;

    /* real INA226 channels */
    for (int i = 1; i < CH_COUNT; i++) {
        const PowerMeasurement &pm = m_mon.get((PowerChannel)i);
        float cur = pm.current;
        float pwr = pm.power;
        if (cur < 0) cur = 0;                       /* ignore negative (metering) */
        if (pwr < 0) pwr = 0;
        m_fram.data().chargeMah[i] += cur * dt / 3600.0f * 1000.0f;
        m_fram.data().energyWh[i]  += pwr * dt / 3600.0f;
    }

    /* battery (neutral rail): I = SYS + IP1 + IP2 + ELOAD */
    float batI = 0;
    batI += m_mon.get(CH_SYS).current;
    batI += m_mon.get(CH_IP1).current;
    batI += m_mon.get(CH_IP2).current;
    batI += m_mon.get(CH_ELOAD).current;
    float batV = m_mon.get(CH_SYS).voltage;
    float batW = batV * batI;

    m_fram.data().chargeMah[CH_BAT] += batI * dt / 3600.0f * 1000.0f;
    m_fram.data().energyWh[CH_BAT]  += (-batW) * dt / 3600.0f;   /* charging = positive */

    /* periodic wear-conscious flush */
    static uint32_t lastFlush = 0;
    if (now - lastFlush >= BATTERY_FRAM_FLUSH_MS) {
        lastFlush = now;
        m_fram.save();
    }
}
