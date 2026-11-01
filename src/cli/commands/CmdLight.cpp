#include "cli/commands.h"
#include "app/App.h"

static void cmdLight(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "status") {
        out.printf("Light: %u %%\r\n", app.light());
        out.println("OK");
        return;
    }
    if (sub == "off") { app.setLight(0); out.println("OK light off"); return; }

    /* light <25|50|100> or any 0..100 */
    int v = atoi(sub.c_str());
    if (String(v) == sub && v >= 0 && v <= 100) {
        app.setLight((uint8_t)v);
        app.fram.data().lightPercent = (uint8_t)v;
        app.fram.save();
        out.println("OK");
        return;
    }
    out.println("usage: light <status|off|0-100>");
}

void cmdRegisterLight(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("light", "Light control", "light <status|off|0-100>", cmdLight);
}
