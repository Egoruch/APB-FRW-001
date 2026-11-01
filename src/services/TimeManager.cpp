#include "services/TimeManager.h"
#include "hardware/FRAM.h"
#include "config/addresses.h"
#include "infra/Logger.h"
#include "infra/I2CBus.h"
#include <Wire.h>

DateTime TimeManager::readExternal() {
    if (!m_present) return DateTime(2020, 1, 1, 0, 0, 0);
    if (!i2cBusLock(100)) return DateTime(2020, 1, 1, 0, 0, 0);
    DateTime res = m_rtc.now();
    i2cBusUnlock();
    return res;
}

void TimeManager::begin(FRAM *fram) {
    m_fram = fram;
    m_startMs = millis();

    if (m_src == TIME_SRC_SOFTWARE) {
        uint32_t ep = 0;
        if (m_fram && m_fram->timeLoad(ep)) {
            m_baseEpoch = ep;
            LOG_INFO("Software clock restored from FRAM");
        } else {
            m_baseEpoch = DateTime(F(__DATE__), F(__TIME__)).unixtime();
            if (m_fram) m_fram->timeSave(m_baseEpoch);
            LOG_INFO("Software clock seeded from build time");
        }
        m_baseMs = millis();
        m_lastSaveMs = millis();
        m_softValid = true;
        m_present = true;
        return;
    }

    if (!i2cBusLock(1000)) { m_present = false; return; }
    bool ok = m_rtc.begin();
    if (ok) {
        /* Ensure the oscillator is running before reading/writing time. */
        m_rtc.start();
        Wire.beginTransmission(ADDR_RTC_BM8563); Wire.write(0x0D); Wire.write(0x00); Wire.endTransmission();  /* CLKOUT off */
        Wire.beginTransmission(ADDR_RTC_BM8563); Wire.write(0x01); Wire.write(0x00); Wire.endTransmission();
        DateTime nowdt = m_rtc.now();
        /* A never-initialized BM8563 reads back 0xFF registers, which decode
         * to nonsense (e.g. 2165-25-45) and would pass a naive year check.
         * Validate every field before trusting it. */
        bool valid = nowdt.year() >= 2024 && nowdt.year() <= 2100 &&
                     nowdt.month() >= 1 && nowdt.month() <= 12 &&
                     nowdt.day() >= 1 && nowdt.day() <= 31 &&
                     nowdt.hour() < 24 && nowdt.minute() < 60 && nowdt.second() < 60;
        if (!valid) {
            m_rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
            LOG_INFO("RTC time invalid - set to build time");
        }
        m_rtc.start();   /* make sure STOP is clear after a time write */
    }
    i2cBusUnlock();
    m_present = ok;
    if (!ok) LOG_WARN("RTC not found");
    else LOG_INFO("RTC present");
}

void TimeManager::setSource(TimeSource s) {
    if (s == m_src) return;
    if (s == TIME_SRC_SOFTWARE) {
        DateTime seed;
        bool have = false;
        if (m_src == TIME_SRC_EXTERNAL) {
            DateTime ext = readExternal();
            if (ext.year() >= 2024) { seed = ext; have = true; }
        }
        if (!have && m_fram) {
            uint32_t ep = 0;
            if (m_fram->timeLoad(ep)) { seed = DateTime(ep); have = true; }
        }
        if (!have) seed = DateTime(F(__DATE__), F(__TIME__));
        m_src = TIME_SRC_SOFTWARE;
        m_baseEpoch = seed.unixtime();
        m_baseMs = millis();
        m_lastSaveMs = millis();
        m_softValid = true;
        m_present = true;
        if (m_fram) m_fram->timeSave(m_baseEpoch);
        LOG_INFO("Time source -> software (%lu)", (unsigned long)m_baseEpoch);
    } else {
        m_src = TIME_SRC_EXTERNAL;
        m_softValid = false;
        begin(m_fram);
        LOG_INFO("Time source -> external RTC");
    }
}

bool TimeManager::present() {
    if (m_src == TIME_SRC_SOFTWARE) return m_softValid;
    return m_present;
}

DateTime TimeManager::now() {
    if (m_src == TIME_SRC_SOFTWARE) {
        if (!m_softValid) return DateTime(2020, 1, 1, 0, 0, 0);
        return DateTime(m_baseEpoch + (millis() - m_baseMs) / 1000UL);
    }
    return readExternal();
}

bool TimeManager::set(const DateTime &dt) {
    if (m_src == TIME_SRC_SOFTWARE) {
        m_baseEpoch = dt.unixtime();
        m_baseMs = millis();
        m_lastSaveMs = millis();
        m_softValid = true;
        m_present = true;
        if (m_fram) m_fram->timeSave(m_baseEpoch);
        return true;
    }
    if (!m_present) return false;
    if (!i2cBusLock(100)) return false;
    m_rtc.adjust(dt);
    m_rtc.start();          /* clear STOP: adjust() alone does not restart the clock */
    i2cBusUnlock();
    return true;
}

bool TimeManager::running() {
    if (m_src == TIME_SRC_SOFTWARE) return m_softValid;
    if (!m_present) return false;
    if (!i2cBusLock(100)) return false;
    bool r = m_rtc.isrunning();
    i2cBusUnlock();
    return r;
}

unsigned long TimeManager::uptime() const {
    return millis() / 1000UL;
}

String TimeManager::nowISO() {
    DateTime t = now();
    char buf[24];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
             t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
    return String(buf);
}

void TimeManager::poll() {
    if (m_src != TIME_SRC_SOFTWARE || !m_softValid) return;
    uint32_t nowMs = millis();
    if (nowMs - m_lastSaveMs < 60000UL) return;
    m_lastSaveMs = nowMs;
    if (m_fram) m_fram->timeSave(m_baseEpoch + (nowMs - m_baseMs) / 1000UL);
}

bool TimeManager::saveNow() {
    if (m_src != TIME_SRC_SOFTWARE || !m_softValid) return false;
    if (!m_fram) return false;
    return m_fram->timeSave(m_baseEpoch + (millis() - m_baseMs) / 1000UL);
}
