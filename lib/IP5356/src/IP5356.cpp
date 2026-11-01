#include "IP5356.h"

/* ------------------------------------------------------------------
 * Register map (see datasheet "IP5356 I2C register spec" V1.15/1.16)
 * ------------------------------------------------------------------ */
// control
#define IP5_SYS_CTL0       0x00 // [1] En_Boost, [0] En_Charger
#define IP5_SYS_CTL1       0x03 // [3] En_Ipow_Low,[2] En_Isys_Low,[5:4] time
#define IP5_VOUT1_CTL0     0x10 // [3] Dcp,[2] Qc,[1] det,[0] En_Vout1_mos
#define IP5_VOUT1_CTL1     0x13 // [1] Vhgilow,[0] Chgilow
#define IP5_VOUT2_CTL0     0x14
#define IP5_VOUT2_CTL1     0x17
#define IP5_VBUS_CTL0      0x18 // [3] Dcp,[2] Qc,[0] En_Vbus_mos
#define IP5_VBUS_CTL1      0x1B
#define IP5_SYS_CTL3       0x25 // [3] En_Vbus_Sinkqc,[2] En_Vin_Sinkqc
#define IP5_TYPEC_CTL2     0x42 // [0] EN_PD
#define IP5_TYPEC_CTL0     0xD1 // [5:4] CC_MODE_SEL (0=UFP,1=DFP,3=DRP)
#define IP5_POW_LOW        0x44 // [5:0] 17.7*N mW
#define IP5_ISYS_LOW       0x45 // [5:0] 5.4*N mA
#define IP5_VOUT1_IMOSLOW  0x49 // 2.68*N mA
#define IP5_VOUT2_IMOSLOW  0x4A
#define IP5_VBUS_IMOSLOW   0x4B
#define IP5_FORCE_STANDBY  0x86 // [7] software shutdown
#define IP5_MOS_CTL        0x86 // data bank: [2:0] MOS off, [5:3] re-detect/open
#define IP5_NTC_CTL0       0xF6 // [5:4] NTC_20uA_Sel
#define IP5_NTC_CTL1       0xFD // [0] En_ntc

// ADC / status (read only)
#define IP5_BATVADC        0x50 // 0.26855 mV
#define IP5_VSYSVADC       0x52 // 1.611328 mV
#define IP5_IIN_IADC       0x54 // 0.671387 mA
#define IP5_IVOUT1         0x56
#define IP5_IVOUT2         0x58
#define IP5_IVBUS          0x5A
#define IP5_VINVADC        0x5C
#define IP5_VOUT1VADC      0x5E
#define IP5_VOUT2VADC      0x60
#define IP5_VBUSVADC       0x62 // 1.611 mV
#define IP5_NTCVADC        0x64 // 0.26855 mV
#define IP5_IBATIADC       0x6E // 1.6785 mA
#define IP5_ISYSIADC       0x70 // 0.671387 mA
#define IP5_VSYS_POW       0x74 // 4.431 mW
#define IP5_SOC_CAP        0x7B // %
#define IP5_STATUS_SRC0    0xA4 // [3:0] VOUT1, [7:4] VOUT2
#define IP5_STATUS_SRC1    0xA5 // [3:0] VBUS
#define IP5_PD_STATE0      0xB1 // [0] Sink_pd_Ok
#define IP5_PD_STATE1      0xC2 // [7] Src_Pd_Ok
#define IP5_PD_STATE2      0xC3 // [0] Src_Pps_Ok
#define IP5_TYPEC_STATE    0xF3 // [7] cc_src_ok, [6] cc_sink_ok
#define IP5_SYS_STATE0     0xC4 // [4] VBUSOK
#define IP5_SYS_STATE2     0xCD // [6] Src_qc_ok
#define IP5_SYS_STATE3     0xD0 // [7] VINOK,[6]VBUSOV,[5]Charge_en,[4]Boost_en,[2:0]
#define IP5_SYS_STATE4     0xD4 // [2:0] Chg_qc_state
#define IP5_CHG_STATE1     0xE8 // [5:4] Vchg,[7]Vbus_mosi,[6]Vin_mosi
#define IP5_CHG_STATE2     0xE9 // [7] Sink_QC_OK,[3]Charge_en_state
#define IP5_MOS_STATE      0xEB // [6]VBUS,[5]VOUT2,[4]VOUT1,[7]At_same

// constants for temperature estimation (generic 10k NTC, B=3435)
#define NTC_B_CONSTANT   3435.0f
#define NTC_R0_AT_25C    10000.0f
#define NTC_T0_KELVIN    298.15f

void IP5356::begin() {
    /* Wire is initialised once by the application (with the correct SDA/SCL
     * pins). Calling TwoWire.begin() here could re-init to default pins, so
     * we deliberately do nothing. */
}

bool IP5356::isConnected() {
    _wire->beginTransmission(_data);
    return _wire->endTransmission() == 0;
}

bool IP5356::readRegister(uint8_t reg, uint8_t *val) {   /* data bank */
    _wire->beginTransmission(_data);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0) return false;
    if (_wire->requestFrom(_data, (uint8_t)1) != 1) return false;
    if (val) *val = _wire->read();
    return true;
}

bool IP5356::writeRegister(uint8_t reg, uint8_t val) {   /* data bank */
    _wire->beginTransmission(_data);
    _wire->write(reg);
    _wire->write(val);
    return _wire->endTransmission() == 0;
}

bool IP5356::readCtlReg(uint8_t reg, uint8_t *val) {     /* control bank */
    _wire->beginTransmission(_ctl);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0) return false;
    if (_wire->requestFrom(_ctl, (uint8_t)1) != 1) return false;
    if (val) *val = _wire->read();
    return true;
}

bool IP5356::writeCtlReg(uint8_t reg, uint8_t val) {     /* control bank */
    _wire->beginTransmission(_ctl);
    _wire->write(reg);
    _wire->write(val);
    return _wire->endTransmission() == 0;
}

bool IP5356::readRegister16(uint8_t regLow, uint16_t *val) {
    uint8_t lo = 0, hi = 0;
    if (!readRegister(regLow, &lo)) return false;
    if (!readRegister(regLow + 1, &hi)) return false;
    if (val) *val = (uint16_t)((uint16_t)hi << 8) | lo;
    return true;
}

bool IP5356::modifyRegister(uint8_t reg, uint8_t setMask, uint8_t clrMask) {
    uint8_t v = 0;
    if (!readCtlReg(reg, &v)) return false;
    v = (uint8_t)((v & ~clrMask) | setMask);
    return writeCtlReg(reg, v);
}

void IP5356::rawCtl(uint8_t reg, uint8_t setMask, uint8_t clrMask) {
    modifyRegister(reg, setMask, clrMask);
}

// ---------------- charge / boost ----------------
bool IP5356::isChargerEnabled() { uint8_t v; return readCtlReg(IP5_SYS_CTL0, &v) && (v & 0x01); }
void IP5356::enableCharger(bool on)  { modifyRegister(IP5_SYS_CTL0, on ? 0x01 : 0x00, 0x01); }
bool IP5356::isBoostEnabled()   { uint8_t v; return readCtlReg(IP5_SYS_CTL0, &v) && (v & 0x02); }
void IP5356::enableBoost(bool on){ modifyRegister(IP5_SYS_CTL0, on ? 0x02 : 0x00, 0x02); }

// ---------------- USB switch force control ----------------
static const uint8_t kPortCtl0[] = { IP5_VOUT1_CTL0, IP5_VOUT2_CTL0, IP5_VBUS_CTL0 };
static const uint8_t kPortMosBit = 0x01;

void IP5356::enableOutput(Port port, bool on) {
    uint8_t reg = kPortCtl0[(int)port];
    modifyRegister(reg, on ? kPortMosBit : 0x00, kPortMosBit);
}

bool IP5356::isOutputEnabled(Port port) {
    uint8_t v = 0;
    return readCtlReg(kPortCtl0[(int)port], &v) && (v & kPortMosBit);
}

void IP5356::setOutputsMos(bool on) {
    /* Datasheet "turn off/on the output ports": write the data-bank register
     * 0x86. 0x07 masks the MOS off; 0x38 re-detects and opens loaded ports. */
    writeRegister(IP5_MOS_CTL, on ? 0x38 : 0x07);
}

void IP5356::enableUsbC5V(bool on) {
    /* VBUS_CTL0 bit0 (En_Vbus_Mos) is READ-ONLY; the MOSFET is driven by the
     * Type-C state machine. Only the DCP (bit3) and QC (bit2) bits are RW.
     * On: enable DCP + boost. Off: clear DCP/QC; leave the shared boost alone
     * so the USB-A ports are not killed. */
    if (on) {
        modifyRegister(IP5_VBUS_CTL0, 0x09, 0x09);   /* DCP (MOS bit ignored) */
        enableBoost(true);
    } else {
        modifyRegister(IP5_VBUS_CTL0, 0x00, 0x0D);   /* DCP|QC|MOS */
    }
}

void IP5356::setCCMode(CCMode mode) {
    uint8_t bits = (uint8_t)mode;   /* UFP=0, DFP=1, DRP=3 */
    modifyRegister(IP5_TYPEC_CTL0, (uint8_t)((bits & 0x03) << 4), 0x30);
}
IP5356::CCMode IP5356::getCCMode() {
    uint8_t v = 0; readCtlReg(IP5_TYPEC_CTL0, &v);
    uint8_t bits = (v >> 4) & 0x03;
    return (CCMode)bits;
}
void IP5356::forceC5V(bool on) {
    /* Best-effort C-port source: DFP role + DCP + boost.
     * The VBUS MOS bit is read-only and the IP5356 only opens VBUS after it
     * detects a sink on CC (vRd 0.85-2.45V). A sink without the 5.1k Rd is
     * seen as vOPEN and cannot be forced on by firmware. */
    if (on) {
        setCCMode(CCMode::DFP);
        modifyRegister(IP5_VBUS_CTL0, 0x09, 0x09);   /* DCP; MOS bit is RO */
        enableBoost(true);
    } else {
        modifyRegister(IP5_VBUS_CTL0, 0x00, 0x0D);   /* DCP|QC|MOS */
        /* leave boost for the USB-A ports */
    }
}

// ---------------- minimum load current ----------------
void IP5356::setMinLoadCurrent(Port port, uint8_t step) {
    uint8_t reg = (port == Port::VOUT1) ? IP5_VOUT1_IMOSLOW
                : (port == Port::VOUT2) ? IP5_VOUT2_IMOSLOW
                                        : IP5_VBUS_IMOSLOW;
    writeCtlReg(reg, step);
}
uint8_t IP5356::getMinLoadCurrent(Port port) {
    uint8_t reg = (port == Port::VOUT1) ? IP5_VOUT1_IMOSLOW
                : (port == Port::VOUT2) ? IP5_VOUT2_IMOSLOW
                                        : IP5_VBUS_IMOSLOW;
    uint8_t v = 0; readCtlReg(reg, &v); return v;
}
void IP5356::setIsysLow(uint8_t step) { modifyRegister(IP5_ISYS_LOW, step & 0x3F, 0x3F); }
uint8_t IP5356::getIsysLow() { uint8_t v = 0; readCtlReg(IP5_ISYS_LOW, &v); return v & 0x3F; }
void IP5356::setPowLow(uint8_t step) { modifyRegister(IP5_POW_LOW, step & 0x3F, 0x3F); }
uint8_t IP5356::getPowLow() { uint8_t v = 0; readCtlReg(IP5_POW_LOW, &v); return v & 0x3F; }

void IP5356::enableLightLoadOff(Port port, bool on) {
    /* CTL1 bit0 = light-load off in discharge state. */
    uint8_t reg = (port == Port::VOUT1) ? IP5_VOUT1_CTL1
                : (port == Port::VOUT2) ? IP5_VOUT2_CTL1
                                        : IP5_VBUS_CTL1;
    modifyRegister(reg, on ? 0x01 : 0x00, 0x01);
}

// ---------------- protocol identification ----------------
static IP5356::Protocol protoFromNibble(uint8_t n) {
    switch (n) {
        case 0x01: return IP5356::Protocol::DCP;
        case 0x06: return IP5356::Protocol::QC_OK;
        case 0x09: return IP5356::Protocol::QC2;
        case 0x0A: return IP5356::Protocol::QC3;
        case 0x0C: return IP5356::Protocol::FCP_SCP;
        case 0x0D: return IP5356::Protocol::AFC;
        case 0x0E: return IP5356::Protocol::SFCP;
        case 0x00: return IP5356::Protocol::NONE;
        default:   return IP5356::Protocol::UNKNOWN;
    }
}

IP5356::Protocol IP5356::getVout1Protocol() {
    uint8_t v = 0; readRegister(IP5_STATUS_SRC0, &v); return protoFromNibble(v & 0x0F);
}
IP5356::Protocol IP5356::getVout2Protocol() {
    uint8_t v = 0; readRegister(IP5_STATUS_SRC0, &v); return protoFromNibble((v >> 4) & 0x0F);
}
IP5356::Protocol IP5356::getVbusProtocol() {
    uint8_t v = 0; readRegister(IP5_STATUS_SRC1, &v); return protoFromNibble(v & 0x0F);
}
bool IP5356::isPdSinkOk() { uint8_t v; return readRegister(IP5_PD_STATE0, &v) && (v & 0x01); }
bool IP5356::isPdSrcOk()  { uint8_t v; return readRegister(IP5_PD_STATE1, &v) && (v & 0x80); }
bool IP5356::isPpsSrcOk() { uint8_t v; return readRegister(IP5_PD_STATE2, &v) && (v & 0x01); }
bool IP5356::isSinkQcOk() { uint8_t v; return readRegister(IP5_CHG_STATE2, &v) && (v & 0x80); }
bool IP5356::isSrcQcOk()  { uint8_t v; return readRegister(IP5_SYS_STATE2, &v) && (v & 0x40); }
bool IP5356::isChargeEnabled() { uint8_t v; return readRegister(IP5_CHG_STATE2, &v) && (v & 0x08); }
bool IP5356::isSinkPd()        { uint8_t v; return readRegister(IP5_PD_STATE0, &v) && (v & 0x01); }
bool IP5356::isSinkFastCharge(){ uint8_t v; return readRegister(IP5_CHG_STATE2, &v) && (v & 0x80); }
float IP5356::getChargeVoltageV() {
    uint8_t v = 0; readRegister(IP5_CHG_STATE1, &v);
    switch ((v >> 4) & 0x03) {
        case 0x03: return 12.0f;
        case 0x02: return 9.0f;
        case 0x01: return 7.0f;
        default:   return 5.0f;
    }
}
uint8_t IP5356::getChargeQcState() { uint8_t v = 0; readRegister(IP5_SYS_STATE4, &v); return v & 0x07; }

// ---------------- NTC ----------------
void IP5356::enableNTC(bool on) { modifyRegister(IP5_NTC_CTL1, on ? 0x01 : 0x00, 0x01); }
bool IP5356::isNTCEnabled()     { uint8_t v; return readCtlReg(IP5_NTC_CTL1, &v) && (v & 0x01); }
void IP5356::setNtcCurrentUa(uint16_t ua) {
    _ntcUa = (ua >= 40) ? 80 : 20;
    uint8_t sel = (_ntcUa >= 40) ? 0x20 /*10=80uA*/ : 0x30 /*11=20uA*/;
    modifyRegister(IP5_NTC_CTL0, sel, 0x30);
}
uint16_t IP5356::getNtcRaw() { uint16_t v = 0; readRegister16(IP5_NTCVADC, &v); return v; }
uint16_t IP5356::getNtcMillivolts() { return (uint16_t)(getNtcRaw() * 0.26855f); }
float IP5356::getNtcCelsius() {
    float vmv = (float)getNtcRaw() * 0.26855f;          /* mV on NTC pin */
    float i = (float)_ntcUa * 1e-6f;                     /* A */
    if (i <= 0.0f) return -100.0f;
    float rntc = (vmv / 1000.0f) / i;                    /* ohms */
    if (rntc <= 0.0f) return -100.0f;
    float invT = 1.0f / NTC_T0_KELVIN
               + (1.0f / NTC_B_CONSTANT) * logf(rntc / NTC_R0_AT_25C);
    return 1.0f / invT - 273.15f;
}

// ---------------- ADC ----------------
float IP5356::getBatteryVoltageV() { uint16_t v = 0; readRegister16(IP5_BATVADC, &v); return v * 0.00026855f; }
float IP5356::getBatteryCurrentA() { uint16_t v = 0; readRegister16(IP5_IBATIADC, &v); return v * 0.0016785f; }
float IP5356::getVsysVoltageV()    { uint16_t v = 0; readRegister16(IP5_VSYSVADC, &v); return v * 0.001611328f; }
float IP5356::getVsysCurrentA()    { uint16_t v = 0; readRegister16(IP5_ISYSIADC, &v); return v * 0.000671387f; }
float IP5356::getVbusVoltageV()    { uint16_t v = 0; readRegister16(IP5_VBUSVADC, &v); return v * 0.001611f; }
float IP5356::getVinVoltageV()     { uint16_t v = 0; readRegister16(IP5_VINVADC, &v);  return v * 0.001611328f; }
float IP5356::getVout1CurrentA()   { uint16_t v = 0; readRegister16(IP5_IVOUT1, &v);   return v * 0.000671387f; }
float IP5356::getVout2CurrentA()   { uint16_t v = 0; readRegister16(IP5_IVOUT2, &v);   return v * 0.000671387f; }
float IP5356::getInputCurrentA()   { uint16_t v = 0; readRegister16(IP5_IIN_IADC, &v); return v * 0.000671387f; }
float IP5356::getVbusCurrentA()    { uint16_t v = 0; readRegister16(IP5_IVBUS, &v);    return v * 0.000671387f; }
float IP5356::getVsysPowerMW()     { uint16_t v = 0; readRegister16(IP5_VSYS_POW, &v); return v * 4.431f; }
uint8_t IP5356::getSOC()           { uint8_t v = 0; readRegister(IP5_SOC_CAP, &v); return v; }

// ---------------- status ----------------
uint8_t IP5356::getSystemState()   { uint8_t v = 0; readRegister(IP5_SYS_STATE3, &v); return v & 0x07; }
bool IP5356::isCharging()          { uint8_t v = 0; return readRegister(IP5_SYS_STATE3, &v) && (v & 0x20); }
bool IP5356::isDischarging()       { uint8_t v = 0; return readRegister(IP5_SYS_STATE3, &v) && (v & 0x10); }
uint8_t IP5356::getMosState()      { uint8_t v = 0; readRegister(IP5_MOS_STATE, &v); return v; }
void IP5356::forceStandby()        { writeRegister(IP5_FORCE_STANDBY, 0x80); }

/* Type-C / VBUS status (data bank 0x75). */
bool IP5356::isCcSrcOk()  { uint8_t v = 0; return readRegister(IP5_TYPEC_STATE, &v) && (v & 0x80); }
bool IP5356::isCcSinkOk() { uint8_t v = 0; return readRegister(IP5_TYPEC_STATE, &v) && (v & 0x40); }
bool IP5356::isVbusOk()   { uint8_t v = 0; return readRegister(IP5_SYS_STATE0, &v) && (v & 0x10); }
