#include "hardware/FRAM.h"
#include "config/addresses.h"
#include "config/battery_config.h"
#include "infra/Logger.h"
#include "infra/I2CBus.h"
#include <Wire.h>

#include <stddef.h>

/* Initialize defaults for the persisted payload. */
void persist_defaults(PersistData &d) {
    memset(&d, 0, sizeof(d));
    d.batteryCapacityMah = BATTERY_CAPACITY_MAH;
    d.batteryCalOffset = 0.0f;
    for (int i = 0; i < CH_COUNT; i++) { d.energyWh[i] = 0; d.chargeMah[i] = 0; }
    d.displayBrightness = 100;
    d.rgbBrightness = 100;
    d.logEnabled = 1;
    d.logLevel = 1;          /* INFO */
    d.debugEnabled = 0;
    d.rgbEffect = 0;
    d.lightPercent = 60;         /* default light 60% */
    d.backlight = 100;
    d.heartbeat = 0;             /* heartbeat disabled by default */
    d.timeSource = 0;            /* external RTC by default (runtime can select software) */
    d.activityTimeoutSec = 0;    /* 0 = default 60 s */
}

/* Standard CRC-32 (IEEE). */
uint32_t persist_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)-(int32_t)(crc & 1));
    }
    return crc ^ 0xFFFFFFFFUL;
}

void FRAM::begin() {
    m_cap = 2048;                        /* MB85RC16 */
    /* Start from defaults so an absent/unreadable FRAM still yields a sane
     * configuration (capacity, brightness, ...) instead of all-zero fields. */
    persist_defaults(m_data);

    /* manual-mode begin(): sets density=16 -> maxaddress=2048 so reads/writes
     * at any offset work; does NOT perform the failing device-ID read. */
    m_fram.begin();

    /* presence probe on the base address (with one retry for the boot transient) */
    if (!i2cBusLock(1000)) { m_present = false; return; }
    Wire.beginTransmission(FRAM_ADDR_BASE);
    m_present = (Wire.endTransmission() == 0);
    i2cBusUnlock();
    if (!m_present) {
        delay(150);
        if (!i2cBusLock(1000)) { m_present = false; return; }
        Wire.beginTransmission(FRAM_ADDR_BASE);
        m_present = (Wire.endTransmission() == 0);
        i2cBusUnlock();
    }
    if (!m_present) { LOG_WARN("FRAM not present on bus"); return; }

    /* Read + validate the versioned/CRC config, retrying through the i2c-ng
     * boot/deep-sleep-wake transient so a valid stored config is NOT lost
     * (otherwise deep-sleep wake re-initializes to defaults). */
    bool ok = false;
    for (int att = 0; att < 12 && !ok; att++) {
        if (att > 0) delay(150);
        if (!i2cBusLock(1000)) { m_valid = false; return; }
        uint8_t hbuf[kPersistHeaderSize];
        if (m_fram.readArray(0x0000, (byte)kPersistHeaderSize, hbuf) == 0) {
            PersistHeader h;
            memcpy(&h, hbuf, sizeof(h));
            if (h.magic == PERSIST_MAGIC && h.version == PERSIST_VERSION && h.length == PERSIST_LENGTH) {
                uint8_t pbuf[PERSIST_LENGTH];
                if (m_fram.readArray((uint16_t)kPersistHeaderSize, (byte)PERSIST_LENGTH, pbuf) == 0) {
                    uint32_t crc = persist_crc32(pbuf, sizeof(pbuf));
                    if (crc == h.crc) { memcpy(&m_data, pbuf, sizeof(m_data)); ok = true; }
                }
            }
        }
        i2cBusUnlock();
    }

    if (!ok) {
        /* Do NOT overwrite stored config here - the read may have failed only
         * due to the i2c-ng settling transient (deep-sleep wake). Keep defaults
         * in RAM and let reloadConfig() (post-settle) load the real data. */
        persist_defaults(m_data);
        m_valid = false;
        LOG_INFO("FRAM: config read deferred (bus settling)");
    } else {
        m_valid = true;
        LOG_INFO("FRAM: config valid (v%u)", PERSIST_VERSION);
    }
}

bool FRAM::reloadConfig() {
    if (!m_present) return false;
    bool ok1 = false;
    for (int att = 0; att < 10 && !ok1; att++) {
        if (att > 0) delay(200);
        if (!i2cBusLock(1000)) continue;
        uint8_t hbuf[kPersistHeaderSize];
        if (m_fram.readArray(0x0000, (byte)kPersistHeaderSize, hbuf) == 0) {
            PersistHeader h;
            memcpy(&h, hbuf, sizeof(h));
            if (h.magic == PERSIST_MAGIC && h.version == PERSIST_VERSION && h.length == PERSIST_LENGTH) {
                uint8_t pbuf[PERSIST_LENGTH];
                if (m_fram.readArray((uint16_t)kPersistHeaderSize, (byte)PERSIST_LENGTH, pbuf) == 0) {
                    uint32_t crc = persist_crc32(pbuf, sizeof(pbuf));
                    if (crc == h.crc) { memcpy(&m_data, pbuf, sizeof(m_data)); ok1 = true; }
                }
            }
        }
        i2cBusUnlock();
    }
    if (ok1) m_valid = true;
    return ok1;
}

/* Software clock backup: a 12-byte blob at 0x0080 (page 0, outside the
 * protected config block and safely below the RTC page at 0x0100). */
#define FRAM_TIME_OFFSET 0x0080u
#define FRAM_TIME_MAGIC  0x54494D45u   /* "TIME" */
#define FRAM_TIME_LEN    12u

bool FRAM::timeLoad(uint32_t &epoch) {
    if (!m_present) return false;
    if (!i2cBusLock(200)) return false;
    uint8_t b[FRAM_TIME_LEN];
    bool ok = (m_fram.readArray((uint16_t)FRAM_TIME_OFFSET, (byte)FRAM_TIME_LEN, b) == 0);
    i2cBusUnlock();
    if (!ok) return false;
    uint32_t magic, ep, crc;
    memcpy(&magic, b, 4);
    memcpy(&ep, b + 4, 4);
    memcpy(&crc, b + 8, 4);
    if (magic != FRAM_TIME_MAGIC) return false;
    if (crc != persist_crc32((const uint8_t *)&ep, 4)) return false;
    epoch = ep;
    return true;
}

bool FRAM::timeSave(uint32_t epoch) {
    if (!m_present) return false;
    uint8_t b[FRAM_TIME_LEN];
    uint32_t magic = FRAM_TIME_MAGIC;
    uint32_t crc = persist_crc32((const uint8_t *)&epoch, 4);
    memcpy(b, &magic, 4);
    memcpy(b + 4, &epoch, 4);
    memcpy(b + 8, &crc, 4);
    if (!i2cBusLock(500)) return false;
    bool ok = (m_fram.writeArray((uint16_t)FRAM_TIME_OFFSET, (byte)FRAM_TIME_LEN, b) == 0);
    i2cBusUnlock();
    return ok;
}

void FRAM::save() {
    if (!m_present) return;
    bool saved = false;
    for (int att = 0; att < 4 && !saved; att++) {
        if (!i2cBusLock(500)) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        uint8_t pbuf[PERSIST_LENGTH];
        memcpy(pbuf, &m_data, sizeof(m_data));
        if (m_fram.writeArray((uint16_t)kPersistHeaderSize, (byte)PERSIST_LENGTH, pbuf) != 0) {
            i2cBusUnlock(); continue;
        }
        PersistHeader h;
        h.magic = PERSIST_MAGIC;
        h.version = PERSIST_VERSION;
        h.length = PERSIST_LENGTH;
        h.crc = persist_crc32(pbuf, sizeof(pbuf));
        if (m_fram.writeArray(0x0000, (byte)kPersistHeaderSize, (uint8_t *)&h) != 0) {
            i2cBusUnlock(); continue;
        }
        i2cBusUnlock();
        saved = true;
    }
    if (!saved) LOG_WARN("FRAM save failed");
}

/* True when [addr, addr+len) overlaps the FRAM page that is addressed as
 * device 0x51 - the BM8563 RTC. Both chips ACK there, so any write would
 * corrupt the clock. */
static bool overlapsRtcPage(uint16_t addr, size_t len) {
    if (len == 0) return false;
    uint16_t end = (uint16_t)(addr + len - 1);
    return !(end < FRAM_RTC_PAGE_START || addr > FRAM_RTC_PAGE_END);
}

bool FRAM::rawReadByte(uint16_t addr, uint8_t &out) {
    if (!m_present || addr >= m_cap) return false;
    if (overlapsRtcPage(addr, 1)) return false;   /* would read the RTC */
    if (!i2cBusLock(100)) return false;
    bool r = (m_fram.readByte(addr, &out) == 0);
    i2cBusUnlock();
    return r;
}

bool FRAM::rawWriteByte(uint16_t addr, uint8_t value, bool allowReserved) {
    if (!m_present || addr >= m_cap) return false;
    if (!allowReserved && addr < kPersistTotalSize) return false; /* protected config */
    if (overlapsRtcPage(addr, 1)) return false;   /* would write the RTC */
    if (!i2cBusLock(100)) return false;
    bool r = (m_fram.writeByte(addr, value) == 0);
    i2cBusUnlock();
    return r;
}

bool FRAM::rawReadPage(uint16_t addr, uint8_t *buf, size_t len) {
    if (!m_present || addr + len > m_cap) return false;
    if (overlapsRtcPage(addr, len)) return false; /* RTC page - not FRAM data */
    if (!i2cBusLock(100)) return false;
    bool r = (m_fram.readArray(addr, (byte)len, buf) == 0);
    i2cBusUnlock();
    return r;
}

bool FRAM::rawWritePage(uint16_t addr, const uint8_t *buf, size_t len, bool allowReserved) {
    if (!m_present || addr + len > m_cap) return false;
    if (!allowReserved && addr < kPersistTotalSize) return false;
    if (overlapsRtcPage(addr, len)) return false;
    if (!i2cBusLock(100)) return false;
    bool r = (m_fram.writeArray(addr, (byte)len, const_cast<uint8_t *>(buf)) == 0);
    i2cBusUnlock();
    return r;
}

void FRAM::erase() {
    if (!m_present) return;
    if (!i2cBusLock(1000)) return;
    /* Never touch 0x0100-0x01FF: that page is addressed as 0x51 (the RTC),
     * so writing it would overwrite the clock registers. */
    for (uint16_t a = 0; a < m_cap; a++) {
        if (a >= FRAM_RTC_PAGE_START && a <= FRAM_RTC_PAGE_END) continue;
        m_fram.writeByte(a, 0xFF);
    }
    i2cBusUnlock();
    persist_defaults(m_data);
    save();
}





