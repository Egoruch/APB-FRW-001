#include "services/EnergyManager.h"
#include "config/battery_config.h"
#include "infra/Logger.h"
#include "infra/term.h"

EnergyManager::EnergyManager(PowerMonitor &mon, FRAM &fram) : m_mon(mon), m_fram(fram) {}

void EnergyManager::begin() {
    /* counters already restored from FRAM by FRAM::begin() */
    m_lastMs = millis();
    bool on = true;                          /* enabled by default */
    if (m_fram.sessionLoad(on)) m_sessionEnabled = on;
}

void EnergyManager::setSessionEnabled(bool on) {
    m_sessionEnabled = on;
    m_fram.sessionSave(on);
}

bool EnergyManager::takeResetNotice(char *port, size_t n, float &oldWh) {
    if (!m_noticePending) return false;
    m_noticePending = false;
    oldWh = m_noticeWh;
    if (n) {
        size_t i = 0;
        for (; i + 1 < n && m_noticePort[i]; i++) port[i] = m_noticePort[i];
        port[i] = 0;
    }
    return true;
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

void EnergyManager::updatePortSessions(uint32_t now) {
    static const PowerChannel ports[] = {CH_C1, CH_C2, CH_A1, CH_A2};
    for (PowerChannel ch : ports) {
        float cur = m_mon.get(ch).current;
        float acur = fabsf(cur);
        /* Both directions count as a connection: a device drawing power from
         * the port (outflow) and a charger feeding the pack (inflow). */
        bool loaded = (acur > PORT_SESSION_ON_A);
        if (loaded) {
            /* load detected: a new device only after it has been absent long
             * enough to re-arm, and the load must persist to count as connect */
            if (!m_portPresent[ch]) {
                if (!m_portLoadSince[ch]) m_portLoadSince[ch] = now;
                if ((uint32_t)(now - m_portLoadSince[ch]) >= PORT_SESSION_DEBOUNCE_MS) {
                    m_portPresent[ch] = true;
                    m_portAbsentSince[ch] = 0;
                    float oldWh = m_fram.data().energyWh[ch];
                    m_fram.data().energyWh[ch] = 0;
                    m_fram.data().chargeMah[ch] = 0;
                    snprintf(m_noticePort, sizeof(m_noticePort), "%s", kChannelName[ch]);
                    m_noticeWh = oldWh;
                    m_noticePending = true;
                    /* normal-mode console line (unconditional) + on-device
                     * terminal pane + debug log */
                    char line[80];
                    snprintf(line, sizeof(line),
                             "[energy] Port %s: session reset, previous %.3f Wh",
                             kChannelName[ch], oldWh);
                    Serial.print(line); Serial.print("\r\n");
                    termAppend(line); termAppend("\n");
                    LOG_INFO("Port %s: new device -> session reset (previous %.3f Wh)", kChannelName[ch], oldWh);
                }
            }
        } else {
            m_portLoadSince[ch] = 0;
            if (m_portPresent[ch] && acur < PORT_SESSION_OFF_A) {
                if (!m_portAbsentSince[ch]) m_portAbsentSince[ch] = now;
                if ((uint32_t)(now - m_portAbsentSince[ch]) >= PORT_SESSION_ABSENT_MS) {
                    m_portPresent[ch] = false;   /* removed; value stays latched */
                    m_portAbsentSince[ch] = 0;
                    LOG_INFO("Port %s: device removed (session value kept)", kChannelName[ch]);
                }
            } else if (acur >= PORT_SESSION_OFF_A) {
                m_portAbsentSince[ch] = 0;       /* inside hysteresis band: stay armed */
            }
        }
    }
}

void EnergyManager::update() {
    uint32_t now = millis();
    if (now - m_lastMs < BATTERY_SAMPLE_MS) return;
    float dt = (float)(now - m_lastMs) / 1000.0f;   /* seconds since last sample */
    m_lastMs = now;
    if (dt <= 0.0f) return;

    if (m_sessionEnabled) updatePortSessions(now);

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
