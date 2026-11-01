#include "hardware/PowerMonitor.h"
#include "config/addresses.h"
#include "config/pins.h"
#include "config/config.h"
#include "infra/Logger.h"
#include "infra/I2CBus.h"
#include <Wire.h>

/* Shunt resistor per channel (ohms): SYS uses 10mOhm, all others 1mOhm. */
static const float kShunt[CH_COUNT] = {
    0.001f,  /* BAT (unused, derived) */
    0.010f,  /* SYS */
    0.001f,  /* IP1 */
    0.001f,  /* IP2 */
    0.001f,  /* C1 */
    0.001f,  /* C2 */
    0.001f,  /* A1 */
    0.001f,  /* A2 */
    0.001f   /* ELOAD */
};

const uint8_t PowerMonitor::kAddr[CH_COUNT] = {
    0,                        /* BAT: no INA, derived */
    INA226_SYS_ADDR,
    INA226_IP1_ADDR,
    INA226_IP2_ADDR,
    INA226_C1_ADDR,
    INA226_C2_ADDR,
    INA226_A1_ADDR,
    INA226_A2_ADDR,
    INA226_ELOAD_ADDR
};

PowerMonitor::PowerMonitor()
    : m_ina{INA226(0), INA226(0x40), INA226(0x41), INA226(0x42),
            INA226(0x43), INA226(0x44), INA226(0x45), INA226(0x46), INA226(0x47)} {
    for (int i = 0; i < CH_COUNT; i++) {
        m_ok[i] = false;
        m_meas[i] = PowerMeasurement();
    }
    /* fix battery/dummy entry (no INA226 for BAT) */
    m_ok[CH_BAT] = false;
}

uint8_t PowerMonitor::begin() {
    /* Single fast pass - the ESP32-C6 i2c-ng driver can intermittently drop
     * transactions, so the caller re-invokes this until the needed rail is
     * found (see monitorLoop). No slow Wire re-init retry here. */
    uint8_t found = 0;
    if (!i2cBusLock(1000)) return 0;
    for (int i = 1; i < CH_COUNT; i++) {
        m_ina[i].setMaxCurrentShunt(8.19f, kShunt[i]);
        m_ok[i] = m_ina[i].begin();
        if (m_ok[i]) found++;
        else LOG_WARN("INA226 @0x%02X (%s) not found", kAddr[i], kChannelName[i]);
    }
    i2cBusUnlock();
    return found;
}

bool PowerMonitor::hasAll() const {
    for (int i = 1; i < CH_COUNT; i++)
        if (!m_ok[i]) return false;
    return true;
}

void PowerMonitor::sampleChannel(PowerChannel ch) {    if (ch == CH_BAT) return; /* computed elsewhere */
    if (ch < 0 || ch >= CH_COUNT || !m_ok[ch]) return;
    if (!i2cBusLock(50)) return;
    m_meas[ch].voltage = m_ina[ch].getBusVoltage();
    m_meas[ch].current = m_ina[ch].getCurrent();
    m_meas[ch].power   = m_ina[ch].getPower();
    m_meas[ch].timestamp = millis();
    i2cBusUnlock();
}

void PowerMonitor::sampleAll() {
    for (int i = 1; i < CH_COUNT; i++) sampleChannel((PowerChannel)i);
}

void PowerMonitor::resetChannel(PowerChannel) {
    /* accumulated counter reset is handled by EnergyManager, not the INA226
     * hardware accumulator, so keep the INA226 reading as-is. */
}
