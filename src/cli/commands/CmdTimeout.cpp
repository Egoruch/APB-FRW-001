#include "cli/commands.h"
#include "app/App.h"

static void printTimeout(Output &out, App &app) {
    uint8_t s = app.activityTimeoutSec();
    if (s == 255)      out.println("Activity timeout: off");
    else if (s == 0)   out.println("Activity timeout: 60 s (default)");
    else               out.printf("Activity timeout: %u s\r\n", s);
}

static void cmdTimeout(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "status") { printTimeout(out, app); out.println("OK"); return; }
    if (sub == "off")    { app.setActivityTimeoutSec(255); out.println("OK activity timeout off"); return; }
    if (sub == "default"){ app.setActivityTimeoutSec(0); out.println("OK activity timeout 60 s"); return; }

    int v = atoi(sub.c_str());
    if (v < 1 || v > 254) {
        out.println("usage: timeout <1-254 seconds|off|default|status>");
        return;
    }
    app.setActivityTimeoutSec((uint8_t)v);
    out.printf("OK activity timeout %d s\r\n", v);
}

void cmdRegisterTimeout(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("timeout", "Display/RGB inactivity timeout",
                        "timeout <1-254 seconds|off|default|status>",
                        cmdTimeout);
}
