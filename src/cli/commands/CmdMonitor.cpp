#include "cli/commands.h"
#include "app/App.h"
#include "config/config.h"
#include "measurements.h"

static void printChannel(Output &out, PowerChannel ch, const char *name) {
    const PowerMeasurement &pm = App::instance()->monitor.get(ch);
    out.printf("%-4s %8.2fV %8.2fA %8.2fW\r\n", name, pm.voltage, pm.current, pm.power);
}

static void cmdMonitor(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "all";
    sub.toLowerCase();

    if (sub == "all") {
        app.monitor.sampleAll();
        out.printf("%-4s %9s %9s %9s\r\n", "BUS", "V", "A", "W");
        out.printf("%-4s %8.2fV %8.2fA %8.2fW\r\n", "BAT",
                   app.batVoltage(), app.batCurrent(), app.batPower());
        printChannel(out, CH_SYS, "SYS");
        printChannel(out, CH_C1, "C1");
        printChannel(out, CH_C2, "C2");
        printChannel(out, CH_A1, "A1");
        printChannel(out, CH_A2, "A2");
        out.println("OK");
        return;
    }
    if (sub == "battery") {
        app.monitor.sampleAll();
        out.println("Battery");
        out.println("Voltage : " + f2str(app.batVoltage()) + " V");
        out.println("Current : " + f2str(app.batCurrent()) + " A");
        out.println("Power   : " + f2str(app.batPower()) + " W");
        out.println("OK");
        return;
    }
    if (sub == "efficiency") {
        app.monitor.sampleAll();
        auto eff = [](float n, float d) {
            if (n < 0.0f) n = -n;
            if (d < 0.0f) d = -d;
            if (n < EFF_MIN_LOAD_W || d <= 0.05f) return String("0.00 %");
            float e = n / d;
            if (e > 1.0f) e = 1.0f / e;
            return String(f2str(e * 100.0f, 2) + " %");
        };
        auto effDen = [](float rail, float bat) {
            float r = rail < 0.0f ? -rail : rail;
            return r > 0.05f ? rail : bat;
        };
        float batP = app.batPower();
        const PowerMeasurement &c1 = app.monitor.get(CH_C1);
        const PowerMeasurement &c2 = app.monitor.get(CH_C2);
        bool c1In = (c1.voltage > 3.7f) && (c1.current > 0.05f);
        bool c2In = (c2.voltage > 3.7f) && (c2.current > 0.05f);
        float chgP = -batP; if (chgP < 0.0f) chgP = 0.0f;
        const PowerMeasurement &a1 = app.monitor.get(CH_A1);
        const PowerMeasurement &a2 = app.monitor.get(CH_A2);
        float c1Den = fabsf(c1.power), c2Den = fabsf(c2.power);
        float outAll = fabsf(a1.power) + fabsf(a2.power);
        float c1Num = chgP + outAll, c2Num = chgP + outAll;
        if (c1Num > c1Den) c1Num = c1Den;
        if (c2Num > c2Den) c2Num = c2Den;
        out.println("Efficiency");
        out.println("C1  (in: total)  : " + eff(c1In ? c1Num : c1.power,
                         c1In ? c1Den : effDen(app.monitor.get(CH_IP1).power, batP)));
        out.println("C2  (in: total)  : " + eff(c2In ? c2Num : c2.power,
                         c2In ? c2Den : effDen(app.monitor.get(CH_IP2).power, batP)));
        out.println("A1  (A1/C1 pass) : " + eff(a1.power,
                         c1In ? c1Den : effDen(app.monitor.get(CH_IP1).power, batP)));
        out.println("A2  (A2/C2 pass) : " + eff(a2.power,
                         c2In ? c2Den : effDen(app.monitor.get(CH_IP2).power, batP)));
        out.println("OK");
        return;
    }
    /* single non-battery channel */
    PowerChannel ch = CH_SYS;
    if (sub == "system") ch = CH_SYS;
    else if (sub == "c1") ch = CH_C1;
    else if (sub == "c2") ch = CH_C2;
    else if (sub == "a1") ch = CH_A1;
    else if (sub == "a2") ch = CH_A2;
    else {
        out.println("usage: monitor <all|battery|efficiency|system|c1|c2|a1|a2>");
        return;
    }
    app.monitor.sampleChannel(ch);
    printChannel(out, ch, kChannelName[ch]);
    out.println("OK");
}

void cmdRegisterMonitor(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("monitor", "Power monitoring", "monitor <all|battery|efficiency|system|c1|c2|a1|a2>", cmdMonitor);
}
