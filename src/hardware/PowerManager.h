#pragma once
#include <Arduino.h>

/* Centralized power-bus & controlled-load control.
 * NOTE: the enable pin (GPIO5) is COMMON for both the 3.3V and 5V buses,
 * so enabling it switches on both rails together. The rail is enabled by
 * default at boot (power to external buses is present immediately). */
class PowerManager {
public:
    void begin();

    /* Common rail enable (controls both 3V3 and 5V buses) */
    void set3V3(bool on);   /* name kept; actually drives the common rail */
    bool get3V3() const { return m_3v3; }

    /* Electronic load duty cycle 0..100 % */
    void setELoadPercent(float pct);
    float getELoadPercent() const { return m_eload; }

    /* Toggle helper returning new state */
    bool toggle3V3() { set3V3(!m_3v3); return m_3v3; }

private:
    bool m_3v3 = false;
    float m_eload = 0.0f;
    static constexpr uint16_t kEloadMax = 16383;   /* 14-bit */
    static constexpr uint32_t kEloadFreq = 1000;
    static constexpr uint8_t  kEloadRes = 14;
};
