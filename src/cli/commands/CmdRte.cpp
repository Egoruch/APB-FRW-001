#include "cli/commands.h"
#include "app/App.h"
#include "config/battery_config.h"
#include "measurements.h"
#include <math.h>

static void cmdRte(Output &out, const std::vector<String> &a) {
    (void)a;
    App &app = *App::instance();
    float remaining = app.battery.energyRemainingWh();
    float full = app.batCapacityMah() / 1000.0f * BATTERY_NOMINAL_VOLTAGE;
    float w = app.batPower();   /* negative = charging (see EnergyManager) */

    out.printf("Battery Wh    : %.3f remaining of %.3f\r\n", remaining, full);
    out.printf("Battery SOC   : %.0f %%\r\n", app.batSOC());
    out.printf("Battery Power : %.3f W\r\n", w);
    if (fabsf(w) > 0.05f) {
        float need = (w < 0) ? (full - remaining) : remaining;
        if (need < 0) need = 0;
        int totMin = (int)(need / fabsf(w) * 60.0f + 0.5f);
        out.printf("RtE           : %02d:%02d (%s)\r\n", totMin / 60, totMin % 60,
                   w < 0 ? "charging to full" : "discharging to empty");
    } else {
        out.println("RtE           : --");
    }
    out.println("OK");
}

void cmdRegisterRte(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("rte", "Run-time to empty/full", "rte", cmdRte);
}
