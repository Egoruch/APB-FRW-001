#include "cli/commands.h"
#include "app/App.h"

static void cmdChargeFull(Output &out, const std::vector<String> &a) {
    (void)a;
    App *app = App::instance();
    if (!app) { out.println("ERROR no app"); return; }
    app->startChargeDoneAlarm();
    out.println("OK charge-full alarm triggered (green light 2.5 s + single beep)");
}

void cmdRegisterCharge(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("chargefull",
                        "Imitate battery-full alarm",
                        "chargefull",
                        cmdChargeFull);
}
