#pragma once
#include <Arduino.h>

/* PCA9546A / TCA9546A 4-channel I2C multiplexer wrapper. */
class I2CMux {
public:
    explicit I2CMux(uint8_t addr);

    bool begin();
    bool selectChannel(uint8_t ch);  /* 0..3 -> enable only that channel */
    bool setMask(uint8_t mask);     /* direct channel mask: 0=off, 0x0F=all */
    /* Caller already owns the I2C bus lock (used by IP5356Manager sessions). */
    bool setMaskLocked(uint8_t mask);
    uint8_t currentChannel() const   { return m_cur; }
    bool isPresent() const           { return m_present; }

private:
    uint8_t m_addr;
    uint8_t m_cur;
    bool m_present;
};
