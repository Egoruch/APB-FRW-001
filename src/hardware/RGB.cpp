#include "hardware/RGB.h"
#include "config/pins.h"
#include "config/config.h"
#include "config/battery_config.h"
#include "infra/Logger.h"

#define RGB_LED_COUNT 14
#define RGB_HB_LED    (RGB_LED_COUNT - 1)   /* last addressable LED (index 13) */

/* CLI activity blink duration per edge (halved from 150 ms per user request) */
#define RGB_FLASH_MS  75

RGB::RGB() : m_strip(RGB_LED_COUNT, PIN_LED_ADDR, NEO_GRBW + NEO_KHZ800) {
    m_mtx = xSemaphoreCreateMutex();
}

bool RGB::lock() {
    return m_mtx && xSemaphoreTake(m_mtx, pdMS_TO_TICKS(100)) == pdTRUE;
}

void RGB::unlock() {
    if (m_mtx) xSemaphoreGive(m_mtx);
}

void RGB::begin() {
    if (!lock()) return;
    m_strip.begin();
    m_strip.setBrightness(255);
    m_strip.show();
    unlock();
    LOG_INFO("RGB ready");
}

/* base effect colour applied to every LED (last LED may be overwritten below) */
static void effectColor(bool phase, LedEffect eff,
                        uint8_t r, uint8_t g, uint8_t b, uint8_t w,
                        uint8_t &or_, uint8_t &og, uint8_t &ob, uint8_t &ow) {
    or_ = 0; og = 0; ob = 0; ow = 0;
    switch (eff) {
        case LED_EFFECT_SOLID:  or_ = r; og = g; ob = b; ow = w; break;
        case LED_EFFECT_BLINK:  if (phase) { or_ = r; og = g; ob = b; ow = w; } break;
        case LED_EFFECT_PULSE: {
            float k = phase ? 1.0f : 0.25f;
            or_ = (uint8_t)(r * k); og = (uint8_t)(g * k);
            ob = (uint8_t)(b * k); ow = (uint8_t)(w * k);
            break;
        }
        case LED_EFFECT_WARNING:
        case LED_EFFECT_ERROR:  or_ = phase ? 60 : 0; break;
        case LED_EFFECT_CHARGING:       og = phase ? 40 : 8; break;
        case LED_EFFECT_DISCHARGING:    or_ = phase ? 0 : 20; og = phase ? 100 : 10; break;
        case LED_EFFECT_LOW_BATTERY:    or_ = phase ? 60 : 0; break;
        case LED_EFFECT_OFF:
        default: break;
    }
}

static uint32_t effectPhaseMs(LedEffect eff) {
    switch (eff) {
        case LED_EFFECT_BLINK:        return 400;
        case LED_EFFECT_PULSE:        return 280;
        case LED_EFFECT_WARNING:      return 250;
        case LED_EFFECT_CHARGING:     return 350;
        case LED_EFFECT_DISCHARGING:  return 500;
        case LED_EFFECT_ERROR:        return 120;
        case LED_EFFECT_LOW_BATTERY:  return 600;
        default:                      return 250;   /* solid/off: slow idle tick */
    }
}

void RGB::render() {
    uint8_t r = 0, g = 0, b = 0, w = 0;
    uint32_t now = millis();
    bool lowFlash = m_lowBat && m_lowBatFlashUntil &&
                    (int32_t)(m_lowBatFlashUntil - now) > 0;
    bool chargeDone = m_chargeDone &&
                      (int32_t)(m_chargeDoneUntil - now) > 0;

    if (m_warning) {
        r = m_phase ? 60 : 0;       /* red warning overrides normal effects */
    } else if (m_lowBat) {
        /* dim red only during the 2 s flash window; otherwise off */
        if (lowFlash) r = BATTERY_LOW_RGB_LEVEL;
    } else if (chargeDone) {
        g = CHARGE_DONE_GREEN_LEVEL;   /* 20% green */
    } else if (m_idle) {
        /* display/RGB idle blanking */
    } else {
        effectColor(m_phase, m_effect, m_r, m_g, m_b, m_w, r, g, b, w);
    }

    for (int i = 0; i < m_strip.numPixels(); i++)
        m_strip.setPixelColor(i, r, g, b, w);

    /* last LED is reserved: queued CLI activity blink takes priority, else heartbeat.
     * The head of the queue is whichever flash window covers "now". */
    while (m_flashN > 0 && (int32_t)(now - m_flashQ[0].end) >= 0) {
        for (int i = 0; i < m_flashN - 1; i++) m_flashQ[i] = m_flashQ[i + 1];
        m_flashN--;
    }
    bool suppressTail = m_lowBat || chargeDone || m_idle;
    if (!suppressTail) {
        if (m_flashN > 0) {
            bool tx = m_flashQ[0].tx;
            uint8_t fr = tx ? 0x90 : 0xF0;          /* tx light blue / rx light yellow */
            uint8_t fb = tx ? 0xFF : 0x30;
            m_strip.setPixelColor(RGB_HB_LED, fr, 0xC0, fb, 0);
        } else if (m_heartbeat) {
            m_strip.setPixelColor(RGB_HB_LED, 0, m_hbOn ? 60 : 0, 0, 0);
        }
    }

    m_strip.show();
}

void RGB::setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (!lock()) return;
    m_r = r; m_g = g; m_b = b; m_w = w;
    if (m_effect == LED_EFFECT_SOLID || m_effect == LED_EFFECT_OFF) render();
    unlock();
}

void RGB::setBrightness(uint8_t pct) {
    if (!lock()) return;
    if (pct > 100) pct = 100;
    m_brightPct = pct;
    m_strip.setBrightness((uint8_t)(255UL * pct / 100UL));
    render();
    unlock();
}

void RGB::setEffect(LedEffect e) {
    if (!lock()) return;
    m_effect = e;
    m_lastMs = millis();
    m_phase = false;
    render();
    unlock();
}

void RGB::setLowBattery(bool on) {
    if (!lock()) return;
    if (on == m_lowBat) { unlock(); return; }
    m_lowBat = on;
    uint32_t now = millis();
    if (on) {
        m_lowBatFlashUntil = now + BATTERY_LOW_RGB_FLASH_MS;
        m_lowBatNextMs = now + BATTERY_LOW_RGB_PERIOD_MS;
    } else {
        m_lowBatFlashUntil = 0;
        m_lowBatNextMs = 0;
    }
    m_lastMs = now;
    m_phase = false;
    render();
    unlock();
}

void RGB::setChargeDone(uint32_t durationMs) {
    if (!lock()) return;
    m_chargeDone = true;
    m_chargeDoneUntil = millis() + durationMs;
    render();
    unlock();
}

void RGB::setIdle(bool on) {
    if (!lock()) return;
    if (on == m_idle) { unlock(); return; }
    m_idle = on;
    render();
    unlock();
}

void RGB::powerOff() {
    if (!lock()) return;
    m_effect = LED_EFFECT_OFF;
    m_warning = false;
    m_lowBat = false;
    m_chargeDone = false;
    m_idle = true;
    render();
    unlock();
}

void RGB::off() {
    if (!lock()) return;
    m_effect = LED_EFFECT_OFF;
    m_warning = false;
    render();
    unlock();
}

void RGB::setOn() {
    if (!lock()) return;
    render();
    unlock();
}

void RGB::setHeartbeat(bool on) {
    if (!lock()) return;
    if (on == m_heartbeat) { unlock(); return; }
    m_heartbeat = on;
    if (on) {
        m_hbOn = true;                          /* start with a visible pulse */
        m_hbNextMs = millis() + 150;            /* 150 ms on, then 850 ms off = 1 Hz */
    } else {
        m_hbOn = false;
    }
    render();
    unlock();
}

void RGB::flashActivity(bool tx) {
    /* enqueue a 75 ms blink on LED #14. rx (yellow) is queued before tx (blue)
     * so both edges are visible in order. */
    if (!lock()) return;
    if (m_flashN < 4) {
        uint32_t now = millis();
        uint32_t start = (m_flashN > 0) ? m_flashQ[m_flashN - 1].end : now;
        if ((int32_t)(start - now) < 0) start = now;     /* previous ones already expired */
        m_flashQ[m_flashN++] = {tx, start + RGB_FLASH_MS};
        render();
    }
    unlock();
}

void RGB::update() {
    if (!lock()) return;
    uint32_t now = millis();
    bool changed = false;

    /* periodic low-battery flash: RED for 2 s, then off until the next 5 min */
    if (m_lowBat) {
        if (m_lowBatFlashUntil && (int32_t)(now - m_lowBatFlashUntil) >= 0) {
            m_lowBatFlashUntil = 0;
            changed = true;
        }
        if (!m_lowBatFlashUntil && (int32_t)(now - m_lowBatNextMs) >= 0) {
            m_lowBatFlashUntil = now + BATTERY_LOW_RGB_FLASH_MS;
            m_lowBatNextMs = now + BATTERY_LOW_RGB_PERIOD_MS;
            changed = true;
        }
    }

    /* timed charge-complete overlay */
    if (m_chargeDone && (int32_t)(now - m_chargeDoneUntil) >= 0) {
        m_chargeDone = false;
        changed = true;
    }

    /* heartbeat: continuous 1 Hz pulse (150 ms on / 850 ms off) */
    if (m_heartbeat) {
        if ((int32_t)(now - m_hbNextMs) >= 0) {
            m_hbNextMs = now + (m_hbOn ? 850 : 150);
            m_hbOn = !m_hbOn;
            changed = true;
        }
    } else if (m_hbOn) {
        m_hbOn = false;
        changed = true;
    }

    /* expire the front CLI blink so the heartbeat/effect resumes */
    if (m_flashN > 0 && (int32_t)(now - m_flashQ[0].end) >= 0) {
        for (int i = 0; i < m_flashN - 1; i++) m_flashQ[i] = m_flashQ[i + 1];
        m_flashN--;
        changed = true;
    }

    /* advance the effect phase on its own cadence */
    uint32_t pm = effectPhaseMs(m_warning ? LED_EFFECT_WARNING
                                         : (m_lowBat ? LED_EFFECT_LOW_BATTERY : m_effect));
    if ((int32_t)(now - m_lastMs) >= (int32_t)pm) {
        m_lastMs = now;
        m_phase = !m_phase;
        changed = true;
    }

    if (changed) render();
    unlock();
}
