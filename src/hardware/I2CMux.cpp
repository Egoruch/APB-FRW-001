#include "hardware/I2CMux.h"
#include "config/addresses.h"
#include "infra/I2CBus.h"
#include <Wire.h>

I2CMux::I2CMux(uint8_t addr) : m_addr(addr), m_cur(0xFF), m_present(false) {}

bool I2CMux::begin() {
    if (!i2cBusLock(1000)) return false;
    Wire.beginTransmission(m_addr);
    m_present = (Wire.endTransmission() == 0);
    if (m_present) {
        /* select channel 0 while the bus lock is already held (no nested lock) */
        setMaskLocked((uint8_t)(1 << 0));
    }
    i2cBusUnlock();
    return m_present;
}

bool I2CMux::selectChannel(uint8_t ch) {
    if (ch > 3) return false;
    return setMask((uint8_t)(1 << ch));
}

bool I2CMux::setMaskLocked(uint8_t mask) {
    Wire.beginTransmission(m_addr);
    Wire.write(mask);
    bool ok = (Wire.endTransmission() == 0);
    if (ok) m_cur = mask;
    return ok;
}

bool I2CMux::setMask(uint8_t mask) {
    if (!i2cBusLock(100)) return false;
    bool ok = setMaskLocked(mask);
    i2cBusUnlock();
    return ok;
}
