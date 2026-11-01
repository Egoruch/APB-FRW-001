#pragma once
#include "IP5356.h"
#include "config/addresses.h"
#include "hardware/I2CMux.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/* -------------------------------------------------------------------------
 * IP5356Manager - owns the two IP5356 PMICs that sit BEHIND the PCA9546A
 * I2C mux (both share the same I2C address 0x75):
 *     channel 0 -> IP1   (per schematic, connected to SD0/SD1)
 *     channel 1 -> IP2   (per schematic, connected to SD1/SC1)
 *
 * The mux is handled HERE (independently of the IP5356 library), together
 * with the shared-I2C-bus lock, so callers can safely:
 *     if (ipman.begin(ch)) { ... ipman.current().getX() ...; ipman.end(); }
 * ------------------------------------------------------------------------- */
class IP5356Manager {
public:
    IP5356Manager(I2CMux &mux);

    void begin();                       /* probe presence on both channels */

    /* Pulse the IP5356 wake line (mux channel 3 + pull SDA low for 200ms) to
     * bring the PMICs out of standby. Idle IP5356s stop ACKing I2C; they must
     * be woken before they will respond. Mirrors the verified reference's
     * IP_WAKEUP(). */
    void wake();

    /* Hold the IP_WAKEUP line (mux SD3 / channel 3) asserted for `ms` so the
     * external 5V wake path stays on. Releases and re-inits the bus after.
     * Returns false if the mux could not be selected. */
    bool wakeHold(uint32_t ms);

    /* Acquire the bus lock and route the mux to `ch` (0=IP1, 1=IP2).
     * Returns false if lock fails or the mux is not present. Caller MUST
     * call end() before returning. */
    bool begin(uint8_t ch);
    void end();

    IP5356 &current()               { return m_ip; }
    void setAddress(uint8_t a)      { m_ip.setAddress(a); }   /* dial 0x74/0x75 */
    uint8_t address() const         { return m_ip.address(); }
    bool present(uint8_t ch);       /* probe with lock+mux */

    /* helpers for probing */
    bool presentNow(uint8_t ch)     { if (!begin(ch)) return false; bool p = m_ip.isConnected(); end(); return p; }

private:
    I2CMux  &m_mux;
    uint8_t  m_ch;
    bool     m_held;
    SemaphoreHandle_t m_sess = nullptr;   /* one task owns a begin/end session */
    TaskHandle_t      m_owner = nullptr;
    IP5356   m_ip{ADDR_IP5356_CTL, ADDR_IP5356};   /* control bank 0x74, data bank 0x75 */
    bool     m_present[2];
};
