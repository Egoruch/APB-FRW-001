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

    /* Per-port session energy: when enabled (default), a port's counters are
     * zeroed when a new load connects and keep the last value after removal. */
    bool sessionEnabled() const { return m_sessionEnabled; }
    void setSessionEnabled(bool on);

    /* Consume a pending port notice (Wh + port name); disconnected=true when
     * the notice is for a removed device (session delivered energy). */
    bool takePortNotice(char *port, size_t n, float &wh, bool &disconnected);

private:
    PowerMonitor &m_mon;
    FRAM &m_fram;
    uint32_t m_lastMs = 0;
    bool m_sessionEnabled = true;
    bool m_portPresent[CH_COUNT] = {};
    bool m_portWasSource[CH_COUNT] = {};   /* session supplied a device */
    uint32_t m_portLoadSince[CH_COUNT] = {};
    uint32_t m_portAbsentSince[CH_COUNT] = {};
    volatile bool m_noticePending = false;
    volatile bool m_noticeDisconnect = false;
    char m_noticePort[4] = "";
    volatile float m_noticeWh = 0;
    void updatePortSessions(uint32_t now);
};
