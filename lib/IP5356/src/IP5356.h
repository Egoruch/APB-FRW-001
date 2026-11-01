#ifndef IP5356_H
#define IP5356_H

#include <Arduino.h>
#include <Wire.h>

/* -------------------------------------------------------------------------
 * IP5356 - Injoinic power-bank / fast-charge PMIC, register accessor.
 *
 * Design notes:
 *  - STANDALONE: this class only talks I2C through the provided TwoWire. It
 *    has NO knowledge of any I2C multiplexer (the PCA9546A routing for the
 *    two IP5356 devices is handled by the caller / manager, not here).
 *  - Register map taken from the official "IP5356 (with reg)" datasheet
 *    V1.15/V1.16. A single device address (default 0x75) is used with a
 *    unified register map: the offsets the datasheet lists under the 0xE8 and
 *    0xEA I2C groups are all addressable from one address.
 *  - All scaling factors come from the datasheet (e.g. VBAT=ADC*0.26855 mV).
 * ------------------------------------------------------------------------- */
class IP5356 {
public:
    /* Output fast-charge protocol (from STATUS_SRC0/1 register codes). */
    enum class Protocol {
        NONE   = 0x00,
        UNKNOWN= 0x01,
        DCP    = 0x0001,   // common 5V DCP / apple
        QC_OK  = 0x0110,   // QC 5V
        QC2    = 0x1001,   // QC2.0 (9V/12V)
        QC3    = 0x1010,   // QC3.0
        FCP_SCP= 0x1100,   // FCP / SCP (Huawei)
        AFC    = 0x1101,   // Samsung AFC
        SFCP   = 0x1110    // SFCP
    };

    /* Port selectors used by several setters/getters. */
    enum class Port { VOUT1 = 0, VOUT2 = 1, VBUS = 2 };

    /* Type-C CC role (TYPEC_CTL0 [5:4]): UFP(sink), DFP(source), DRP. */
    enum class CCMode { UFP = 0, DFP = 1, DRP = 3 };

    IP5356(uint8_t ctlAddr = 0x74, uint8_t dataAddr = 0x75, TwoWire &wire = Wire)
        : _ctl(ctlAddr), _data(dataAddr), _wire(&wire), _ntcUa(80) {}

    void begin();
    bool isConnected();                     /* ACK probe (data addr) */
    void setAddress(uint8_t a) { _data = a; }   /* 0xE8->0x74, 0xEA->0x75 (data) */
    uint8_t address() const { return _data; }

    /* ---------------- low-level register access ---------------- */
    bool readRegister(uint8_t reg, uint8_t *val);    /* data bank (0x75) */
    bool writeRegister(uint8_t reg, uint8_t val);    /* data bank (0x75) */
    bool readCtlReg(uint8_t reg, uint8_t *val);      /* control bank (0x74) */
    bool writeCtlReg(uint8_t reg, uint8_t val);      /* control bank (0x74) */
    bool readRegister16(uint8_t regLow, uint16_t *val);
    bool modifyRegister(uint8_t reg, uint8_t setMask, uint8_t clrMask); /* control */
    void rawCtl(uint8_t reg, uint8_t setMask, uint8_t clrMask);         /* control RMW */

    /* ---------------- global charge / boost enable ---------------- */
    bool isChargerEnabled();
    void enableCharger(bool on);
    bool isBoostEnabled();
    void enableBoost(bool on);

    /* -------- force control of each USB switch (output MOS) -------- */
    void enableOutput(Port port, bool on);
    bool isOutputEnabled(Port port);

    /* Master MOS control (data bank 0x86), per datasheet procedure:
     *   0x86[2:0] = 111 -> turn off VOUT1/VOUT2/VBUS MOS
     *   0x86[5:3] = 111 -> re-detect / open the loaded USB ports
     * This is the reliable "all outputs off" control; clearing a single
     * VOUTx_CTL0 MOS bit is not enough while the port DCP/QC engine runs. */
    void setOutputsMos(bool on);

    /* -------- force USB-C power enable (5V) -------------------------
     * Used to charge devices whose CC lines have no 5.1k pulldown: enables
     * the VBUS MOS + common 5V DCP so VBUS drives 5V without negotiation. */
    void enableUsbC5V(bool on);

    /* Type-C CC role + full forced 5V source on the C port (DFP/source).
     * forceC5V puts the chip as a Type-C source (CC=DFP), enables the output
     * MOS on VBUS/VOUT1/VOUT2 and the 5V boost - for USB-C-to-USB-C cables
     * into a C port even when the device lacks the 5.1k CC pulldowns. */
    void setCCMode(CCMode mode);
    CCMode getCCMode();
    void forceC5V(bool on);

    /* -------- minimum load current (light-load shutdown threshold) ---
     * IMOS_LOW = 2.68*N mA per port, ISYS_LOW = 5.4*N mA, POW_LOW=17.7*N mW. */
    void setMinLoadCurrent(Port port, uint8_t step);   /* IMOS_LOW 2.68*N mA */
    uint8_t getMinLoadCurrent(Port port);
    void setIsysLow(uint8_t step);                     /* 5.4*N mA */
    uint8_t getIsysLow();
    void setPowLow(uint8_t step);                      /* 17.7*N mW */
    uint8_t getPowLow();
    /* Disable light-load auto shutdown on a port (keeps output alive under
     * a light load). on=false turns the light-load-off feature OFF. */
    void enableLightLoadOff(Port port, bool on);

    /* -------- fast-charge protocol identification ---------------- */
    Protocol getVout1Protocol();
    Protocol getVout2Protocol();
    Protocol getVbusProtocol();
    bool isPdSinkOk();      /* PD SINK connected (charging in) */
    bool isPdSrcOk();       /* PD SRC connected (charging out) */
    bool isPpsSrcOk();
    bool isSinkQcOk();      /* input fast charge valid (DPDM or PD) */
    bool isSrcQcOk();       /* output fast charge valid */
    bool isChargeEnabled(); /* charge in progress */
    bool isSinkPd();        /* input is PD (else DPDM/QC) */
    bool isSinkFastCharge();/* input fast-charge valid */
    float getChargeVoltageV();  /* negotiated charge voltage 5/7/9/12 */
    uint8_t getChargeQcState(); /* Chg_qc_state 0xD4[2:0] */

    /* -------- NTC battery temperature ----------------------------- */
    void enableNTC(bool on);
    bool isNTCEnabled();
    void setNtcCurrentUa(uint16_t ua);      /* 20 or 80 uA source */
    uint16_t getNtcRaw();
    uint16_t getNtcMillivolts();            /* NTC = ADC*0.26855 mV */
    float getNtcCelsius();                  /* estimated from R_NTC (B-3435) */

    /* -------- ADC readouts ---------------------------------------- */
    float getBatteryVoltageV();             /* VBAT  *0.26855 mV */
    float getBatteryCurrentA();             /* IBAT  *1.6785 mA (|I|) */
    float getVsysVoltageV();                /* VSYS  *1.611328 mV */
    float getVsysCurrentA();                /* IVSYS *0.671387 mA */
    float getVbusVoltageV();                /* VBUS  *1.611 mV */
    float getVinVoltageV();                 /* VIN   *1.611328 mV */
    float getVout1CurrentA();
    float getVout2CurrentA();
    float getInputCurrentA();               /* IVIN/IVBUS charging in */
    float getVbusCurrentA();                /* IVBUS charging out */
    float getVsysPowerMW();                 /* *4.431 mW */
    uint8_t getSOC();                       /* 0..100% */

    /* -------- status ---------------------------------------------- */
    uint8_t getSystemState();               /* SYS_STATE3[2:0] */
    bool isCharging();
    bool isDischarging();
    uint8_t getMosState();                  /* MOS_STATE */
    bool isCcSrcOk();                       /* TYPEC_STATE[7] - C port is a source */
    bool isCcSinkOk();                      /* TYPEC_STATE[6] - C port is a sink */
    bool isVbusOk();                        /* SYS_STATE0[4] - VBUS is active */
    void forceStandby();                    /* software shutdown (data bank 0x86) */

private:
    uint8_t  _ctl;                          /* control bank (0x74) */
    uint8_t  _data;                         /* data/ADC/status bank (0x75) */
    TwoWire *_wire;
    uint16_t _ntcUa;                        /* configured NTC source current */
    uint8_t  m_curProtocol;
};

#endif // IP5356_H
