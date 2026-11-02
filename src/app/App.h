#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "cli/CliEngine.h"
#include "config/pins.h"
#include "hardware/PowerMonitor.h"
#include "hardware/PowerManager.h"
#include "hardware/Buzzer.h"
#include "hardware/RGB.h"
#include "hardware/I2CMux.h"
#include "hardware/FRAM.h"
#include "hardware/Buttons.h"
#include "hardware/Display.h"
#include "hardware/TempSensor.h"
#include "services/TimeManager.h"
#include "services/EnergyManager.h"
#include "services/BatteryManager.h"
#include "services/SleepManager.h"
#include "services/BLEUART.h"
#include "services/IP5356Manager.h"

/* A single buzzer note: freq in Hz (0 = silence/rest), durMs = length. */
struct BuzzerNote {
    uint16_t freq;
    uint16_t durMs;
};

/* Composition root / application kernel. Owns every manager and the CLI engine
 * so that UART CLI, BLE CLI and the LVGL UI all call the same services. */
class App {
public:
    /* hardware drivers */
    PowerMonitor monitor;
    PowerManager power;
    Buzzer buzzer;
    RGB rgb;
    I2CMux mux{0x70};
    FRAM fram;
    Buttons buttons;
    Display display;
    TempSensor temps;

    /* services */
    TimeManager time;
    EnergyManager energy{monitor, fram};
    BatteryManager battery{monitor, fram};
    SleepManager sleep;
    BLEUART ble;
    IP5356Manager ipman{mux};
    CliEngine cli;

    void begin();
    void registerCoreCommands();
    void bootSequence();
    void markActivity();                 /* button/CLI use; wakes display + RGB */
    bool markActivityWoke();             /* true if this call woke an idle screen */
    void setActivityTimeoutSec(uint8_t sec);
    uint8_t activityTimeoutSec() const;
    bool postCommand(const char *cmd);   /* queue a CLI command for the CLI task */
    bool enterDeepSleep();   /* prep (backlight/buzzer off) then deep sleep; false if refused */
    void beginLightRamp();   /* start BUT4 hold-to-ramp task */
    void endLightRamp();     /* stop it on release */
    static void lightRampTask(void *p);
    void toggleLightBlink(); /* double-click: 100% + blink toggle */
    static void lightBlinkTask(void *p);
    void button4Click();     /* flashlight toggle, used on every screen */
    void button4Double();    /* 100% <-> 0% */
    void button4Long();      /* hold-to-ramp */
    void button4LongStop();  /* release ramp */
    void persistLightSelect();   /* save selected light regime to FRAM */
    void onButtonEvent(const char *event, uint8_t idx);

    /* accessors shared by CLI + UI */
    String timeString();
    const char *timeCached() const { return m_timeCache; }
    const char *dateCached() const { return m_dateCache; }
    bool wakeActive() const { return digitalRead(PIN_MCU_WAKE) == HIGH; }
    void refreshTimeCache() {
        DateTime dt = time.now();
        snprintf(m_timeCache, sizeof(m_timeCache), "%02d:%02d:%02d", dt.hour(), dt.minute(), dt.second());
        static const char *dow[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
        snprintf(m_dateCache, sizeof(m_dateCache), "%s %02d-%02d", dow[dt.dayOfTheWeek()], dt.day(), dt.month());
    }
    float batVoltage();
    float batCurrent();
    float batPower();
    float batSOC();
    float batCapacityMah();
    float energyWh(PowerChannel ch);
    bool chargeOvpLatched() const { return m_chargeOvpLatched; }
    float ovpTripVoltage() const { return m_ovpTripVoltage; }
    uint32_t ovpTripMs() const { return m_ovpTripMs; }
    float ovpCutoffVoltage() const { return m_ovpCutoffV; }
    bool setOvpCutoffVoltage(float v);
    PowerMeasurement &meas(PowerChannel ch) { return monitor.get(ch); }

    void setLight(uint8_t pct);
    uint8_t light() const;

    /* battery-full (charge done) alarm: green RGB 20% + "beep-beep" x3 */
    void startChargeDoneAlarm();
    static void chargeDoneAlarmTask(void *p);

    /* low-battery alarm: short + 2 long beeps once; dim red flash repeats
     * every 5 minutes while the battery stays low */
    void startLowBatteryAlarm();

    /* non-blocking melody player (dedicated task, yields to other tasks) */
    void playMelody(const BuzzerNote *notes, int count);
    static void playMelodyTask(void *p);

    /* ELOAD test/sweep (non-blocking task, BUT3 double-click) */
    bool startEloadSweep();
    void stopEloadSweep();
    bool eloadRunning() const { return m_eloadRunning; }
    static void eloadSweepTask(void *p);
    void eloadSetPercent(float p);
    float eloadR() const { return m_eloadR; }
    float eloadTemp() const { return m_eloadTemp; }

    /* one-shot 0->50->0 RGB pulse (non-blocking); notifies connect events */
    void rgbPulse(uint8_t r, uint8_t g, uint8_t b);
    static void rgbPulseTask(void *p);

    /* RTOS task entry points (started by main) */
    void monitorLoop();
    void batteryLoop();
    void uiLoop();
    void buttonLoop();
    void systemLoop();
    void cliLoop();

    static App *instance() { return s_inst; }

private:
    static App *s_inst;
    char m_timeCache[16] = "--:--:--";   /* fixed buffers: no cross-task String race */
    char m_dateCache[16] = "--";
    QueueHandle_t m_cmdQueue = nullptr;  /* UI/button -> CLI task command queue */
    volatile bool m_bootRgbActive = false;   /* suppress RGB updater during boot fade */
    volatile uint32_t m_lastActivityMs = 0;
    volatile bool m_activityIdle = false;
    volatile bool m_externalActive = false;   /* USB charge/load current flowing */
    volatile uint8_t m_prevBrightness = 100;
    uint32_t m_activityTimeoutMs = 60000UL;
    void applyActivityTimeout(uint8_t sec);
    volatile bool m_lightRamp = false;       /* BUT4 long-press light ramp active */
    volatile bool m_lightBlink = false;      /* double-click light blink active */
    bool m_lightSolidSel = false;            /* double-click reached solid 100% */
    uint8_t m_lightSelect = 60;              /* last/selected light regime for "on" */
    volatile bool m_chargeDoneNotified = false;   /* suppress re-alarm while full */
    volatile bool m_lowBatteryLatched = false;             /* low-battery alert fired */
    bool m_chargeOvpLatched = false;                        /* over-voltage charger cutoff */
    float m_ovpCutoffV = 4.20f;                             /* user-selectable cutoff */
    float m_ovpTripVoltage = 0;
    uint32_t m_ovpTripMs = 0;
    uint32_t m_ovpSince = 0;
    uint32_t m_ovpLastAttempt = 0;
    uint32_t m_chargerAbsentSince = 0;
    bool disableChargingIPs();
    void loadOvpCutoff();
    volatile void *m_melodyTask = nullptr;        /* running melody task handle */
    BuzzerNote *m_melodyPack = nullptr;           /* its heap note pack */
    volatile float m_eloadR = 0;                  /* last ELOAD resistance (mOhm) */
    volatile bool m_eloadRunning = false;         /* ELOAD sweep active */
    volatile bool m_eloadAbort = false;           /* cancel request for sweep task */
    volatile bool m_overtemp = false;             /* ELOAD overtemp latch */
    volatile float m_eloadTemp = 0;               /* cached ELOAD temp (0x4A) */
};









