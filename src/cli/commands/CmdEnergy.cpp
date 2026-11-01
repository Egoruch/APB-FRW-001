#include "cli/commands.h"
#include "app/App.h"
#include "measurements.h"

static PowerChannel portToCh(const String &s) {
    if (s == "c1") return CH_C1;
    if (s == "c2") return CH_C2;
    if (s == "a1") return CH_A1;
    if (s == "a2") return CH_A2;
    if (s == "battery") return CH_BAT;
    if (s == "sys" || s == "system") return CH_SYS;
    return CH_COUNT;
}

static void printOne(Output &out, PowerChannel ch) {
    const char *nm = kChannelName[ch];
    out.printf("%s : %.1f mAh, %.3f Wh\r\n", nm,
               App::instance()->energy.chargeMah(ch),
               App::instance()->energy.energyWh(ch));
}

static void cmdEnergy(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "all";
    sub.toLowerCase();

    if (sub == "all") {
        out.println("Energy counters:");
        for (int i = CH_BAT; i <= CH_A2; i++) printOne(out, (PowerChannel)i);
        out.println("OK");
        return;
    }
    if (sub == "reset") {
        for (int i = CH_BAT; i <= CH_A2; i++) app.energy.resetChannel((PowerChannel)i);
        out.println("OK all counters reset");
        return;
    }
    if (sub == "battery" && a.size() >= 4 && a[2].equalsIgnoreCase("set")) {
        float wh = atof(a[3].c_str());
        if (wh < 0) wh = 0;
        app.fram.data().energyWh[CH_BAT] = wh;   /* also persists to FRAM */
        app.fram.save();
        out.printf("OK battery energy set %.2f Wh\r\n", wh);
        return;
    }
    PowerChannel ch = portToCh(sub);
    if (ch == CH_COUNT) { out.println("usage: energy <c1|c2|a1|a2|battery|sys|all|reset>"); return; }
    printOne(out, ch);
    out.println("OK");
}

void cmdRegisterEnergy(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("energy", "Port charge/energy counters", "energy <c1|c2|a1|a2|battery|sys|all|reset>", cmdEnergy);
}
