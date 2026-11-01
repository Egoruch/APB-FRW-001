#include "cli/commands.h"
#include "app/App.h"
#include "measurements.h"

static void cmdEload(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "status") {
        const PowerMeasurement &el = app.monitor.get(CH_ELOAD);
        out.printf("running   : %s\r\n", app.eloadRunning() ? "yes" : "no");
        out.printf("percent   : %.1f %%\r\n", app.power.getELoadPercent());
        out.printf("power(0x47): %.3f W\r\n", el.power);
        out.printf("voltage   : %.3f V\r\n", el.voltage);
        out.printf("current   : %.3f A\r\n", el.current);
        out.printf("resistance: %.1f mOhm\r\n", app.eloadR());
        out.printf("temp(0x4A) : %.1f C\r\n", app.eloadTemp());
        out.println("OK");
        return;
    }
    if (sub == "sweep" || sub == "test") {
        if (!app.startEloadSweep()) {
            out.println("ERROR eload test busy / battery low / ELOAD hot");
            return;
        }
        out.println("OK eload test started (resistance sweep)");
        return;
    }
    if (sub == "off") {
        app.stopEloadSweep();
        app.eloadSetPercent(0);
        out.println("OK eload off");
        return;
    }

    for (unsigned k = 0; k < sub.length(); k++) {
        char c = sub[k];
        if (!(isdigit(c) || c == '.' || c == '-')) {
            out.println("usage: eload <status|test|sweep|off|0-100>");
            return;
        }
    }
    float p = sub.toFloat();
    if (p < 0.0f) p = 0.0f;
    if (p > 100.0f) p = 100.0f;
    app.eloadSetPercent(p);
    out.printf("OK eload set %.1f %%\r\n", p);
}

void cmdRegisterEload(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("eload", "Electronic load (ELOAD)",
                        "eload <status|test|sweep|off|0-100>", cmdEload);
}
