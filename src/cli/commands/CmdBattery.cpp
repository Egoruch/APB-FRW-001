#include "cli/commands.h"
#include "app/App.h"
#include "config/battery_config.h"

static void cmdBattery(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "status") {
        app.monitor.sampleAll();
        out.println("Battery");
        out.println("Voltage : " + f2str(app.batVoltage()) + " V");
        out.println("Current : " + f2str(app.batCurrentCharge()) + " A");
        out.println("Power   : " + f2str(app.batPowerCharge()) + " W");
        out.println("SOC     : " + f2str(app.batSOC(), 0) + " %");
        if (app.battery.socMethod() == BatteryManager::SOC_ENERGY) {
            if (app.battery.fullCapacityMah() > 1.0f)
                out.printf("SOC method: energy (full %.0f mAh, counted %.0f mAh)\r\n",
                           app.battery.fullCapacityMah(), app.battery.countedMah());
            else
                out.printf("SOC method: energy (not learned, counted %.0f mAh, using voltage)\r\n", app.battery.countedMah());
        } else
            out.println("SOC method: voltage");
        out.println("Energy  : " + f2str(app.energyWh(CH_BAT)) + " Wh (net pack)");
        out.println("Capacity: " + f2str(app.batCapacityMah(), 0) + " mAh");
        out.println("Vbat min/max: " + f2str(app.batVMin()) + " / " + f2str(app.batVMax()) + " V");
        if (app.chargeOvpLatched())
            out.printf("OVP latch: ACTIVE (trip %.2f V at %lu s)\r\n",
                       app.ovpTripVoltage(), (unsigned long)(app.ovpTripMs() / 1000UL));
        else if (app.ovpTripMs())
            out.printf("OVP latch: cleared (last trip %.2f V at %lu s)\r\n",
                       app.ovpTripVoltage(), (unsigned long)(app.ovpTripMs() / 1000UL));
        else
            out.println("OVP latch: off");
        out.printf("OVP cutoff: %.2f V\r\n", app.ovpCutoffVoltage());
        out.println("OK");
        return;
    }
    if (sub == "voltage")   { app.monitor.sampleAll(); out.println("Battery voltage: " + f2str(app.batVoltage()) + " V\r\nOK"); return; }
    if (sub == "current")   { app.monitor.sampleAll(); out.println("Battery current: " + f2str(app.batCurrentCharge()) + " A\r\nOK"); return; }
    if (sub == "power")     { app.monitor.sampleAll(); out.println("Battery power  : " + f2str(app.batPowerCharge()) + " W\r\nOK"); return; }
    if (sub == "monitor") {
        int sec = (a.size() >= 3) ? atoi(a[2].c_str()) : 10;
        if (sec < 1) sec = 1;
        if (sec > 60) sec = 60;
        out.println("Battery monitor (1 Hz):");
        float mn = 1e9f, mx = -1e9f;
        for (int i = 0; i < sec; i++) {
            app.monitor.sampleAll();
            float v = app.batVoltage();
            float c = app.batCurrentCharge();
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            out.printf("  %2ds  %.3f V  %+6.2f A\r\n", i + 1, v, c);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        out.printf("min %.3f V  max %.3f V  delta %.3f V\r\n", mn, mx, mx - mn);
        out.println("OK");
        return;
    }
    if (sub == "soc") {
        if (a.size() >= 3) {
            String m = a[2];
            m.toLowerCase();
            if (m == "status") {
                if (app.battery.socMethod() == BatteryManager::SOC_ENERGY) {
                    if (app.battery.fullCapacityMah() > 1.0f)
                        out.printf("SOC method: energy (full %.0f mAh, counted %.0f mAh)\r\n",
                                   app.battery.fullCapacityMah(), app.battery.countedMah());
                    else
                        out.printf("SOC method: energy (not learned, counted %.0f mAh, using voltage)\r\n", app.battery.countedMah());
                } else {
                    out.println("SOC method: voltage");
                }
                out.println("OK");
                return;
            }
            if (m == "voltage" || m == "v") {
                app.battery.setSocMethod(BatteryManager::SOC_VOLTAGE);
                out.println("OK SOC method: voltage");
                return;
            }
            if (m == "energy" || m == "coulomb" || m == "wh") {
                app.battery.setSocMethod(BatteryManager::SOC_ENERGY);
                out.println("OK SOC method: energy");
                return;
            }
            out.println("ERROR battery soc <status|voltage|energy>");
            return;
        }
        out.printf("Battery SOC    : %.0f %% (%s)\r\n", app.batSOC(),
                   app.battery.socMethod() == BatteryManager::SOC_ENERGY ? "energy" : "voltage");
        out.println("OK");
        return;
    }
    if (sub == "capacity" && a.size() >= 3) {
        if (a[2].equalsIgnoreCase("measure")) {
            float wh = app.energyWh(CH_BAT);
            if (wh < 5.0f) { out.println("ERROR battery energy too low"); return; }
            float mah = wh * 1000.0f / BATTERY_NOMINAL_VOLTAGE;
            app.battery.setCapacityMah(mah);
            out.printf("OK capacity = %.1f Wh / 3.7V = %.0f mAh\r\n", wh, mah);
            return;
        }
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
        float full = app.batFullEnergyWh();
        if (full < remaining) full = remaining;
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
    if (sub == "ovp") {
        if (a.size() < 3 || a[2].equalsIgnoreCase("status")) {
            out.printf("OVP cutoff: %.2f V (%s)\r\n", app.ovpCutoffVoltage(),
                       app.chargeOvpLatched() ? "latched" : "armed");
            out.println("OK");
            return;
        }
        float v = 0.0f;
        if (a[2].equalsIgnoreCase("set")) {
            if (a.size() < 4) { out.println("usage: battery ovp <3.00-4.20|set 3.00-4.20>"); return; }
            v = atof(a[3].c_str());
        } else {
            v = atof(a[2].c_str());
        }
        if (!app.setOvpCutoffVoltage(v)) {
            out.println("ERROR OVP cutoff must be 3.00-4.20 V");
            return;
        }
        out.printf("OK OVP cutoff %.2f V\r\n", app.ovpCutoffVoltage());
        return;
    }
    out.println("usage: battery <status|voltage|current|power|soc|capacity|energy|rte|reset|recalibrate|ovp|monitor [sec]>");
}

void cmdRegisterBattery(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("battery", "Battery information", "battery <status|voltage|current|power|soc|capacity|energy|rte|reset|recalibrate|ovp|monitor [sec]>", cmdBattery);
}
