#include "services/BatteryManager.h"
#include "config/battery_config.h"
#include "infra/Logger.h"

BatteryManager::BatteryManager(PowerMonitor &mon, FRAM &fram) : m_mon(mon), m_fram(fram) {}

void BatteryManager::begin() {
    m_socPct = liIonSOC(voltage());
    m_socEst = m_socPct;
    uint8_t m = 0;
    float full = 0.0f, counted = 0.0f, pct = 0.0f;
    uint8_t anchored = 0;
    if (m_fram.socPrefsLoad(m, full, counted, pct, anchored) && m <= 1) {
        m_method = (SocMethod)m;
        m_fullMah = full;
        m_socMah = counted;
        m_socPct = pct;
        m_anchored = (anchored != 0);
    }
    m_lastMs = millis();
    m_lastSaveMs = m_lastMs;
}

void BatteryManager::setSocMethod(SocMethod m) {
    m_method = (m == SOC_ENERGY) ? SOC_ENERGY : SOC_VOLTAGE;
    saveState();
}

void BatteryManager::saveState() {
    m_fram.socPrefsSave((uint8_t)m_method, m_fullMah, m_socMah, m_socPct, m_anchored ? 1 : 0);
    m_lastSaveMs = millis();
}

void BatteryManager::update(float chargeCurrentA) {
    uint32_t now = millis();
    if (!m_lastMs) { m_lastMs = now; return; }
    float dt = (float)(now - m_lastMs) / 1000.0f;
    m_lastMs = now;
    if (dt <= 0.0f) return;
    if (dt > 10.0f) dt = 10.0f;             /* tolerate scheduling gaps */

    /* mAh = A * s / 3.6 */
    m_socMah += chargeCurrentA * dt / 3.6f;

    if (m_fullMah > 1.0f) {
        float target = m_socMah / m_fullMah * 100.0f;
        if (target < 0.0f) target = 0.0f;
        if (target > 100.0f) target = 100.0f;
        /* 0.1% deadband: commit only meaningful changes (LibreSolar-style) */
        if (fabsf(target - m_socPct) >= 0.1f) m_socPct = target;
    }

    if ((uint32_t)(now - m_lastSaveMs) >= 60000UL) saveState();
}

void BatteryManager::anchorFull() {
    /* Only learn FCC if this cycle started from a known empty anchor. */
    if (m_anchored && m_socMah > 500.0f) m_fullMah = m_socMah;
    if (m_fullMah > 1.0f) m_socMah = m_fullMah;
    m_socPct = 100.0f;
    m_anchored = false;
    saveState();
}

void BatteryManager::anchorEmpty() {
    m_socMah = 0.0f;
    m_socPct = 0.0f;
    m_anchored = true;
    saveState();
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
    if (m_method == SOC_ENERGY && m_fullMah > 1.0f) {
        return m_socPct;
    }
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
    /* Use the actual battery energy counter shown on screen: it is reset at
     * full discharge and integrates net pack energy, so it represents the
     * energy available now. The old capacity*SOC estimate ignored the real
     * pack size and made RtE wrong on multi-cell packs. */
    float e = energyWh();
    return (e > 0.0f) ? e : 0.0f;
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
