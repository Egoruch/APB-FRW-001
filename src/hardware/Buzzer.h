#pragma once
#include <Arduino.h>

/* Non-blocking buzzer. A periodic update() call (from a task / main loop) turns
 * the tone off automatically after the requested duration. */
class Buzzer {
public:
    void begin();
    void setFrequency(uint32_t hz);   /* 0 = silence */
    void setDuty(uint8_t duty);       /* 0..255 */

    void on();
    void off();
    void beep(uint32_t durationMs);   /* non-blocking */
    void update();                    /* call periodically */

    bool isActive() const { return m_active; }

private:
    uint32_t m_freq = 2000;
    uint8_t  m_dutyOn = 127;
    int      m_onDuty = 0;
    bool     m_active = false;
    uint32_t m_untilMs = 0;
};
