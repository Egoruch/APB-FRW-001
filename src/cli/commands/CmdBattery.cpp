#include "cli/commands.h"
#include "app/App.h"

static void cmdBattery(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "status") {
        app.monitor.sampleAll();
        out.println("Battery");
        out.println("Voltage : " + f2str(app.batVoltage()) + " V");
        out.println("Current : " + f2str(app.batCurrent()) + " A");
        out.println("Power   : " + f2str(app.batPower()) + " W");
        out.println("SOC     : " + f2str(app.batSOC(), 0) + " %");
        out.println("Energy  : " + f2str(app.energyWh(CH_BAT)) + " Wh");
        out.println("Capacity: " + f2str(app.batCapacityMah(), 0) + " mAh");
        if (app.chargeOvpLatched())
            out.printf("OVP latch: ACTIVE (trip %.2f V at %lu s)\r\n",
                       app.ovpTripVoltage(), (unsigned long)(app.ovpTripMs() / 1000UL));
        else if (app.ovpTripMs())
            out.printf("OVP latch: cleared (last trip %.2f V at %lu s)\r\n",
                       app.ovpTripVoltage(), (unsigned long)(app.ovpTripMs() / 1000UL));
        else
            out.println("OVP latch: off");
        out.println("OK");
        return;
    }
    if (sub == "voltage")   { app.monitor.sampleAll(); out.println("Battery voltage: " + f2str(app.batVoltage()) + " V\r\nOK"); return; }
    if (sub == "current")   { app.monitor.sampleAll(); out.println("Battery current: " + f2str(app.batCurrent()) + " A\r\nOK"); return; }
    if (sub == "power")     { app.monitor.sampleAll(); out.println("Battery power  : " + f2str(app.batPower()) + " W\r\nOK"); return; }
    if (sub == "soc")       { out.println("Battery SOC    : " + f2str(app.batSOC(), 0) + " %\r\nOK"); return; }
    if (sub == "capacity" && a.size() >= 3) {
        float mah = atof(a[2].c_str());
        app.battery.setCapacityMah(mah);
        out.println("OK battery capacity set");
        return;
    }
    if (sub == "capacity")  { out.println("Battery capacity: " + f2str(app.batCapacityMah(), 0) + " mAh\r\nOK"); return; }
    if (sub == "energy")    { out.println("Battery energy : " + f2str(app.energyWh(CH_BAT)) + " Wh\r\nOK"); return; }
    if (sub == "reset")     { app.energy.resetChannel(CH_BAT); out.println("OK battery counters reset"); return; }
    if (sub == "rte") {
        app.monitor.sampleAll();
        float remaining = app.battery.energyRemainingWh();
        float full = app.batCapacityMah() / 1000.0f * 3.70f;
        float w  = app.batPower();
        if (fabsf(w) > 0.05f) {
            float need = (w < 0) ? (full - remaining) : remaining;
            if (need < 0) need = 0;
            out.println("RtE (Run-Time to Empty) : " + f2str(need / fabsf(w), 1) + " h\r\nOK");
        } else {
            out.println("RtE (Run-Time to Empty) : -- (charging/power-off)\r\nOK");
        }
        return;
    }
    if (sub == "recalibrate") { app.battery.recalibrate(); out.println("OK battery SOC recalibrated"); return; }
    out.println("usage: battery <status|voltage|current|power|soc|capacity|energy|rte|reset|recalibrate>");
}

void cmdRegisterBattery(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("battery", "Battery information", "battery <status|voltage|current|power|soc|capacity|energy|rte|reset|recalibrate>", cmdBattery);
}
