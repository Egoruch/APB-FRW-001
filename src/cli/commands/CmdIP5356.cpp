#include "cli/commands.h"
#include "app/App.h"
#include "services/IP5356Manager.h"

/* parses "1"/"2" (or 0/1) into an internal mux channel index (0=IP1,1=IP2) */
static bool chipIdx(const String &s, uint8_t &ch) {
    int n = atoi(s.c_str());
    if (n == 1) { ch = 0; return true; }
    if (n == 2) { ch = 1; return true; }
    if (n == 0) { ch = 0; return true; }
    return false;
}

static const char *protoName(IP5356::Protocol p) {
    switch (p) {
        case IP5356::Protocol::DCP:     return "5V DCP";
        case IP5356::Protocol::QC_OK:   return "QC 5V";
        case IP5356::Protocol::QC2:     return "QC2.0(9V/12V)";
        case IP5356::Protocol::QC3:     return "QC3.0";
        case IP5356::Protocol::FCP_SCP: return "FCP/SCP";
        case IP5356::Protocol::AFC:     return "AFC";
        case IP5356::Protocol::SFCP:    return "SFCP";
        case IP5356::Protocol::NONE:    return "none";
        default:                        return "unknown";
    }
}

static bool portFrom(const String &s, IP5356::Port &p) {
    String t = s; t.toLowerCase();
    if (t == "vout1") { p = IP5356::Port::VOUT1; return true; }
    if (t == "vout2") { p = IP5356::Port::VOUT2; return true; }
    if (t == "vbus")  { p = IP5356::Port::VBUS;  return true; }
    return false;
}

static bool onOff(const String &s) {
    String t = s; t.toLowerCase();
    return (t == "on" || t == "1");
}

/* Strict on/off parse: unknown tokens are an error, not "off". */
static bool parseOnOffStrict(const String &s, bool &on) {
    String t = s; t.toLowerCase();
    if (t == "on" || t == "1")  { on = true;  return true; }
    if (t == "off" || t == "0") { on = false; return true; }
    return false;
}

/* ---------------- register map helpers ---------------- */
struct RegItem { bool ctl; uint8_t reg; const char *name; };

static void dumpRegItem(Output &out, IP5356 &s, bool ctl, uint8_t reg, const char *name) {
    uint8_t v = 0;
    bool ok = ctl ? s.readCtlReg(reg, &v) : s.readRegister(reg, &v);
    if (ok) out.printf("  %-18s %s 0x%02X = 0x%02X (%u)\r\n", name, ctl ? "CTL" : "DAT", reg, v, v);
    else    out.printf("  %-18s %s 0x%02X = read fail\r\n", name, ctl ? "CTL" : "DAT", reg);
}

static bool modifyCtl(IP5356 &s, uint8_t reg, uint8_t set, uint8_t mask) {
    uint8_t v = 0;
    if (!s.readCtlReg(reg, &v)) return false;
    v = (uint8_t)((v & ~mask) | (set & mask));
    return s.writeCtlReg(reg, v);
}

static void sweepBank(Output &out, IP5356 &s, bool ctl) {
    out.printf("%s sweep (0x00-0xFF):\r\n", ctl ? "CTL" : "DAT");
    for (int base = 0; base < 256; base += 16) {
        out.printf("  %02X:", base);
        for (int i = 0; i < 16; i++) {
            uint8_t v = 0;
            bool ok = ctl ? s.readCtlReg((uint8_t)(base + i), &v) : s.readRegister((uint8_t)(base + i), &v);
            if (ok) out.printf(" %02X", v); else out.printf(" --");
        }
        out.println();
    }
}

static bool dumpNamedGroup(Output &out, IP5356 &s, const String &g) {
    if (g == "sys") {
        static const RegItem r[] = {
            {true,0x00,"SYS_CTL0"},{true,0x03,"SYS_CTL1"},{true,0x0F,"SYS_CTL2"},
            {true,0x25,"SYS_CTL3"},{true,0x24,"PPATH_CTL0"},{true,0xF5,"PPATH_CTL1"},
            {true,0x31,"SYS_CTL4 key"},{true,0x33,"SYS_CTL5 vset"},{true,0x84,"SYS_CTL5 lc"},
            {false,0x01,"VIN_VBUS_OV"},{false,0x03,"BATLOW"},{false,0x27,"TEMP_LP"},
        };
        for (auto &x : r) dumpRegItem(out, s, x.ctl, x.reg, x.name);
        return true;
    }
    if (g == "chg") {
        static const RegItem r[] = {
            {true,0x0B,"CHG_CTL0 12V UV"},{true,0x0C,"CHG_CTL1 9V UV"},{true,0x0D,"CHG_CTL2 5V UV"},
            {true,0x21,"CHG timeout1"},{true,0x22,"CHG timeout2"},
            {true,0x26,"CHG_CTL3"},{true,0x27,"CHG_CTL4 12V"},{true,0x28,"CHG_CTL5 5V"},
            {true,0x29,"CHG_CTL6 VBUS5V"},{true,0x2B,"CHG_CTL7 9V"},
            {true,0x6E,"CHG_CTL8 VBUS12V"},{true,0x6F,"CHG_CTL9 VBUS9V"},
            {false,0x3A,"CHG_CTL10 CV"},
        };
        for (auto &x : r) dumpRegItem(out, s, x.ctl, x.reg, x.name);
        return true;
    }
    if (g == "port") {
        static const RegItem r[] = {
            {true,0x10,"VOUT1_CTL0"},{true,0x13,"VOUT1_CTL1"},{true,0x1C,"VOUT1_CTL2"},
            {true,0x14,"VOUT2_CTL0"},{true,0x17,"VOUT2_CTL1"},{true,0x1D,"VOUT2_CTL2"},
            {true,0x18,"VBUS_CTL0"},{true,0x1B,"VBUS_CTL1"},{true,0x1E,"VBUS_CTL2"},
            {true,0xAA,"BST_VSET hi"},{true,0xAB,"BST_VSET lo"},
            {true,0xAD,"BST_5V"},{true,0xAE,"BST_12V_9V"},
            {true,0xB1,"VOUT1 5V Ilim"},{true,0xB3,"VOUT1 9V Ilim"},{true,0xB4,"VOUT1 12V Ilim"},
            {true,0xB5,"VOUT2 5V Ilim"},{true,0xB7,"VOUT2 9V Ilim"},{true,0xB8,"VOUT2 12V Ilim"},
            {true,0xB9,"VBUS 5V Ilim"},{true,0xBB,"VBUS 9V Ilim"},{true,0xBC,"VBUS 12V Ilim"},
        };
        for (auto &x : r) dumpRegItem(out, s, x.ctl, x.reg, x.name);
        return true;
    }
    if (g == "pd") {
        static const RegItem r[] = {
            {true,0xD1,"TYPEC_CTL0 CC"},{true,0xD5,"TYPEC_CTL1 Vmax"},{true,0x42,"TYPEC_CTL2 PD"},
            {true,0x81,"SINK_QC_EN"},{true,0x85,"SRC_QC_EN"},{true,0x86,"SRC_QC_EN2"},
            {false,0x1F,"SYS_CTL5 PD VOUT2"},
            {false,0x90,"STATE_CTL0"},{false,0x91,"STATE_CTL1"},{false,0x92,"STATE_CTL2"},{false,0x97,"STATE_CTL3"},
        };
        for (auto &x : r) dumpRegItem(out, s, x.ctl, x.reg, x.name);
        return true;
    }
    if (g == "cap") {
        static const RegItem r[] = {
            {true,0x4C,"FCAP"},{true,0x78,"EN_FCAP"},
            {false,0x7B,"SOC_CAP_DATA"},{false,0x87,"SOC_CAP_SET"},
        };
        for (auto &x : r) dumpRegItem(out, s, x.ctl, x.reg, x.name);
        return true;
    }
    if (g == "mfp") {
        static const RegItem r[] = {
            {true,0x65,"MFP_CTL0"},{true,0x66,"MFP_CTL1"},
            {false,0x68,"LED4VADC lo"},{false,0x69,"LED4VADC hi"},
            {false,0x6A,"LED5VADC lo"},{false,0x6B,"LED5VADC hi"},
            {false,0x6C,"LED6VADC lo"},{false,0x6D,"LED6VADC hi"},
        };
        for (auto &x : r) dumpRegItem(out, s, x.ctl, x.reg, x.name);
        return true;
    }
    if (g == "stat2") {
        static const RegItem r[] = {
            {false,0x51,"BATV hi"},{false,0x53,"VSYSV hi"},{false,0x55,"IVIN hi"},
            {false,0x57,"IVOUT1 hi"},{false,0x59,"IVOUT2 hi"},{false,0x5B,"IVBUS hi"},
            {false,0x5D,"VINV hi"},{false,0x5F,"VOUT1V hi"},{false,0x61,"VOUT2V hi"},
            {false,0x63,"VBUSV hi"},{false,0x65,"NTCV hi"},{false,0x6F,"IBAT hi"},
            {false,0x71,"ISYS hi"},{false,0x75,"VSYS_POW hi"},
            {false,0xA1,"FCP_STATUS"},{false,0xA8,"STATUS_SRC2"},{false,0xAF,"AFC_STATUS"},
            {false,0xC5,"SYS_STATE1"},{false,0xE1,"LOWCUR_STATE"},{false,0xF2,"ILOW_STATE"},
            {false,0xF4,"KEYIN_STATE"},{false,0xFB,"NTC_STATE"},{false,0xFC,"OCP_STATE"},
        };
        for (auto &x : r) dumpRegItem(out, s, x.ctl, x.reg, x.name);
        return true;
    }
    return false;
}

/* input (sink) fast-charge protocol + negotiated voltage */
static void printSink(Output &out, IP5356 &s) {
    if (s.isSinkFastCharge()) {
        out.printf("sink: fast-charge %s %.0fV (qc_state=%u)\r\n",
                   s.isSinkPd() ? "PD" : "QC", s.getChargeVoltageV(), s.getChargeQcState());
    } else if (s.isChargeEnabled() || s.isCharging()) {
        out.printf("sink: charging 5V\r\n");
    } else {
        out.printf("sink: none\r\n");
    }
}

static void dumpChip(Output &out, uint8_t ch) {
    if (!App::instance()->ipman.begin(ch)) { out.printf("IP5356 %u: ERROR (mux/bus fail)\r\n", ch + 1); return; }
    IP5356 &s = App::instance()->ipman.current();
    if (!s.isConnected()) {
        App::instance()->ipman.end();
        out.printf("--- IP5356 %u: not present on this channel ---\r\n", ch + 1);
        return;
    }
    out.printf("--- IP5356 %u (addr 0x75) detected ---\r\n", ch + 1);
    out.printf("Vbat=%.3fV Ibat=%.3fA SOC=%u%%\r\n", s.getBatteryVoltageV(), s.getBatteryCurrentA(), s.getSOC());
    out.printf("Vsys=%.3fV Isys=%.3fA Psys=%.0fmW\r\n", s.getVsysVoltageV(), s.getVsysCurrentA(), s.getVsysPowerMW());
    out.printf("Vin=%.3fV Vbus=%.3fV Iin=%.3fA Ivbus=%.3fA\r\n", s.getVinVoltageV(), s.getVbusVoltageV(), s.getInputCurrentA(), s.getVbusCurrentA());
    out.printf("Iout VOUT1=%.3fA VOUT2=%.3fA\r\n", s.getVout1CurrentA(), s.getVout2CurrentA());
    out.printf("state: charge=%s dischg=%s sys=%u\r\n", s.isCharging() ? "ON" : "off",
               s.isDischarging() ? "ON" : "off", s.getSystemState());
    out.printf("prot: v1=%s v2=%s vbus=%s\r\n", protoName(s.getVout1Protocol()),
               protoName(s.getVout2Protocol()), protoName(s.getVbusProtocol()));
    printSink(out, s);
    out.printf("PD: sink=%s src=%s pps=%s  QC: sink=%s src=%s\r\n",
               s.isPdSinkOk() ? "on" : "off", s.isPdSrcOk() ? "on" : "off",
               s.isPpsSrcOk() ? "on" : "off", s.isSinkQcOk() ? "on" : "off", s.isSrcQcOk() ? "on" : "off");
    out.printf("mos(v1/v2/vbus)=%d/%d/%d  ntc raw=%u %.0fmV %.1fC\r\n",
               (s.getMosState() >> 4) & 1, (s.getMosState() >> 5) & 1, (s.getMosState() >> 6) & 1,
               s.getNtcRaw(), (float)s.getNtcMillivolts(), s.getNtcCelsius());
    App::instance()->ipman.end();
}

static void cmdIP(Output &out, const std::vector<String> &a) {
    App &app = *App::instance();
    IP5356Manager &ip = app.ipman;

    if (a.size() < 2) { out.println("usage: ip <1|2|status|wake|sleep> <cmd>"); return; }

    String s1 = a[1]; s1.toLowerCase();
    if (s1 == "status" || s1 == "all") {
        dumpChip(out, 0);
        dumpChip(out, 1);
        out.println("OK");
        return;
    }
    if (s1 == "wake") {
        if (a.size() >= 3 && a[2].equalsIgnoreCase("hold")) {
            uint32_t ms = (a.size() >= 4) ? (uint32_t)strtoul(a[3].c_str(), nullptr, 0) : 2000;
            if (ms < 100) ms = 100;
            if (ms > 5000) ms = 5000;
            bool ok = ip.wakeHold(ms);
            if (ok) out.printf("OK IP_WAKEUP (mux SD3) held %lu ms\r\n", (unsigned long)ms);
            else    out.println("ERROR could not select mux SD3");
            return;
        }
        ip.wake();
        out.println("OK wake pulse sent; ip1=" + f2str(ip.present(0), 0) + " ip2=" + f2str(ip.present(1), 0));
        return;
    }
    if (s1 == "sleep") {
        /* ip sleep      -> both chips to software standby
         * ip sleep 1|2  -> only the selected chip */
        bool one = false;
        uint8_t only = 0;
        if (a.size() >= 3) {
            if (!chipIdx(a[2], only)) { out.println("ERROR chip must be 1 or 2"); return; }
            one = true;
        }
        for (uint8_t c = (one ? only : 0); c <= (one ? only : 1); c++) {
            if (!ip.begin(c)) { out.printf("IP5356 %u: mux/bus fail\r\n", c + 1); continue; }
            IP5356 &s = ip.current();
            if (s.isConnected()) { s.forceStandby(); out.printf("OK IP5356 %u asleep\r\n", c + 1); }
            else                 out.printf("IP5356 %u already asleep\r\n", c + 1);
            ip.end();
        }
        return;
    }
    if (s1 == "outputs" || s1 == "mos") {
        /* ip outputs on|off -> all VOUT1/VOUT2/VBUS MOS on both chips */
        if (a.size() < 3) { out.println("usage: ip outputs <on|off>"); return; }
        bool on;
        if (!parseOnOffStrict(a[2], on)) { out.println("ERROR need on|off"); return; }
        for (uint8_t c = 0; c < 2; c++) {
            if (!ip.begin(c)) { out.printf("IP5356 %u: mux/bus fail\r\n", c + 1); continue; }
            IP5356 &s = ip.current();
            if (!s.isConnected()) out.printf("IP5356 %u asleep (run 'ip wake' first)\r\n", c + 1);
            else {
                s.setOutputsMos(on);
                out.printf("OK IP5356 %u outputs %s\r\n", c + 1, on ? "on" : "off");
            }
            ip.end();
        }
        out.println("OK");
        return;
    }

    uint8_t ch; 
    if (!chipIdx(a[1], ch)) { out.println("ERROR chip must be 1 or 2"); return; }
    if (a.size() < 3) { dumpChip(out, ch); out.println("OK"); return; }

    String sub = a[2]; sub.toLowerCase();
    if (!ip.begin(ch)) { out.println("ERROR no IP5356 on this channel"); return; }
    IP5356 &s = ip.current();
    if (!s.isConnected()) {
        ip.end();
        out.printf("ERROR IP5356 %u not present on this channel\r\n", ch + 1);
        return;
    }

    if (sub == "status") {
        ip.end();
        dumpChip(out, ch); out.println("OK"); return;
    }
    if (sub == "adc") {
        out.printf("Vbat=%.3fV Ibat=%.3fA Vsys=%.3fV Vin=%.3fV Vbus=%.3fV\r\n",
                   s.getBatteryVoltageV(), s.getBatteryCurrentA(), s.getVsysVoltageV(),
                   s.getVinVoltageV(), s.getVbusVoltageV());
        out.printf("Iin=%.3fA Iout1=%.3fA Iout2=%.3fA\r\n", s.getInputCurrentA(), s.getVout1CurrentA(), s.getVout2CurrentA());
        ip.end(); out.println("OK"); return;
    }
    if (sub == "outputs" || sub == "mos") {
        if (a.size() < 4) { out.println("ERROR outputs <on|off>"); ip.end(); return; }
        bool on;
        if (!parseOnOffStrict(a[3], on)) { out.println("ERROR need on|off"); ip.end(); return; }
        if (!s.isConnected())
            out.printf("ERROR IP5356 %u asleep - run 'ip wake' first\r\n", ch + 1);
        else {
            s.setOutputsMos(on);
            out.printf("OK IP5356 %u outputs %s\r\n", ch + 1, on ? "on" : "off");
        }
        ip.end(); out.println("OK"); return;
    }

    if (sub == "cstatus") {
        uint8_t m = (uint8_t)s.getCCMode();
        out.printf("CC mode  : %s (%u)\r\n",
                   m == 0 ? "ufp" : (m == 1 ? "dfp" : (m == 3 ? "drp" : "?")), m);
        out.printf("CC src_ok: %d   sink_ok: %d\r\n", s.isCcSrcOk() ? 1 : 0, s.isCcSinkOk() ? 1 : 0);
        out.printf("VBUSOK   : %d   Vin=%.3fV  Vbus=%.3fV  proto=%s\r\n",
                   s.isVbusOk() ? 1 : 0, s.getVinVoltageV(), s.getVbusVoltageV(), protoName(s.getVbusProtocol()));
        out.printf("PD src=%d sink=%d pps=%d  QC src=%d sink=%d\r\n",
                   s.isPdSrcOk() ? 1 : 0, s.isPdSinkOk() ? 1 : 0, s.isPpsSrcOk() ? 1 : 0,
                   s.isSrcQcOk() ? 1 : 0, s.isSinkQcOk() ? 1 : 0);
        out.println("OK"); ip.end(); return;
    }
    if (sub == "sweep") {
        sweepBank(out, s, true);
        sweepBank(out, s, false);
        ip.end(); out.println("OK"); return;
    }
    if (sub == "pd" && a.size() >= 4) {
        String act = a[3]; act.toLowerCase();
        if (act == "off" || act == "on") {
            bool on = (act == "on");
            /* Global PD enable (TYPEC_CTL2 0x42 bit0) + VBUS output PD bit
             * (control 0xD4 bit0, per the datasheet's output-PD procedure). */
            uint8_t v = 0;
            if (s.readCtlReg(0x42, &v)) { v = on ? (v | 0x01) : (v & ~0x01); s.writeCtlReg(0x42, v); }
            if (s.readCtlReg(0xD4, &v)) { v = on ? (v | 0x01) : (v & ~0x01); s.writeCtlReg(0xD4, v); }
            /* Output DPDM/QC protocols: off clears 0x85[7:1]; on restores the
             * factory value. Without this the device falls back to QC 12V. */
            s.writeCtlReg(0x85, on ? 0x7F : 0x01);
            /* Input (sink) PD + QC: PD sink state 0x91[3:2], DCP sink state
             * 0x90[5:4], DPDM enable 0x81[4:1], VBUS/VIN sink QC 0x25[3:2]. */
            modifyCtl(s, 0x91, on ? 0x00 : 0x04, 0x0C);   /* auto / off */
            modifyCtl(s, 0x90, on ? 0x00 : 0x10, 0x30);   /* auto / off */
            modifyCtl(s, 0x81, on ? 0x1E : 0x00, 0x1E);   /* input DPDM */
            modifyCtl(s, 0x25, on ? 0x0C : 0x00, 0x0C);   /* VBUS/VIN sink QC */
            /* Restart the Type-C input port: UFP -> DRP (datasheet procedure). */
            s.setCCMode(IP5356::CCMode::UFP);
            delay(300);
            s.setCCMode(IP5356::CCMode::DRP);
            delay(300);
            /* Restart the output port so an existing PD contract is dropped. */
            s.setOutputsMos(false);
            delay(300);
            s.setOutputsMos(true);
            s.enableUsbC5V(true);
            delay(1200);   /* let the device renegotiate */
            out.printf("PD %s: port restarted; verify with 'monitor c1' / 'ip 1 cstatus'\r\n",
                       on ? "on" : "off");
            ip.end(); out.println("OK"); return;
        }
        bool ok = false;
        if (act == "enable") {
            bool on;
            if (a.size() < 5 || !parseOnOffStrict(a[4], on)) { out.println("ERROR pd enable on|off"); ip.end(); return; }
            ok = modifyCtl(s, 0x42, on ? 0x01 : 0x00, 0x01);
        } else if (act == "vmax") {
            if (a.size() < 5) { out.println("ERROR pd vmax 5|9|12"); ip.end(); return; }
            int v = atoi(a[4].c_str());
            uint8_t bits = (v == 9) ? 1 : (v == 12) ? 2 : 0;
            ok = modifyCtl(s, 0xD5, (uint8_t)(bits << 5), 0x60);
        } else { out.println("usage: ip <n> pd [enable on|off | vmax 5|9|12]"); ip.end(); return; }
        out.println(ok ? "OK" : "ERROR PD write");
        ip.end(); return;
    }
    if (sub == "prot" && a.size() >= 5) {
        String which = a[3]; which.toLowerCase();
        bool on;
        if (!parseOnOffStrict(a[4], on)) { out.println("ERROR prot src|sink on|off"); ip.end(); return; }
        bool ok = false;
        if (which == "src")       ok = modifyCtl(s, 0x85, on ? 0xFE : 0x00, 0xFE);
        else if (which == "sink") ok = modifyCtl(s, 0x81, on ? 0x1E : 0x00, 0x1E);
        else { out.println("usage: ip <n> prot src|sink on|off"); ip.end(); return; }
        out.println(ok ? "OK" : "ERROR protocol write");
        ip.end(); return;
    }
    if (sub == "cap" && a.size() >= 4) {
        String act = a[3]; act.toLowerCase();
        bool ok = false;
        if (act == "set" && a.size() >= 5) {
            uint8_t v = (uint8_t)strtoul(a[4].c_str(), nullptr, 0);
            ok = s.writeRegister(0x87, v);
        } else if (act == "fcap" && a.size() >= 5) {
            uint8_t v = (uint8_t)strtoul(a[4].c_str(), nullptr, 0);
            ok = s.writeCtlReg(0x4C, v);
        } else if (act == "en" && a.size() >= 5) {
            bool on;
            if (!parseOnOffStrict(a[4], on)) { out.println("ERROR cap en on|off"); ip.end(); return; }
            ok = modifyCtl(s, 0x78, on ? 0x80 : 0x00, 0x80);
        } else { out.println("usage: ip <n> cap [set <0-255> | fcap <0-255> | en on|off]"); ip.end(); return; }
        out.println(ok ? "OK" : "ERROR capacity write");
        ip.end(); return;
    }
    if (sub == "mfp" && a.size() >= 6 && a[3].equalsIgnoreCase("set")) {
        uint8_t reg = (uint8_t)strtoul(a[4].c_str(), nullptr, 0);
        uint8_t val = (uint8_t)strtoul(a[5].c_str(), nullptr, 0);
        if (reg != 0x65 && reg != 0x66) { out.println("ERROR mfp set 0x65|0x66 <value>"); ip.end(); return; }
        bool ok = s.writeCtlReg(reg, val);
        out.println(ok ? "OK" : "ERROR MFP write");
        ip.end(); return;
    }
    if (dumpNamedGroup(out, s, sub)) {
        ip.end(); out.println("OK"); return;
    }
    IP5356::Port port;
    bool done = true;

    if (sub == "vout1" || sub == "vout2" || sub == "vbus") {
        if (a.size() < 4) { out.println("ERROR need on|off"); done = false; }
        else { portFrom(sub, port); bool on = onOff(a[3]); s.enableOutput(port, on);
               out.printf("OK port %s output %s\r\n", sub.c_str(), on ? "enabled" : "disabled"); }
    }
    else if (sub == "ccmode") {
        if (a.size() < 4) {
            uint8_t m = (uint8_t)s.getCCMode();
            out.printf("CC mode = %s (reg=%u)\r\n",
                       m == 0 ? "ufp" : (m == 1 ? "dfp" : (m == 3 ? "drp" : "?")), m);
        } else {
            String m = a[3]; m.toLowerCase();
            IP5356::CCMode mm = IP5356::CCMode::DRP;
            if (m == "ufp") mm = IP5356::CCMode::UFP;
            else if (m == "dfp") mm = IP5356::CCMode::DFP;
            else if (m == "drp") mm = IP5356::CCMode::DRP;
            else { out.println("ERROR ccmode ufp|dfp|drp"); done = false; }
            if (done) {
                s.setCCMode(mm);
                out.printf("OK CC mode = %s (reg=0x%02X)\r\n", m.c_str(), (unsigned)s.getCCMode());
            }
        }
    }
    else if (sub == "minload") {
        if (a.size() < 5) { out.println("ERROR minload <vout1|vout2|vbus> <step 0-255>"); done = false; }
        else if (!portFrom(a[3], port)) { out.println("ERROR port"); done = false; }
        else { int step = atoi(a[4].c_str()); if (step < 0) step = 0; if (step > 255) step = 255;
               s.setMinLoadCurrent(port, (uint8_t)step);
               out.printf("OK %s min-load step=%d (%.1f mA)\r\n", a[3].c_str(), step, step * 2.68f); }
    }
    else if (sub == "isyslow") {
        if (a.size() < 4) { out.println("ERROR isyslow <step 0-63>"); done = false; }
        else { int step = atoi(a[3].c_str()); if (step < 0) step = 0; if (step > 63) step = 63;
               s.setIsysLow((uint8_t)step); out.printf("OK isys_low step=%d (%.1f mA)\r\n", step, step * 5.4f); }
    }
    else if (sub == "powlow") {
        if (a.size() < 4) { out.println("ERROR powlow <step 0-63>"); done = false; }
        else { int step = atoi(a[3].c_str()); if (step < 0) step = 0; if (step > 63) step = 63;
               s.setPowLow((uint8_t)step); out.printf("OK pow_low step=%d (%.1f mW)\r\n", step, step * 17.7f); }
    }
    else if (sub == "llo") {
        if (a.size() < 5) { out.println("ERROR llo <vout1|vout2|vbus> <on|off>"); done = false; }
        else if (!portFrom(a[3], port)) { out.println("ERROR port"); done = false; }
        else { bool on = onOff(a[4]); s.enableLightLoadOff(port, on);
               out.printf("OK %s light-load-off %s\r\n", a[3].c_str(), on ? "enabled" : "disabled"); }
    }
    else if (sub == "protocol") {
        out.printf("v1=%s v2=%s vbus=%s\r\n", protoName(s.getVout1Protocol()),
                   protoName(s.getVout2Protocol()), protoName(s.getVbusProtocol()));
        out.printf("PD sink=%s src=%s pps=%s  QC sink=%s src=%s\r\n",
                   s.isPdSinkOk() ? "on" : "off", s.isPdSrcOk() ? "on" : "off",
                   s.isPpsSrcOk() ? "on" : "off", s.isSinkQcOk() ? "on" : "off", s.isSrcQcOk() ? "on" : "off");
        printSink(out, s);
    }
    else if (sub == "ntc") {
        if (a.size() >= 4 && a[3] == "on")       { s.enableNTC(true);  out.println("OK ntc enabled"); }
        else if (a.size() >= 4 && a[3] == "off") { s.enableNTC(false); out.println("OK ntc disabled"); }
        else if (a.size() >= 4 && a[3] == "cur") {
            int ua = (a.size() >= 5) ? atoi(a[4].c_str()) : 80;
            s.setNtcCurrentUa((uint16_t)ua); out.printf("OK ntc current %u uA\r\n", ua);
        }
        else out.printf("NTC raw=%u %.0fmV %.1fC\r\n", s.getNtcRaw(), (float)s.getNtcMillivolts(), s.getNtcCelsius());
    }
    else if (sub == "charge") {
        if (a.size() < 4) { out.println("ERROR charge on|off"); done = false; }
        else { bool on = onOff(a[3]); s.enableCharger(on); out.printf("OK charger %s\r\n", on ? "enabled" : "disabled"); }
    }
    else if (sub == "boost") {
        if (a.size() < 4) { out.println("ERROR boost on|off"); done = false; }
        else { bool on = onOff(a[3]); s.enableBoost(on); out.printf("OK boost %s\r\n", on ? "enabled" : "disabled"); }
    }
    else if (sub == "sleep")    { s.forceStandby(); out.println("OK forced standby (sleep)"); }
    else if (sub == "addr") {
        if (a.size() < 4) { out.println("ERROR addr <0x74|0x75>"); done = false; }
        else {
            uint8_t ad = (uint8_t)strtoul(a[3].c_str(), nullptr, 16);
            if (ad != 0x74 && ad != 0x75) { out.println("ERROR addr must be 0x74 or 0x75"); done = false; }
            else { ip.setAddress(ad); out.printf("OK device address now 0x%02X\r\n", ad); }
        }
    }
    else if (sub == "reg") {
        if (a.size() < 4) { out.println("ERROR reg <0x..> [value]"); done = false; }
        else {
            uint8_t reg = (uint8_t)strtoul(a[3].c_str(), nullptr, 16);
            if (a.size() >= 5) {
                uint8_t val = (uint8_t)strtoul(a[4].c_str(), nullptr, 16);
                bool ok = s.writeRegister(reg, val);
                out.printf("%s reg 0x%02X = 0x%02X\r\n", ok ? "OK" : "ERR", reg, val);
            } else {
                uint8_t val = 0; bool ok = s.readRegister(reg, &val);
                out.printf("reg 0x%02X = 0x%02X%s\r\n", reg, val, ok ? "" : " (read fail)");
            }
        }
    }
    else if (sub == "ctlreg") {
        if (a.size() < 4) { out.println("ERROR ctlreg <reg> [value]"); done = false; }
        else {
            uint8_t reg = (uint8_t)strtoul(a[3].c_str(), nullptr, 16);
            if (a.size() >= 5) {
                uint8_t val = (uint8_t)strtoul(a[4].c_str(), nullptr, 16);
                bool ok = s.writeCtlReg(reg, val);
                out.printf("%s ctlreg 0x%02X = 0x%02X\r\n", ok ? "OK" : "ERR", reg, val);
            } else {
                uint8_t val = 0; bool ok = s.readCtlReg(reg, &val);
                out.printf("ctlreg 0x%02X = 0x%02X%s\r\n", reg, val, ok ? "" : " (read fail)");
            }
        }
    }
    else { done = false; }

    if (!done) out.println("usage: ip <1|2> <vout1|vout2|vbus|ccmode|cstatus|minload|isyslow|powlow|llo|protocol|ntc|charge|boost|outputs on|off|addr|sleep|adc|status|reg|ctlreg|sweep|sys|chg|port|pd|prot|cap|mfp|stat2>");
    ip.end();
    if (done) out.println("OK");
}

void cmdRegisterIP5356(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("ip", "IP5356 PMIC (behind I2C mux)",
                        "ip <1|2|status|wake [hold [ms]]|sleep|outputs> <cmd>", cmdIP);
}
