#include "cli/commands.h"
#include "app/App.h"

static void cmdFilter(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "on" || sub == "off" || sub == "1" || sub == "0") {
        bool on = (sub == "on" || sub == "1");
        app.monitor.setCurrentFilter(on);
        app.fram.filterSave(on);
        out.printf("OK current filter %s\r\n", on ? "ON" : "off");
        return;
    }
    if (sub == "status") {
        out.printf("Current filter: %s\r\n", app.monitor.currentFilter() ? "ON" : "off");
        out.println("OK");
        return;
    }
    out.println("usage: filter <status|on|off>");
}

void cmdRegisterFilter(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("filter", "Current smoothing filter",
                        "filter <status|on|off>", cmdFilter);
}
