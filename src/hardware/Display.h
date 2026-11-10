#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>
#include "measurements.h"

class App;
class DateTime;

/* LVGL display manager (ST7789 240x320) that hosts the copied UI exactly as
 * the working example (APB_TMP102_LVGL_Test_PIO): apb_ui_init + home_create
 * dashboard, with all value labels updated by an LVGL timer. */
class Display {
public:
    void begin(App &app);
    bool ready() const { return m_ready; }

    void setBrightness(uint8_t pct);   /* 0..100 via LEDC PWM (5 kHz) */
    uint8_t brightness() const { return m_bright; }

    void update();  /* run lv_timer_handler; call from UI task only */
    void notify(const char *msg, uint32_t durationMs = 4000);   /* transient on-screen toast */
    void requestScreenToggle();   /* cycle main <-> screen2 <-> screen3 <-> screen4 on next UI tick */
    void requestScreen(int n);    /* jump directly to screen 1..4 (1=main, 2=monitor, 3=config, 4=IP5356) */
    int  screen() const;          /* current screen number 1..4 */
    bool isOnConfig() const { return m_onScreen3; }

    /* Screen4 (IP5356) controls */
    bool isOnScreen4() const { return m_onScreen4; }
    void ip4CycleChannel();     /* toggle displayed PMIC 1 <-> 2 */
    void ip4PrevSetting();      /* previous config row */
    void ip4NextSetting();      /* next config row */
    void ip4ToggleSetting();    /* run/toggle the selected config */
    bool s4TermMode() const { return m_s4Mode == 1; }
    void s4ToggleMode() { m_s4Mode ^= 1; m_s4TermOff = 0; m_s4Dirty = true; }
    void s4TermScroll(int dir) {
        int d = (dir < 0) ? 1 : -1;
        m_s4TermOff += d;
        if (m_s4TermOff < 0) m_s4TermOff = 0;
        m_s4Dirty = true;
    }

    /* Screen3 config menu controls (routed by App from buttons) */
    void configLeft();
    void configRight();
    void configConfirm();
    void runItem();
    bool termMode() const { return m_cfgMode == 1; }
    void configToggleMode();        /* items <-> terminal */
    void configExitSub();           /* BUT1 long: close submenu */
    void configTermScroll(int dir); /* +/- scroll steps in terminal mode */
    bool adjustActive() const;      /* currently editing an adjustable item */
    void startRepeat(int dir);      /* BUT long: fast value repeat */
    void stopRepeat();
    static void repeatTaskFn(void *p);
    void configCancel();            /* BUT3 long while editing: restore + exit */

private:
    App *m_app = nullptr;
    bool m_ready = false;
    uint8_t m_bright = 100;
    bool m_blPending = true;   /* keep backlight off until the first frame renders */
    lv_obj_t *m_toast = nullptr;          /* transient notification label (top layer) */
    volatile bool m_toastPending = false;
    char m_toastMsg[48] = "";
    uint32_t m_toastUntil = 0;
    uint32_t m_toastDur = 4000;
    lv_obj_t *m_bt[3] = {nullptr, nullptr, nullptr};   /* Bluetooth/Berkanan rune lines */

    /* screens */
    lv_obj_t *m_screen1 = nullptr;
    lv_obj_t *m_screen2 = nullptr;
    volatile bool m_switchReq = false;
    bool m_onScreen2 = false;
    bool m_onScreen3 = false;
    lv_obj_t *m_s2a2V=nullptr, *m_s2a2A=nullptr, *m_s2a2W=nullptr, *m_s2a2Wh=nullptr;
    lv_obj_t *m_s2c1V=nullptr, *m_s2c1A=nullptr, *m_s2c1W=nullptr, *m_s2c1Wh=nullptr;
    lv_obj_t *m_s2soc=nullptr, *m_s2fill=nullptr, *m_s2batWh=nullptr;
    lv_obj_t *m_s2lblA2=nullptr, *m_s2lblC1=nullptr;

    /* screen3 config menu + terminal */
    lv_obj_t *m_screen3 = nullptr;    lv_obj_t *m_cfgItems[64] = {nullptr};
    lv_obj_t *m_cfgSubLabels[6] = {nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};
    lv_obj_t *m_termBox = nullptr;    /* scroll container for the terminal text */
    lv_obj_t *m_termLabel = nullptr;
    lv_obj_t *m_s3Clock = nullptr;
    lv_obj_t *m_s3Date = nullptr;
    lv_obj_t *m_s3SoC = nullptr;
    lv_obj_t *m_s3Runtime = nullptr;
    int m_cfgSel = 0;
    int m_cfgSub = 0;
    int m_cfgTermOff = 0;   /* terminal scroll offset in lines (0 = newest) */
    float m_cfgNum = 25.0f;   /* numeric-entry value (e.g. battery Wh) */
    bool m_cfgActive = false;
    volatile bool m_cfgDirty = true;   /* rebuild config rows on next UI tick */
    volatile bool m_cfgScrollToSel = false;  /* UI task scrolls menu to selection */
    uint8_t m_cfgMode = 0;   /* 0 = items, 1 = terminal/scroll */
    volatile bool m_repeat = false;
    volatile int m_repeatDir = 0;
    void *m_repeatTask = nullptr;
    uint8_t m_initBright = 0;   /* values at start of an edit (for cancel) */
    uint8_t m_initLight = 0;
    float m_initNum = 0;

    /* screen4: IP5356 config list (two PMICs behind the I2C mux) */
    lv_obj_t *m_screen4 = nullptr;
    bool m_onScreen4 = false;
    uint8_t m_ip4Ch = 0;          /* 0 = IP1 (mux ch0), 1 = IP2 (mux ch1) */
    lv_obj_t *m_s4Ch = nullptr;
    lv_obj_t *m_s4Box = nullptr;  /* scrollable list container */
    lv_obj_t *m_s4Rows[64] = {nullptr};
    lv_obj_t *m_s4Tele[10] = {nullptr};   /* IP values, right column */
    lv_obj_t *m_s4Rd[6] = {nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};   /* bottom-half readings */
    lv_obj_t *m_s4Hint = nullptr;
    lv_obj_t *m_termBox4 = nullptr;       /* screen4 terminal (same place as config) */
    lv_obj_t *m_termLabel4 = nullptr;
    uint8_t m_ip4Edit = 0;        /* selected row index on screen4 */
    uint8_t m_s4Mode = 0;         /* 0 = items, 1 = terminal */
    int m_s4TermOff = 0;          /* terminal scroll offset in lines */
    bool m_s4AutoWake = false;    /* auto-wake a sleeping chip on screen4 */
    uint32_t m_s4LastWake = 0;    /* throttling for auto-wake */
    bool m_s4Present = false;     /* last-known alive state of selected chip */
    bool m_s4ForceProbe = false;  /* probe despite presence cache (user action / entry) */
    volatile bool m_s4Dirty = true;   /* redraw this tick (button action) */
    volatile bool m_s4ScrollToEdit = false;  /* UI task scrolls to selected row */
    uint32_t m_s4LastProbe = 0;   /* last mux probe time (throttle live reads) */
    String m_s4Vals[64];          /* last-known config value per row */

    const char *itemCmd(int idx);
    void runSelectedSub();
    void runEnergySet();
    void runOvpSet();

    void buildBluetoothIcon();
    void updateBluetooth();            /* grey=connected, blue=not connected */
    void buildScreen2();
    void updateScreen2();
    void buildScreen3();
    void updateScreen3();
    void buildScreen4();
    void updateScreen4();
    void updateLabels();               /* LVGL timer callback */
    static void timerCb(lv_timer_t *t);
    static void paintBattery(float soc);
};






