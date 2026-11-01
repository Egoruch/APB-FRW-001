#include "cli/commands.h"
#include "app/App.h"
#include "config/pins.h"
#include "config/config.h"
#include "config/addresses.h"
#include "infra/I2CBus.h"
#include <Wire.h>

static const char *addrName(uint8_t addr) {
    static char buf[28];
    if (addr >= INA226_SYS_ADDR && addr <= INA226_ELOAD_ADDR) {
        static const char *ina[] = {"SYS", "IP1", "IP2", "C1", "C2", "A1", "A2", "ELOAD"};
        snprintf(buf, sizeof(buf), "INA226 %s", ina[addr - INA226_SYS_ADDR]);
        return buf;
    }
    if (addr == TMP102_PB1_ADDR) return "TMP102 PB1";
    if (addr == TMP102_PB2_ADDR) return "TMP102 PB2";
    if (addr == TMP102_ELOAD_ADDR) return "TMP102 ELOAD";
    if (addr == TMP102_AIR_ADDR) return "TMP102 AIR";
    if (addr == ADDR_RTC_BM8563) return "BM8563 RTC";
    if (addr == ADDR_I2C_MUX) return "PCA9546 MUX";
    if (addr == FRAM_ADDR_BASE) return "MB85RC16 FRAM";
    if (addr > FRAM_ADDR_BASE && addr <= 0x57) return "MB85RC16 FRAM alias";
    if (addr == 0x75) return "IP5356 PMIC";
    return "";
}

static void cmdI2C(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "scan") {
        out.printf("I2C bus scan (SDA=%d SCL=%d, %u kHz):\r\n", PIN_I2C1_SDA, PIN_I2C1_SCL, I2C_BUS_SPEED / 1000);
        out.println("  ADDR  Device");
        int found = 0;
        if (i2cBusLock(1000)) {
            for (uint8_t addr = 1; addr < 127; addr++) {
                Wire.beginTransmission(addr);
                if (Wire.endTransmission() == 0) {
                    const char *name = addrName(addr);
                    if (name && *name) out.printf("  0x%02X  %s\r\n", addr, name);
                    else out.printf("  0x%02X  <unknown>\r\n", addr);
                    found++;
                }
            }
            i2cBusUnlock();
        }
        out.printf("== %d device(s) found ==\r\n", found);
        out.println("OK");
        return;
    }
    if (sub == "status") {
        uint8_t mask = app.mux.currentChannel();
        out.printf("I2C: SDA=%d SCL=%d speed=%u\r\n", PIN_I2C1_SDA, PIN_I2C1_SCL, I2C_BUS_SPEED);
        out.printf("Mux present : %s\r\n", app.mux.isPresent() ? "yes" : "no");
        if (mask == 0) {
            out.println("Mux mask    : 0x00 (all channels off)");
        } else if ((mask & (mask - 1)) == 0) {
            uint8_t ch = 0;
            while ((uint8_t)(1u << ch) != mask) ch++;
            out.printf("Mux mask    : 0x%02X (channel %u)\r\n", mask, ch);
        } else {
            out.printf("Mux mask    : 0x%02X (multiple channels)\r\n", mask);
        }
        out.println("OK");
        return;
    }
    if (sub == "reset") {
        /* Re-init the driver under the lock, then release it before probing:
         * I2CMux::begin() takes the same (non-recursive) mutex itself. */
        if (i2cBusLock(1000)) {
            Wire.end();
            delay(10);
            Wire.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, I2C_BUS_SPEED);
            i2cBusUnlock();
        }
        app.mux.begin();
        out.println("OK");
        return;
    }
    if (sub == "mux" && a.size() >= 3) {
        String m = a[2]; m.toLowerCase();
        bool ok = false;
        if (m == "on" || m == "all")   ok = app.mux.setMask(0x0F);            /* all channels */
        else if (m == "off")           ok = app.mux.setMask(0x00);
        else {
            int ch = atoi(m.c_str());
            if (ch < 0 || ch > 3) { out.println("ERROR mux channel 0..3"); return; }
            ok = app.mux.selectChannel((uint8_t)ch);
        }
        if (!ok) { out.println("ERROR mux not present"); return; }
        out.println("OK");
        return;
    }
    if (sub == "mux") {
        out.printf("Mux mask: 0x%02X\r\n", app.mux.currentChannel());
        out.println("OK");
        return;
    }
    out.println("usage: i2c <scan|status|reset|mux [on|off|0-3]>");
}

void cmdRegisterI2C(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("i2c", "I2C subsystem", "i2c <scan|status|reset|mux [on|off|0-3]>", cmdI2C);
    cli.registerCommand("i2cscan", "Alias for i2c scan", "i2cscan", [](Output &out, const std::vector<String> &a) {
        std::vector<String> v{"i2c", "scan"};
        cmdI2C(out, v);
    });
}
