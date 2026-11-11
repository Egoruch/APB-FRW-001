#include "cli/commands.h"
#include "app/App.h"

static void cmdDisplay(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "status") {
        out.printf("Backlight brightness: %u %%\r\n", app.display.brightness());
        out.println("OK");
        return;
    }
    if (sub == "brightness") {
        if (a.size() >= 3) {
            String op = a[2];
            op.toLowerCase();
            if (op == "get") { out.printf("Backlight brightness: %u %%\r\nOK\r\n", app.display.brightness()); return; }
            int v = atoi(op.c_str());
            if (op == "set") { if (a.size() < 4) { out.println("ERROR need value"); return; } v = atoi(a[3].c_str()); }
            if (v < 0 || v > 100) { out.println("ERROR brightness 0..100"); return; }
            app.display.setBrightness((uint8_t)v);
            app.fram.data().displayBrightness = (uint8_t)v;
            app.fram.save();
            out.println("OK");
        } else {
            out.printf("Backlight brightness: %u %%\r\n", app.display.brightness());
            out.println("OK");
        }
        return;
    }
    if (sub == "screen") {
        if (a.size() >= 3) {
            int n = atoi(a[2].c_str());
            if (n < 1 || n > 4) { out.println("ERROR screen 1..4 (1=main 2=monitor 3=config 4=ip5356)"); return; }
            app.display.requestScreen(n);
            out.printf("OK screen %d\r\n", n);
        } else {
            out.printf("Current screen: %d\r\n", app.display.screen());
            out.println("OK");
        }
        return;
    }
    if (sub == "stats") {
        if (a.size() >= 3) {
            String op = a[2];
            op.toLowerCase();
            if (op == "on" || op == "off" || op == "1" || op == "0") {
                bool on = (op == "on" || op == "1");
                app.display.setShowStats(on);
                out.printf("OK FPS/CPU overlay %s\r\n", on ? "ON" : "off");
                return;
            }
            out.println("ERROR stats on|off");
            return;
        }
        out.printf("FPS/CPU overlay: %s\r\n", app.display.showStats() ? "ON" : "off");
        out.println("OK");
        return;
    }
    out.println("usage: display brightness <get|set 0-100> | display screen <1|2|3|4> | display stats <on|off> | display status");
}

void cmdRegisterDisplay(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("display", "Display control", "display <status|brightness <get|set 0-100>|screen <1-4>|stats <on|off>>", cmdDisplay);
}
