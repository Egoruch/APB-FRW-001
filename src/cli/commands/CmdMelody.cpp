#include "cli/commands.h"
#include "app/App.h"
#include "sound/melodies.h"

static void play(Output &out, const char *name, const BuzzerNote *notes, int count) {
    App *app = App::instance();
    if (!app) { out.println("ERROR no app"); return; }
    app->playMelody(notes, count);
    out.printf("OK playing %s\r\n", name);
}

static void cmdMelody(Output &out, const std::vector<String> &a) {
    String sub = (a.size() > 1) ? a[1] : "demo";
    sub.toLowerCase();

    if (sub == "oginski" || sub == "polonaise" || sub == "farewell") {
        int n; out.printf("Melody: Ogiski polonaise\r\n"); play(out, "Ogiski polonaise", melodyOginski(&n), n); return;
    }
    if (sub == "jingle") {
        int n; out.printf("Melody: Jingle Bells\r\n"); play(out, "Jingle Bells", melodyJingle(&n), n); return;
    }
    if (sub == "svoi" || sub == "artemev") {
        int n; out.printf("Melody: Svoi sredi chuzhih (Artemyev)\r\n"); play(out, "Svoi sredi chuzhih", melodySvoi(&n), n); return;
    }
    out.println("usage: melody <oginski|jingle|svoi>");
}

void cmdRegisterMelody(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("melody", "Play a melody",
                        "melody <oginski|jingle|svoi>", cmdMelody);
}

