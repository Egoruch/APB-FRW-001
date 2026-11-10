#pragma once
#include <Arduino.h>
#include "persist.h"
#include "FRAM_MB85RC_I2C.h"
#include "config/addresses.h"

/* Safe FRAM storage layer (MB85RC16, 2 KB). Stores a versioned+CRC protected
 * configuration block at the start of the device and prevents arbitrary CLI
 * writes from clobbering the protected config page. */
class FRAM {
public:
    void begin();
    bool isPresent() const { return m_present; }
    bool storageValid() const { return m_valid; }

    /* payload access */
    const PersistData &data() const { return m_data; }
    PersistData &data()             { return m_data; }

    /* write config block with fresh CRC + header */
    void save();

    /* raw, protected access for CLI `fram read/write`. Writes are only allowed
     * outside the reserved config region unless explicitly permitted. */
    bool rawReadByte(uint16_t addr, uint8_t &out);
    bool rawWriteByte(uint16_t addr, uint8_t value, bool allowReserved);
    bool rawReadPage(uint16_t addr, uint8_t *buf, size_t len);
    bool rawWritePage(uint16_t addr, const uint8_t *buf, size_t len, bool allowReserved);

    /* Re-read the config block (retries the i2c-ng settling transient).
     * Returns true if a valid config was loaded. */
    bool reloadConfig();

    /* Software-clock backup: Unix epoch stored at a safe offset with its own
     * magic+CRC (independent of the config block version). */
    bool timeLoad(uint32_t &epoch);
    bool timeSave(uint32_t epoch);

    /* Battery OVP cutoff in mV, stored separately from the config block. */
    bool ovpLoad(uint16_t &mv);
    bool ovpSave(uint16_t mv);

    /* Per-port session-energy mode, stored separately from the config block
     * (defaults to ON when no valid record exists). */
    bool sessionLoad(bool &on);
    bool sessionSave(bool on);

    void erase();   /* writes 0xFF to whole device (config reinitialized) */

    uint16_t capacity() const { return m_cap; }

private:
    /* manual-mode constructor: MB85RC16 has no device-ID, so we set density=16
     * (16 Kbit = 2 KB) to make the library compute maxaddress correctly. */
    FRAM_MB85RC_I2C m_fram{FRAM_ADDR_BASE, false, DEFAULT_WP_PIN, 16};
    bool m_present = false;
    bool m_valid = false;
    uint16_t m_cap = 0;
    PersistData m_data;
};


