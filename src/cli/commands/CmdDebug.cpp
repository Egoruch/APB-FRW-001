#include "cli/commands.h"
#include "app/App.h"
#include "infra/Logger.h"

static void cmdDebug(Output &out, const std::vector<String> &a) {
    String sub = (a.size() > 1) ? a[1] : "status";
    sub.toLowerCase();

    if (sub == "enable")  { Logger::instance().setEnabled(true);  out.println("OK debug/log enabled"); return; }
    if (sub == "disable") { Logger::instance().setEnabled(false); out.println("OK debug/log disabled"); return; }
    if (sub == "status")  { out.printf("Logging enabled: %s\r\n", Logger::instance().enabled() ? "yes" : "no"); out.println("OK"); return; }
    out.println("usage: debug <enable|disable|status>");
}

static void cmdLog(Output &out, const std::vector<String> &a) {
    if (a.size() < 2) { out.println("usage: log <status|enable|disable|level error|warn|info|debug>"); return; }
    String sub = a[1];
    sub.toLowerCase();

    if (sub == "status") {
        out.printf("Logging enabled: %s\r\n", Logger::instance().enabled() ? "yes" : "no");
        out.printf("Log level      : %u\r\n", (unsigned)Logger::instance().level());
        out.println("OK");
        return;
    }
    if (sub == "enable" || sub == "disable") {
        Logger::instance().setEnabled(sub == "enable");
        App::instance()->fram.data().logEnabled = Logger::instance().enabled() ? 1 : 0;
        App::instance()->fram.save();
        out.println("OK");
        return;
    }
    if (sub == "level" && a.size() >= 3) {
        String lv = a[2];
        lv.toLowerCase();
        if (lv == "error") Logger::instance().setLevel(APB_LEVEL_ERROR);
        else if (lv == "warn") Logger::instance().setLevel(APB_LEVEL_WARN);
        else if (lv == "info") Logger::instance().setLevel(APB_LEVEL_INFO);
        else if (lv == "debug") Logger::instance().setLevel(APB_LEVEL_DEBUG);
        else { out.println("ERROR bad log level"); return; }
        App::instance()->fram.data().logLevel = (uint8_t)Logger::instance().level();
        App::instance()->fram.data().logEnabled = Logger::instance().enabled() ? 1 : 0;
        App::instance()->fram.save();
        out.println("OK");
        return;
    }
    out.println("usage: log <status|enable|disable|level error|warn|info|debug>");
}

void cmdRegisterDebug(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("debug", "Debug toggling", "debug <enable|disable|status>", cmdDebug);
    cli.registerCommand("log", "Logging control", "log <status|enable|disable|level error|warn|info|debug>", cmdLog);
}

