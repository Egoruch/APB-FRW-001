#include "services/SleepManager.h"
#include "config/pins.h"
#include "infra/Logger.h"
#include <esp_sleep.h>
#include <esp_bt.h>

void SleepManager::begin() {
    m_wakeCause = (uint8_t)esp_sleep_get_wakeup_cause();
    /* Wake on MCU_WAKE high (ESP32-C6 pin-specific GPIO wake) + 30 s timer. */
    esp_deep_sleep_enable_gpio_wakeup((1ULL << PIN_MCU_WAKE), ESP_GPIO_WAKEUP_GPIO_HIGH);
    esp_sleep_enable_timer_wakeup(30ULL * 1000000ULL);
    if (m_wakeCause != 0) LOG_INFO("Woke from sleep, cause=%u", m_wakeCause);
}

void SleepManager::enterLightSleep(uint32_t ms) {
    if (ms > 0) esp_sleep_enable_timer_wakeup(ms * 1000ULL);
    esp_light_sleep_start();
}

void SleepManager::enterDeepSleep() {
    if (digitalRead(PIN_MCU_WAKE) == HIGH) {
        LOG_WARN("MCU_WAKE already high - not sleeping");
        return;
    }
    /* Disable Bluetooth (BT controller) before deep sleep: an active BLE/NimBLE
     * stack entering deep sleep gives an unstable GPIO/EXT wake (CPU never
     * completes boot). The working example has no BLE active -> clean wake. */
    esp_bt_controller_disable();

    esp_deep_sleep_enable_gpio_wakeup((1ULL << PIN_MCU_WAKE), ESP_GPIO_WAKEUP_GPIO_HIGH);
    /* Only the button should wake deep sleep - clear any timer armed earlier
     * (e.g. from begin()) so it does not auto-wake after ~30 s. */
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    LOG_INFO("Entering deep sleep");
    Serial.flush();
    esp_deep_sleep_start();
}

bool SleepManager::wakeWasReason() const {
    return m_wakeCause == ESP_SLEEP_WAKEUP_EXT1 ||
           m_wakeCause == ESP_SLEEP_WAKEUP_GPIO ||
           m_wakeCause == ESP_SLEEP_WAKEUP_TIMER;
}

void SleepManager::clearWakeReason() { m_wakeCause = 0; }



