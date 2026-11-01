#include "cli/commands.h"
#include "app/App.h"

static const char *kTempName[4] = {"PB1", "PB2", "ELOAD", "AIR"};

static void cmdSensors(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "list") {
        out.println("Sensors:");
        out.println("  TMP102 x4 (PB1 PB2 ELOAD AIR), INA226 x8, RTC");
        out.println("OK");
        return;
    }
    if (sub == "status" || sub == "all") {
        out.println("Temperature sensors:");
        for (int i = 0; i < 4; i++) {
            float t = app.temps.readC(i);
            if (isnan(t)) out.printf("  %-6s absent\r\n", kTempName[i]);
            else out.printf("  %-6s %.2f C\r\n", kTempName[i], t);
        }
        out.println("OK");
        return;
    }
    out.println("usage: sensor <list|status>");
}

static void cmdTemperature(Output &out, const std::vector<String> &a) {
    (void)a;
    App &app = *App::instance();
    out.println("Temperature sensors:");
    for (int i = 0; i < 4; i++) {
        float t = app.temps.readC(i);
        if (isnan(t)) out.printf("  %-6s absent\r\n", kTempName[i]);
        else out.printf("  %-6s %.2f C\r\n", kTempName[i], t);
    }
    out.printf("  %-6s %.2f C (MCU)\r\n", "MCU", temperatureRead());
    out.println("OK");
}

void cmdRegisterSensors(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("sensor", "Sensor management", "sensor <list|status>", cmdSensors);
    cli.registerCommand("temperature", "Read temperature sensor(s)", "temperature [all]", cmdTemperature);
}
