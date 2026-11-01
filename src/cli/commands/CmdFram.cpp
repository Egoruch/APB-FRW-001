#include "cli/commands.h"
#include "app/App.h"

static void cmdFram(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "info";
    sub.toLowerCase();

    if (sub == "info") {
        out.printf("FRAM present : %s\r\n", app.fram.isPresent() ? "yes" : "no");
        out.printf("Storage valid: %s\r\n", app.fram.storageValid() ? "yes" : "no");
        out.printf("Capacity     : %u bytes\r\n", app.fram.capacity());
        out.println("OK");
        return;
    }
    if (sub == "read") {
        uint16_t base = (a.size() >= 3) ? (uint16_t)strtol(a[2].c_str(), NULL, 0) : 0;
        if (base >= FRAM_RTC_PAGE_START && base <= FRAM_RTC_PAGE_END) {
            out.println("ERROR 0x0100-0x01FF is addressed as device 0x51 (the RTC) - protected");
            return;
        }
        uint8_t buf[16];
        if (!app.fram.rawReadPage(base, buf, 16)) { out.println("ERROR read failed"); return; }
        out.printf("FRAM @0x%04X:\r\n", base);
        for (int i = 0; i < 16; i++) out.printf("%02X ", buf[i]);
        out.println("\r\nOK");
        return;
    }
    if (sub == "write" && a.size() >= 4) {
        uint16_t base = (uint16_t)strtol(a[2].c_str(), NULL, 0);
        if (base >= FRAM_RTC_PAGE_START && base <= FRAM_RTC_PAGE_END) {
            out.println("ERROR 0x0100-0x01FF is addressed as device 0x51 (the RTC) - protected");
            return;
        }
        int val = (int)strtol(a[3].c_str(), NULL, 0);
        if (val < 0 || val > 255) { out.println("ERROR value 0..255"); return; }
        if (!app.fram.rawWriteByte(base, (uint8_t)val, false)) { out.println("ERROR write failed / reserved area"); return; }
        out.println("OK");
        return;
    }
    if (sub == "erase") {
        app.fram.erase();
        out.println("OK FRAM erased and reinitialized");
        return;
    }
    if (sub == "test") {
        /* use 0x0200 (maps to FRAM alias 0x52) - blocks 0x0100/0x0300 collide
         * with the RTC (0x51) and the light/TMP102 area (0x53) on the bus */
        uint8_t w[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        uint8_t r[4];
        if (!app.fram.rawWritePage(0x0200, w, 4, true)) { out.println("FAIL write"); return; }
        if (!app.fram.rawReadPage(0x0200, r, 4)) { out.println("FAIL read"); return; }
        bool same = (r[0]==w[0] && r[1]==w[1] && r[2]==w[2] && r[3]==w[3]);
        out.println(same ? "PASS FRAM test" : "FAIL data mismatch");
        return;
    }
    out.println("usage: fram <info|read [addr]|write addr val|erase|test>");
    out.println("note : use offsets 0x0200/0x0400-0x07FF; avoid 0x0100/0x0300 (bus collision)");
}

void cmdRegisterFram(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("fram", "FRAM storage", "fram <info|read [addr]|write addr val|erase|test>", cmdFram);
}
