#pragma once
#include <Arduino.h>
#include <RTClib.h>

class FRAM;

/* Selectable time base:
 *  - external: talk to the BM8563 at 0x51 (may be unusable if the FRAM shares
 *              that address, see the FRAM/RTC overlap note)
 *  - software: keep a Unix epoch in FRAM and advance it with millis()
 */
enum TimeSource {
    TIME_SRC_EXTERNAL = 0,
    TIME_SRC_SOFTWARE = 1
};

/* Centralized time handling. Provides a monotonic source for timestamps and
 * the same DateTime/String API regardless of the selected time base. */
class TimeManager {
public:
    void begin(FRAM *fram = nullptr);
    bool present();

    DateTime now();
    bool set(const DateTime &dt);
    bool running();
    unsigned long uptime() const;

    /* string forms for CLI */
    String nowISO();

    /* time-base selection (persisted by the caller in PersistData) */
    void setSource(TimeSource s);
    TimeSource source() const { return m_src; }
    bool softwareValid() const { return m_softValid; }

    /* software clock: persist periodically and on demand (sleep/shutdown) */
    void poll();
    bool saveNow();

private:
    RTC_PCF8563 m_rtc;
    FRAM *m_fram = nullptr;
    TimeSource m_src = TIME_SRC_EXTERNAL;
    bool m_present = false;

    bool m_softValid = false;
    uint32_t m_baseEpoch = 0;   /* Unix seconds at m_baseMs */
    uint32_t m_baseMs = 0;
    uint32_t m_lastSaveMs = 0;

    uint32_t m_startMs = 0;

    DateTime readExternal();
};
