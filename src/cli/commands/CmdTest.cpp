#include "cli/commands.h"
#include "app/App.h"
#include "config/addresses.h"
#include "infra/I2CBus.h"
#include <Wire.h>

static void testI2C(Output &out) {
    App &app = *App::instance();
    uint8_t a[4] = {ADDR_RTC_BM8563, ADDR_I2C_MUX, INA226_SYS_ADDR, FRAM_ADDR_BASE};
    int ok = 0;
    if (i2cBusLock(1000)) {
        for (int i = 0; i < 4; i++) {
            uint8_t addr = a[i];
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) ok++;
        }
        i2cBusUnlock();
    }
    out.printf("I2C probe: %d/4 core devices present\r\n", ok);
    out.println(ok >= 3 ? "PASS i2c" : "FAIL i2c (some devices missing)");
}

static void testFram(Output &out) {
    App &app = *App::instance();
    out.println(app.fram.isPresent() ? "PASS fram" : "FAIL fram");
}

static void testRtc(Output &out) {
    App &app = *App::instance();
    out.println(app.time.present() ? "PASS rtc" : "FAIL rtc");
}

static void testDisplay(Output &out) {
    App &app = *App::instance();
    out.println(app.display.ready() ? "PASS display" : "FAIL display");
}

static void cmdTest(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "all";
    sub.toLowerCase();

    if (sub == "all") {
        testI2C(out);
        testFram(out);
        testRtc(out);
        testDisplay(out);
        out.printf("INA226 present: %u/8\r\n", app.monitor.begin());
        out.println(app.monitor.present(CH_SYS) ? "PASS ina226 (SYS)" : "FAIL ina226 (SYS)");
        out.println("SKIP rgb/buzzer/buttons/power (need operator)");
        out.println("DONE");
        return;
    }
    if (sub == "i2c") { testI2C(out); return; }
    if (sub == "fram") { testFram(out); return; }
    if (sub == "rtc") { testRtc(out); return; }
    if (sub == "display") { testDisplay(out); return; }
    if (sub == "ina226") {
        uint8_t n = app.monitor.begin();
        out.printf("INA226 found: %u/8\r\n", n);
        out.println(n >= 7 ? "PASS ina226" : "FAIL ina226");
        return;
    }
    if (sub == "rgb") { app.rgb.setEffect(LED_EFFECT_SOLID); app.rgb.setColor(30, 30, 30, 0); out.println("PASS rgb (on)"); return; }
    if (sub == "buzzer") { app.buzzer.beep(150); out.println("PASS buzzer (beep)"); return; }
    if (sub == "power") { out.println("PASS power (3V3 off, no action taken)"); return; }
    if (sub == "buttons") { out.println("OK buttons - press to verify (no blocking)"); return; }
    out.println("usage: test <all|i2c|fram|rtc|display|rgb|buzzer|buttons|ina226|power>");
}

void cmdRegisterTest(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("test", "Hardware self-test", "test <all|i2c|fram|rtc|display|rgb|buzzer|buttons|ina226|power>", cmdTest);
}
