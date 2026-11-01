#include "cli/commands.h"
#include "app/App.h"

static const char *effectName(LedEffect e) {
    switch (e) {
        case LED_EFFECT_OFF: return "off";
        case LED_EFFECT_SOLID: return "solid";
        case LED_EFFECT_BLINK: return "blink";
        case LED_EFFECT_PULSE: return "pulse";
        case LED_EFFECT_WARNING: return "warning";
        case LED_EFFECT_CHARGING: return "charging";
        case LED_EFFECT_DISCHARGING: return "discharging";
        case LED_EFFECT_ERROR: return "error";
        case LED_EFFECT_LOW_BATTERY: return "low-battery";
        default: return "?";
    }
}

static void cmdRgb(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "status") {
        out.printf("RGB effect: %s\r\n", effectName(app.rgb.effect()));
        out.printf("Warning   : %s\r\n", app.rgb.warning() ? "on" : "off");
        out.printf("Low batt  : %s\r\n", app.rgb.lowBattery() ? "on" : "off");
        out.printf("Charge done: %s\r\n", app.rgb.chargeDone() ? "on" : "off");
        out.printf("Idle      : %s\r\n", app.rgb.idle() ? "on" : "off");
        out.printf("Heartbeat : %s\r\n", app.rgb.heartbeat() ? "on" : "off");
        out.println("OK");
        return;
    }
    if (sub == "off") { app.rgb.off(); out.println("OK rgb off"); return; }
    if (sub == "on")  { app.rgb.setColor(255, 255, 255, 255); app.rgb.setWarning(false); app.rgb.setEffect(LED_EFFECT_SOLID); app.rgb.setOn(); out.println("OK rgb on (max)"); return; }
    if (sub == "set" && a.size() >= 5) {
        int rr = atoi(a[2].c_str());
        int gg = atoi(a[3].c_str());
        int bb = atoi(a[4].c_str());
        int ww = (a.size() >= 6) ? atoi(a[5].c_str()) : 0;
        if (rr < 0 || rr > 255 || gg < 0 || gg > 255 || bb < 0 || bb > 255 || ww < 0 || ww > 255) {
            out.println("ERROR each component 0..255");
            return;
        }
        app.rgb.setColor((uint8_t)rr, (uint8_t)gg, (uint8_t)bb, (uint8_t)ww);
        app.rgb.setEffect(LED_EFFECT_SOLID);
        out.println("OK");
        return;
    }
    if (sub == "brightness" && a.size() >= 3) {
        int v = atoi(a[2].c_str());
        if (v < 0 || v > 100) { out.println("ERROR brightness 0..100"); return; }
        app.rgb.setBrightness((uint8_t)v);
        out.println("OK");
        return;
    }
    if (sub == "blink") { app.rgb.setColor(255, 255, 255, 255); app.rgb.setWarning(false); app.rgb.setEffect(LED_EFFECT_BLINK); app.rgb.setOn(); out.println("OK"); return; }
    if (sub == "heartbeat") {
        String v = (a.size() >= 3) ? a[2] : "toggle";
        v.toLowerCase();
        if (v == "status" || v == "get") {
            out.printf("Heartbeat : %s\r\n", app.rgb.heartbeat() ? "on" : "off");
            out.println("OK");
            return;
        }
        bool on = !app.rgb.heartbeat();
        if (v == "on") on = true;
        else if (v == "off") on = false;
        else if (v != "toggle" && v != "") { out.println("usage: rgb heartbeat <on|off|status>"); return; }
        app.fram.data().heartbeat = on ? 1 : 0;
        app.fram.save();
        app.rgb.setHeartbeat(on);
        out.printf("OK heartbeat %s\r\n", on ? "on" : "off");
        return;
    }
    out.println("usage: rgb <status|on|off|set R G B [W]|brightness 0-100|blink|heartbeat [on|off]>");
}

void cmdRegisterRgb(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("rgb", "RGB LEDs", "rgb <status|on|off|set R G B [W]|brightness 0-100|blink|heartbeat [on|off]>", cmdRgb);
}
