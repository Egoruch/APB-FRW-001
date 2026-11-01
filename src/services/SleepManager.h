#pragma once
#include <Arduino.h>

/* Low-power sleep handling for ESP32-C6.
 * Light sleep preserves RAM; deep sleep wakes on MCU_WAKE (GPIO0 hi) and a
 * periodic timer. Caller is responsible for saving FRAM state before sleep. */
class SleepManager {
public:
    void begin();
    void enterLightSleep(uint32_t ms = 0);
    void enterDeepSleep();                 /* wake on MCU_WAKE hi + 30s timer */
    bool wakeWasReason() const;            /* true if woke from deep sleep */
    void clearWakeReason();
    uint8_t wakeCause() const { return m_wakeCause; }
private:
    uint8_t m_wakeCause = 0;
};
