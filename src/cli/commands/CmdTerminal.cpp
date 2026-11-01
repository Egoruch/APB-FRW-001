#include "cli/commands.h"
#include "infra/term.h"

static void cmdClr(Output &out, const std::vector<String> &a) {
    (void)a;
    termClear();
    out.println("OK terminal cleared");
}

static void cmdEcho(Output &out, const std::vector<String> &a) {
    String s;
    for (size_t i = 1; i < a.size(); i++) {
        if (i > 1) s += ' ';
        s += a[i];
    }
    out.println(s);
}

void cmdRegisterTerminal(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("clr", "Clear the on-screen terminal", "clr", cmdClr);
    cli.registerCommand("echo", "Echo text", "echo <text>", cmdEcho);
}
