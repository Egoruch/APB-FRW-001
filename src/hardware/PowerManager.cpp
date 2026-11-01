#include "hardware/PowerManager.h"
#include "config/pins.h"
#include "config/config.h"
#include "infra/Logger.h"

void PowerManager::begin() {
    pinMode(PIN_3V3_SW_EN, OUTPUT);
    /* Enable the common rail (3V3 + 5V) by default at boot. */
    digitalWrite(PIN_3V3_SW_EN, HIGH);
    m_3v3 = true;
    delay(10);   /* DC-DC settling */

    ledcAttachChannel(PIN_ELOAD_PWM, kEloadFreq, kEloadRes, LEDC_CH_ELOAD);
    setELoadPercent(0);
    LOG_INFO("PowerManager ready (rail ON by default)");
}

void PowerManager::set3V3(bool on) {
    if (on == m_3v3) return;
    digitalWrite(PIN_3V3_SW_EN, on ? HIGH : LOW);
    m_3v3 = on;
    if (on) delay(10);   /* small DC-DC settling delay, non-blocking enough */
    LOG_INFO("3V3 bus %s", on ? "enabled" : "disabled");
}

void PowerManager::setELoadPercent(float pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    m_eload = pct;
    ledcWrite(PIN_ELOAD_PWM, (uint32_t)(pct / 100.0f * kEloadMax));
}
