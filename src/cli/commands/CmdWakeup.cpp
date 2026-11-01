#include "cli/commands.h"
#include "app/App.h"
#include "config/pins.h"
#include <esp_sleep.h>

static void cmdWakeup(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "wake";
    sub.toLowerCase();

    if (sub == "status") {
        out.println("IP5356 wake status");
        out.printf("IP1 (mux 0): %s\r\n", app.ipman.present(0) ? "present" : "absent");
        out.printf("IP2 (mux 1): %s\r\n", app.ipman.present(1) ? "present" : "absent");
        out.printf("Last wake  : %d\r\n", (int)esp_sleep_get_wakeup_cause());
        out.println("OK");
        return;
    }
    if (sub == "wake" || sub == "ip") {
        app.ipman.wake();
        out.println("OK IP wake pulse sent (mux channel 3 / SD3)");
        return;
    }
    if (sub == "hold") {
        uint32_t ms = (a.size() > 2) ? (uint32_t)atol(a[2].c_str()) : 200UL;
        if (ms < 50) ms = 50;
        if (ms > 2000) ms = 2000;
        bool ok = app.ipman.wakeHold(ms);
        out.printf("%s IP wake hold %lu ms\r\n", ok ? "OK" : "ERROR", (unsigned long)ms);
        return;
    }
    if (sub == "last") {
        out.printf("Last wake cause: %d\r\n", (int)esp_sleep_get_wakeup_cause());
        out.println("OK");
        return;
    }
    out.println("usage: wakeup <wake|status|hold [50-2000]|last>");
}

void cmdRegisterWakeup(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("wakeup", "Wake IP5356 PMICs through the I2C mux",
                        "wakeup <wake|status|hold [50-2000]|last>", cmdWakeup);
}
