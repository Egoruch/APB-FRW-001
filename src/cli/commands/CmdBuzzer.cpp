#include "cli/commands.h"
#include "app/App.h"

static void cmdBuzzer(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "status") { out.printf("Buzzer active: %s\r\n", app.buzzer.isActive() ? "yes" : "no"); out.println("OK"); return; }
    if (sub == "on")  { app.buzzer.on(); out.println("OK"); return; }
    if (sub == "off") { app.buzzer.off(); out.println("OK"); return; }
    if (sub == "beep") { app.buzzer.beep(100); out.println("OK"); return; }
    if (sub == "frequency" && a.size() >= 3) {
        uint32_t hz = atol(a[2].c_str());
        if (hz == 0 || hz > 20000) { out.println("ERROR frequency out of range"); return; }
        app.buzzer.setFrequency(hz);
        out.println("OK");
        return;
    }
    if (sub == "duration" && a.size() >= 3) {
        uint32_t ms = atol(a[2].c_str());
        if (ms == 0 || ms > 10000) { out.println("ERROR duration out of range"); return; }
        app.buzzer.beep(ms);
        out.println("OK");
        return;
    }
    if (sub == "test") {
        app.buzzer.setFrequency(2000);
        app.buzzer.beep(200);
        out.println("OK buzzer test (beep)");
        return;
    }
    out.println("usage: buzzer <status|on|off|beep|frequency Hz|duration ms|test>");
}

void cmdRegisterBuzzer(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("buzzer", "Buzzer", "buzzer <status|on|off|beep|frequency Hz|duration ms|test>", cmdBuzzer);
}
