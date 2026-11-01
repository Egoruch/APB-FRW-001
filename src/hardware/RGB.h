#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/* RGBW addressable LED subsystem (14x SK6812 on GPIO14).
 * Effects are refreshed by a periodic update() so nothing blocks. */
enum LedEffect {
    LED_EFFECT_OFF = 0,
    LED_EFFECT_SOLID,
    LED_EFFECT_BLINK,
    LED_EFFECT_PULSE,
    LED_EFFECT_WARNING,   /* red blink, overrides until cancelled */
    LED_EFFECT_CHARGING,
    LED_EFFECT_DISCHARGING,
    LED_EFFECT_ERROR,
    LED_EFFECT_LOW_BATTERY,
    LED_EFFECT_COUNT
};

class RGB {
public:
    RGB();
    void begin();

    void setOn();                       /* reapply current effect */
    void off();
    void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);
    void setBrightness(uint8_t pct);    /* 0..100 */
    void setEffect(LedEffect e);

    /* red warning overrides normal effects until cancelled */
    void setWarning(bool on) { m_warning = on; }
    bool warning() const { return m_warning; }

    LedEffect effect() const { return m_effect; }
    void update();                      /* refresh blink/pulse; call periodically */

    /* periodic dim-red low-battery flash; stays active until cancelled */
    void setLowBattery(bool on);
    bool lowBattery() const { return m_lowBat; }

    /* timed green charge-complete overlay; previous effect is untouched */
    void setChargeDone(uint32_t durationMs);
    bool chargeDone() const { return m_chargeDone; }

    /* idle blanking: suppresses normal effects, warning/low-battery still show */
    void setIdle(bool on);
    bool idle() const { return m_idle; }

    /* force all LEDs off for deep sleep (clears overlays too) */
    void powerOff();

    /* last-LED heartbeat (a 1 Hz green pulse on LED #13, on until disabled) */
    void setHeartbeat(bool on);
    bool heartbeat() const { return m_heartbeat; }

    /* short one-shot blink on LED #14 only (CLI activity feedback):
     * tx = light blue, rx = light yellow. Supersedes the effect briefly. */
    void flashActivity(bool tx);

private:
    Adafruit_NeoPixel m_strip;
    LedEffect m_effect = LED_EFFECT_OFF;
    bool m_warning = false;
    bool m_lowBat = false;
    uint32_t m_lowBatNextMs = 0;
    uint32_t m_lowBatFlashUntil = 0;
    bool m_chargeDone = false;
    uint32_t m_chargeDoneUntil = 0;
    bool m_idle = false;
    uint8_t m_r = 0, m_g = 0, m_b = 255, m_w = 0;
    uint8_t m_brightPct = 100;
    uint32_t m_lastMs = 0;
    bool m_phase = false;
    uint32_t m_phaseMs = 400;

    bool m_heartbeat = false;
    bool m_hbOn = false;
    uint32_t m_hbNextMs = 0;       /* next heartbeat edge (on->off or off->on) */

    bool m_flashBusy = false;      /* one-shot CLI blink in progress */
    bool m_flashTx = false;        /* tx = light blue, rx = light yellow */
    uint32_t m_flashEndMs = 0;
    struct FlashEv { bool tx; uint32_t end; };
    FlashEv m_flashQ[4];           /* ordered flash queue (rx then tx) */
    int m_flashN = 0;

    void applyAll(uint8_t r, uint8_t g, uint8_t b, uint8_t w);
    void render();

    SemaphoreHandle_t m_mtx = nullptr;   /* serialises strip state + show() */
    bool lock();
    void unlock();
};
