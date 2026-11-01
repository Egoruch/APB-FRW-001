#include "services/IP5356Manager.h"
#include "infra/I2CBus.h"
#include "config/pins.h"
#include "config/config.h"
#include <Wire.h>

IP5356Manager::IP5356Manager(I2CMux &mux)
    : m_mux(mux), m_ch(0), m_held(false) {
    m_present[0] = m_present[1] = false;
    m_sess = xSemaphoreCreateMutex();
}

void IP5356Manager::wake() {
    /* Mirror the verified reference IP_WAKEUP(): select the wake mux channel,
     * pull SDA low to energise the IP5356 power p-MOS via the wake line, then
     * release SDA and RE-INIT Wire(). The Wire.begin() restore is essential:
     * after the SDA pin was temporarily taken over, the ESP32-C6 i2c-ng driver
     * must be rebound to our pins or the bus is left "not initialized". */
    if (!m_sess) return;
    if (xSemaphoreTake(m_sess, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    if (i2cBusLock(1000)) {
        m_mux.setMaskLocked((uint8_t)(1 << MUX_CH_IP_WAKE));   /* select wake channel (3) */

        /* Release the i2c-ng driver BEFORE taking the pin over as plain GPIO;
         * otherwise the next bus write fails with "bus is not initialized". */
        Wire.end();
        delay(5);

        pinMode(PIN_I2C1_SDA, OUTPUT);
        digitalWrite(PIN_I2C1_SDA, LOW);
        delay(200);                                    /* P-MOSFET ON */
        pinMode(PIN_I2C1_SDA, INPUT);                  /* release SDA */

        Wire.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, I2C_BUS_SPEED);
        delay(5);

        m_mux.setMaskLocked(0);                        /* deselect all channels */

        i2cBusUnlock();
    }
    xSemaphoreGive(m_sess);   /* release before probing: presentNow() re-acquires */

    for (uint8_t ch = 0; ch < 2; ch++) m_present[ch] = presentNow(ch);
}

bool IP5356Manager::wakeHold(uint32_t ms) {
    /* Select mux channel 3 (SD3 = IP_WAKEUP) and hold SDA low so the external
     * P-MOS keeps the 5V wake path on for the requested time. Keep the I2C
     * lock for the whole hold: releasing it while Wire is de-initialised makes
     * the other tasks call Wire with a NULL buffer. */
    if (!m_sess) return false;
    if (xSemaphoreTake(m_sess, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    if (!i2cBusLock(1000)) { xSemaphoreGive(m_sess); return false; }

    bool ok = m_mux.setMaskLocked((uint8_t)(1 << MUX_CH_IP_WAKE));

    Wire.end();
    delay(5);
    pinMode(PIN_I2C1_SDA, OUTPUT);
    digitalWrite(PIN_I2C1_SDA, LOW);   /* IP_WAKEUP asserted */

    vTaskDelay(pdMS_TO_TICKS(ms));      /* bus is intentionally parked */

    pinMode(PIN_I2C1_SDA, INPUT);
    Wire.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, I2C_BUS_SPEED);
    delay(5);
    m_mux.setMaskLocked(0);            /* deselect all channels */

    i2cBusUnlock();
    xSemaphoreGive(m_sess);
    return ok;
}

void IP5356Manager::begin() {
    for (uint8_t ch = 0; ch < 2; ch++)
        m_present[ch] = presentNow(ch);
}

bool IP5356Manager::begin(uint8_t ch) {
    if (!m_sess) return false;
    /* One task owns the mux+PMIC session at a time. A second caller waits
     * briefly and then fails instead of releasing someone else's lock. */
    if (xSemaphoreTake(m_sess, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    if (!i2cBusLock(1000)) {
        xSemaphoreGive(m_sess);
        return false;
    }
    if (!m_mux.setMaskLocked((uint8_t)(1 << (ch & 1)))) {
        /* Probing an empty/sleeping mux channel wedges the ESP32-C6 i2c-ng
         * controller (ESP_ERR_INVALID_STATE). Re-init Wire once to clear the
         * bad driver state, then retry. */
        Serial.println("[ipm] i2c wedge - recovering");
        Wire.end();
        delay(5);
        Wire.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, I2C_BUS_SPEED);
        if (!m_mux.setMaskLocked((uint8_t)(1 << (ch & 1)))) {
            i2cBusUnlock();
            xSemaphoreGive(m_sess);
            return false;
        }
    }
    m_ch  = ch & 1;
    m_held = true;
    m_owner = xTaskGetCurrentTaskHandle();
    return true;
}

void IP5356Manager::end() {
    if (!m_held) return;
    /* Only the task that opened the session may close it / release the bus. */
    if (m_owner != xTaskGetCurrentTaskHandle()) return;
    /* deselect all mux channels so an empty/stub channel is never left selected */
    m_mux.setMaskLocked(0);
    /* NOTE: we deliberately do NOT re-init Wire here - doing so on every
     * transaction stalls the UI task (frame drops / high CPU). A wedge is
     * recovered lazily in begin() only when an actual mux write fails. */
    i2cBusUnlock();
    m_held = false;
    m_owner = nullptr;
    xSemaphoreGive(m_sess);
}

bool IP5356Manager::present(uint8_t ch) {
    if (ch < 2) return m_present[ch];
    return presentNow(ch);
}
