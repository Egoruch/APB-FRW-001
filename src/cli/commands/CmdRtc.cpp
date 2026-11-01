#include "cli/commands.h"
#include "app/App.h"
#include "config/addresses.h"
#include "infra/I2CBus.h"
#include <RTClib.h>
#include <Wire.h>

static void rtcRaw(Output &out, const std::vector<String> &a) {
    uint8_t reg = (a.size() >= 3) ? (uint8_t)strtoul(a[2].c_str(), nullptr, 0) : 0x00;
    bool haveVal = (a.size() >= 4);
    uint8_t val = haveVal ? (uint8_t)strtoul(a[3].c_str(), nullptr, 0) : 0;
    if (!i2cBusLock(500)) { out.println("ERROR i2c busy"); return; }
    if (haveVal) {
        Wire.beginTransmission(ADDR_RTC_BM8563);
        Wire.write(reg);
        Wire.write(val);
        bool ok = (Wire.endTransmission() == 0);
        i2cBusUnlock();
        out.printf("%s raw 0x%02X = 0x%02X\r\n", ok ? "OK" : "ERR", reg, val);
        return;
    }
    uint8_t n = (a.size() >= 3) ? 1 : 16;
    Wire.beginTransmission(ADDR_RTC_BM8563);
    Wire.write(reg);
    Wire.endTransmission(false);
    uint8_t got = Wire.requestFrom((uint8_t)ADDR_RTC_BM8563, n);
    out.printf("RTC raw @0x%02X (%u): ", reg, got);
    for (uint8_t i = 0; i < got; i++) out.printf("%02X ", Wire.read());
    out.println("\r\nOK");
    i2cBusUnlock();
}

static void cmdRtc(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    String sub = (a.size() > 1) ? a[1] : "get";
    sub.toLowerCase();

    if (sub == "raw") { rtcRaw(out, a); return; }
    if (sub == "source") {
        if (a.size() >= 3) {
            String v = a[2]; v.toLowerCase();
            TimeSource s;
            if (v == "ext" || v == "external" || v == "i2c" || v == "rtc" || v == "0") s = TIME_SRC_EXTERNAL;
            else if (v == "soft" || v == "software" || v == "1") s = TIME_SRC_SOFTWARE;
            else { out.println("usage: rtc source <ext|soft>"); return; }
            app.time.setSource(s);
            app.fram.data().timeSource = (uint8_t)s;
            app.fram.save();
            app.time.saveNow();
            out.printf("OK time source: %s\r\n", s == TIME_SRC_SOFTWARE ? "software (FRAM-backed)" : "external RTC");
        } else {
            out.printf("Time source : %s\r\n", app.time.source() == TIME_SRC_SOFTWARE ? "software (FRAM-backed)" : "external RTC");
            out.printf("RTC present : %s\r\n", app.time.present() ? "yes" : "no");
            out.printf("Time        : %s\r\n", app.time.nowISO().c_str());
            out.println("OK");
        }
        return;
    }
    if (sub == "get" || sub == "status") {
        out.println("RTC : " + app.timeString());
        out.printf("Source: %s\r\n", app.time.source() == TIME_SRC_SOFTWARE ? "software" : "external RTC");
        out.printf("RTC present: %s\r\n", app.time.present() ? "yes" : "no");
        out.printf("RTC running: %s\r\n", app.time.running() ? "yes" : "NO");
        out.println("OK");
        return;
    }
    if (sub == "set" && a.size() >= 4) {
        /* require rtc set YYYY-MM-DD HH:MM:SS */
        String date = a[2];
        String tms = a[3];
        int y = atoi(date.substring(0, 4).c_str());
        int mo = atoi(date.substring(5, 7).c_str());
        int d = atoi(date.substring(8, 10).c_str());
        int hh = atoi(tms.substring(0, 2).c_str());
        int mi = atoi(tms.substring(3, 5).c_str());
        int ss = atoi(tms.substring(6, 8).c_str());
        if (y < 2020 || mo < 1 || mo > 12 || d < 1 || d > 31 || hh > 23 || mi > 59 || ss > 59) {
            out.println("ERROR invalid datetime (rtc set YYYY-MM-DD HH:MM:SS)");
            return;
        }
        if (!app.time.set(DateTime(y, mo, d, hh, mi, ss))) { out.println("ERROR RTC not present"); return; }
        out.println("OK");
        return;
    }
    if (sub == "alarm") {
        out.println("RTC alarm not configured in this build");
        out.println("OK");
        return;
    }
    out.println("usage: rtc <get|status|set YYYY-MM-DD HH:MM:SS|source [ext|soft]|alarm|raw [reg] [val]>");
}

void cmdRegisterRtc(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("rtc", "Real-time clock", "rtc <get|status|set YYYY-MM-DD HH:MM:SS|source [ext|soft]|alarm|raw [reg] [val]>", cmdRtc);
}
