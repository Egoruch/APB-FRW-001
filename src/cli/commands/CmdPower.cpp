#include "cli/commands.h"
#include "app/App.h"

static bool parseOnOff(const String &s, bool &on) {
    String v = s;
    v.toLowerCase();
    if (v == "on" || v == "1" || v == "true") { on = true; return true; }
    if (v == "off" || v == "0" || v == "false") { on = false; return true; }
    return false;
}

static void cmdPower(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "status") {
        out.printf("Power rail (3V3+5V, common enable): %s\r\n", app.power.get3V3() ? "ON" : "OFF");
        out.printf("ELOAD   : %.1f %%\r\n", app.power.getELoadPercent());
        out.println("OK");
        return;
    }
    if (sub == "all") {
        /* common enable drives both 3V3 and 5V buses */
        if (a.size() >= 3) {
            bool on;
            if (!parseOnOff(a[2], on)) { out.println("ERROR bad argument (on/off)"); return; }
            app.power.set3V3(on);
            out.printf("OK common rail (3V3+5V) %s\r\n", on ? "enabled" : "disabled");
        } else {
            out.println("usage: power all <on|off>");
        }
        return;
    }
    if ((sub == "3v3" || sub == "5v") && a.size() >= 3) {
        /* 3V3 and 5V share the same enable pin - drive the common rail */
        bool on;
        if (!parseOnOff(a[2], on)) { out.println("ERROR bad argument (on/off)"); return; }
        app.power.set3V3(on);
        out.printf("OK common rail (3V3+5V) %s (enable pin shared)\r\n", on ? "enabled" : "disabled");
        return;
    }
    if (sub == "eload") {
        if (a.size() >= 3) {
            float p = atof(a[2].c_str());
            if (p < 0 || p > 100) { out.println("ERROR invalid % (0..100)"); return; }
            app.power.setELoadPercent(p);
            out.println("OK");
        } else {
            out.printf("ELOAD duty: %.1f %%\r\n", app.power.getELoadPercent());
            out.println("OK");
        }
        return;
    }
    out.println("usage: power <status|all on/off|3v3 on/off|5v on/off|eload [0-100]>");
}

void cmdRegisterPower(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("power", "Power control", "power <status|all on/off|3v3 on/off|5v on/off|eload [0-100]>", cmdPower);
}
