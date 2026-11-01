#include "cli/commands.h"
#include "app/App.h"
#include "hardware/PowerMonitor.h"
#include "infra/SerialOutput.h"

/* Long-run cable analysis: samples until the "wrap" of the two ports and
 * reports min/max/avg resistance + an OK/BAD verdict for an estimated length.
 * Runs as a background RTOS task (~10 s) so the CLI stays responsive. */
static volatile bool s_CableRunning = false;
static float      s_CableLenM = 1.0f;
static bool       s_CableViaBle = false;   /* route output to BLE instead of serial */

static void cableLongTask(void *p) {
    (void)p;
    App &app = *App::instance();
    SerialOutput serialOut;   /* safe even if the caller's output was stack-local */
    Output &out = s_CableViaBle ? (Output &)app.ble.output() : (Output &)serialOut;

    /* auto-detect source + sink once */
    app.monitor.sampleAll();
    const PowerChannel ports[4] = { CH_A1, CH_A2, CH_C1, CH_C2 };
    int srcI = -1; float vmax = -1.0f;
    for (int i = 0; i < 4; i++) {
        float ci = app.monitor.get(ports[i]).current; if (ci < 0) ci = -ci;
        float vi = app.monitor.get(ports[i]).voltage;
        if (ci > 0.02f && vi > vmax) { vmax = vi; srcI = i; }
    }
    if (srcI < 0) { out.println("cabletest: no loop"); s_CableRunning = false; vTaskDelete(NULL); return; }
    PowerChannel src = ports[srcI];
    int snkI = -1; float best = 1e9f;
    for (int j = 0; j < 4; j++) {
        if (j == srcI) continue;
        float cj = app.monitor.get(ports[j]).current; if (cj < 0) cj = -cj;
        if (cj < 0.01f) continue;
        float d = fabsf(app.monitor.get(ports[j]).voltage - vmax);
        if (d < best) { best = d; snkI = j; }
    }
    if (snkI < 0) { out.println("cabletest: source found but no sink"); s_CableRunning = false; vTaskDelete(NULL); return; }
    PowerChannel snk = ports[snkI];

    out.printf("cabletest %s->%s  sampling 10 s...\r\n", kChannelName[src], kChannelName[snk]);
    int n = 0; float mn = 1e9f, mx = -1e9f, sum = 0.0f;
    unsigned long t0 = millis();
    int lastSec = 11;   /* countdown from 10 s, printed immediately on the first tick */
    while ((unsigned long)(millis() - t0) < 10000UL) {
        app.monitor.sampleChannel(src);
        app.monitor.sampleChannel(snk);
        float icur = app.monitor.get(src).current; if (icur < 0) icur = -icur;
        if (icur > 0.05f) {
            float drop = app.monitor.get(src).voltage - app.monitor.get(snk).voltage;
            float r = drop / icur * 1000.0f;            /* mOhm */
            if (r > 0.0f && r < 5000.0f) {
                if (r < mn) mn = r;
                if (r > mx) mx = r;
                sum += r; n++;
            }
        }
        /* one countdown tick per second so the user sees progress, all on one line */
        int sec = 10 - (int)((millis() - t0) / 1000);
        if (sec < lastSec) {
            lastSec = sec;
            out.printf("%ds%s", sec, (sec > 1) ? " " : "\r\n");
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (n > 0) {
        float avg = sum / n;
        float lenM = s_CableLenM; if (lenM < 0.05f) lenM = 0.05f;
        float thresh = 80.0f + 70.0f * lenM;           /* incl. both connectors */
        const char *v = (avg <= thresh) ? "OK" : "BAD";
        out.printf("cabletest %s->%s\r\n", kChannelName[src], kChannelName[snk]);
        out.printf("  samples  : %d\r\n", n);
        out.printf("  min/max  : %.1f / %.1f mOhm\r\n", mn, mx);
        out.printf("  avg      : %.1f mOhm\r\n", avg);
        out.printf("  R/m      : %.0f mOhm per m\r\n", avg / lenM);
        out.printf("  est len  : %.2f m   (limit %.0f mOhm)\r\n", lenM, thresh);
        out.printf("  VERDICT  : %s\r\n", v);
    } else {
        out.println("cabletest: no useful samples (no current)");
    }
    s_CableRunning = false;
    vTaskDelete(NULL);
}

/* Cable / connector resistance test.
 * One power-bank IC sources a port (e.g. A2), the other IC charges a port
 * (e.g. C1), and the two ports are joined by the cable under test. The INA226
 * rails give the voltage at each end, so the voltage drop across the cable is
 *   drop = V(source port) - V(sink port)
 * and   R    = drop / I   (I = current flowing through the cable).
 * Typical:  cable a2 c1
 */
static bool portToCh(const String &s, PowerChannel &c) {
    String t = s; t.toLowerCase();
    if (t == "a1") { c = CH_A1; return true; }
    if (t == "a2") { c = CH_A2; return true; }
    if (t == "c1") { c = CH_C1; return true; }
    if (t == "c2") { c = CH_C2; return true; }
    return false;
}

static void printCable(Output &out, const PowerMeasurement &ms, const PowerMeasurement &mn) {
    float vsrc = ms.voltage;
    float vsnk = mn.voltage;
    float icur = ms.current < 0.0f ? -ms.current : ms.current;
    float isnk = mn.current < 0.0f ? -mn.current : mn.current;
    float drop = vsrc - vsnk;

    out.printf("  src : %.3f V   %.3f A\r\n", vsrc, icur);
    out.printf("  snk : %.3f V   %.3f A (|I|)\r\n", vsnk, isnk);
    out.printf("Vdrop = %.0f mV\r\n", drop * 1000.0f);
    if (icur > 0.02f) {
        float r = drop / icur;
        out.printf("R = %.1f mOhm\r\n", r * 1000.0f);
    } else {
        out.printf("R = --  (no load current - enable a source output)\r\n");
    }
    out.println("OK");
}

static void cmdCable(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    PowerChannel src, snk;

    /* cable test [<len_m>] - 10 s analysis in the background */
    if (a.size() >= 2 && (a[1] == "test" || a[1] == "long")) {
        if (s_CableRunning) { out.println("ERROR cable test already running"); return; }
        float lenM = 1.0f;
        if (a.size() >= 3) lenM = atof(a[2].c_str());
        if (lenM < 0.05f) lenM = 0.05f;
        s_CableLenM = lenM;
        s_CableViaBle = (&out == &app.ble.output());   /* remember the origin */
        s_CableRunning = true;
        xTaskCreate(cableLongTask, "cabletest", 4096, NULL, 1, NULL);
        out.printf("OK cable test %.2f m started (10 s)\r\n", lenM);
        return;
    }

    if (a.size() >= 3) {                       /* explicit: cable <src> <snk> */
        if (!portToCh(a[1], src) || !portToCh(a[2], snk)) {
            out.println("usage: cable [test [len_m]] [<src> <snk>]");
            return;
        }
        if (src == snk) { out.println("ERROR source and sink must differ"); return; }
        app.monitor.sampleChannel(src);
        app.monitor.sampleChannel(snk);
        out.printf("cable %s -> %s\r\n", kChannelName[src], kChannelName[snk]);
        printCable(out, app.monitor.get(src), app.monitor.get(snk));
        return;
    }

    /* auto-detect: one port is sourcing (highest V, current out), the other
     * is the closest V neighbour carrying current (the sink through the cable) */
    app.monitor.sampleAll();
    const PowerChannel ports[4] = { CH_A1, CH_A2, CH_C1, CH_C2 };
    int srcI = -1; float vmax = -1.0f;
    for (int i = 0; i < 4; i++) {
        float ci = app.monitor.get(ports[i]).current; if (ci < 0) ci = -ci;
        float vi = app.monitor.get(ports[i]).voltage;
        if (ci > 0.02f && vi > vmax) { vmax = vi; srcI = i; }
    }
    if (srcI < 0) {
        out.println("cable: no loop  (enable a source output, connect the cable)");
        out.println("OK");
        return;
    }
    src = ports[srcI];
    int snkI = -1; float best = 1e9f;
    for (int j = 0; j < 4; j++) {
        if (j == srcI) continue;
        float cj = app.monitor.get(ports[j]).current; if (cj < 0) cj = -cj;
        if (cj < 0.01f) continue;
        float d = fabsf(app.monitor.get(ports[j]).voltage - vmax);
        if (d < best) { best = d; snkI = j; }
    }
    if (snkI < 0) {
        out.println("cable: source found but no sink (check both ends connected)");
        out.println("OK");
        return;
    }
    snk = ports[snkI];
    out.printf("cable (auto) %s -> %s\r\n", kChannelName[src], kChannelName[snk]);
    printCable(out, app.monitor.get(src), app.monitor.get(snk));
}

void cmdRegisterCable(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("cable", "Cable/connector resistance test",
                        "cable [test [len_m] | <src> <snk>]", cmdCable);
}
