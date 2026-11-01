#include "hardware/Buzzer.h"
#include "config/pins.h"
#include "config/config.h"
#include "infra/Logger.h"

void Buzzer::begin() {
    ledcAttachChannel(PIN_BUZZER, m_freq, 8, LEDC_CH_BUZZER);
    ledcWrite(PIN_BUZZER, 0);
    m_onDuty = 0;
    LOG_INFO("Buzzer ready");
}

void Buzzer::setFrequency(uint32_t hz) {
    m_freq = (hz == 0) ? 1 : hz;
    /* apply even while silent: melodies set the next note's pitch before beep() */
    ledcChangeFrequency(PIN_BUZZER, m_freq, 8);
}

void Buzzer::setDuty(uint8_t duty) { m_dutyOn = duty; }

void Buzzer::off() {
    m_active = false;
    m_onDuty = 0;
    ledcWrite(PIN_BUZZER, 0);
}

void Buzzer::on() {
    m_active = true;
    m_onDuty = m_dutyOn;
    m_untilMs = 0xFFFFFFFFUL;            /* continuous - never auto-off */
    ledcWrite(PIN_BUZZER, m_dutyOn);     /* freq already set at attach/setFrequency */
}

void Buzzer::beep(uint32_t durationMs) {
    on();                                /* continuous drive ... */
    m_untilMs = millis() + durationMs;   /* ... but auto-off after duration */
}

void Buzzer::update() {
    if (!m_active) return;
    if (m_untilMs != 0xFFFFFFFFUL &&
        (int32_t)(millis() - m_untilMs) >= 0) off();
}

