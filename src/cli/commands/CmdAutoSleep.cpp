#include "cli/commands.h"
#include "app/App.h"

static void cmdAutoSleep(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "time") {
        if (a.size() < 3) { out.println("ERROR autosleep time <off|0-255>"); return; }
        String v = a[2];
        v.toLowerCase();
        int mins = v.equalsIgnoreCase("off") ? 0 : atoi(v.c_str());
        if (mins < 0 || mins > 255) { out.println("ERROR minutes 0-255"); return; }
        app.setAutoSleepMinutes((uint8_t)mins);
        out.printf("OK auto-sleep %s\r\n", mins ? "enabled" : "off");
        return;
    }
    if (sub == "voltage") {
        if (a.size() < 3) { out.println("ERROR autosleep voltage 3.00-4.20"); return; }
        float v = atof(a[2].c_str());
        if (!app.setAutoSleepVoltage(v)) { out.println("ERROR voltage 3.00-4.20"); return; }
        out.printf("OK auto-sleep voltage %.2f V\r\n", app.autoSleepVoltage());
        return;
    }
    if (sub == "status") {
        if (app.autoSleepMinutes() == 0)
            out.println("Auto-sleep: off");
        else
            out.printf("Auto-sleep: %.2f V after %u min\r\n",
                       app.autoSleepVoltage(), (unsigned)app.autoSleepMinutes());
        out.println("OK");
        return;
    }
    out.println("usage: autosleep <status|time <off|0-255>|voltage 3.00-4.20>");
}

void cmdRegisterAutoSleep(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("autosleep", "Deep sleep after unplug at high voltage",
                        "autosleep <status|time <off|0-255>|voltage 3.00-4.20>", cmdAutoSleep);
}
