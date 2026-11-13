#include "app/App.h"
#include "config/pins.h"
#include "config/config.h"
#include "config/battery_config.h"
#include "config/addresses.h"
#include "config/display_config.h"
#include "infra/Logger.h"
#include "infra/SerialOutput.h"
#include "infra/I2CBus.h"
#include <Wire.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

App *App::s_inst = nullptr;

#define ELOAD_PWM_FREQ 1000
#define ELOAD_PWM_RES  14
#define ELOAD_PWM_MAX  ((1 << ELOAD_PWM_RES) - 1)

/* Survives deep sleep (RTC memory) - robust light-regime persistence across
 * deep-sleep wake without relying on FRAM I2C read timing. */
RTC_DATA_ATTR uint8_t s_lightRegime = 0;

/* command registration defined in cli/commands.cpp */
void registerCommands(CliEngine &cli, App &app);

static void lightWrite(uint8_t pct) {
    static const uint32_t kFreq = 6000;
    static const uint8_t kRes = 12;
    static const uint32_t kMax = (1u << kRes) - 1;
    if (pct > 100) pct = 100;
    /* attach the light pin once - repeated  spams an LEDC error */
    static bool attached = false;
    if (!attached) {
        ledcAttachChannel(PIN_LIGHT_PWM, kFreq, kRes, LEDC_CH_LIGHT);
        attached = true;
    }
    ledcWrite(PIN_LIGHT_PWM, (uint32_t)(pct / 100.0f * kMax));
}

static uint8_t gLight = 0;
void App::setLight(uint8_t pct) { gLight = pct; lightWrite(pct); }
uint8_t App::light() const { return gLight; }

bool App::postCommand(const char *cmd) {
    if (!m_cmdQueue || !cmd) return false;
    char buf[64];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    return xQueueSend(m_cmdQueue, buf, 0) == pdTRUE;
}

void App::begin() {
    s_inst = this;
    m_lastActivityMs = millis();

    i2cBusInit();
    Logger::instance().begin();
    Logger::instance().setLevel((ApbLogLevel)fram.data().logLevel);
    Logger::instance().setEnabled(fram.data().logEnabled != 0);

    Wire.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, I2C_BUS_SPEED);
    /* No full-bus warm-up scan: the verified simple firmware inits INA226
     * directly after Wire.begin and it works fast; a wide scan perturbs the
     * ESP32-C6 i2c-ng driver. */

    /* default GPIO init matching verified examples (ELOAD already forced low in setup) */
    pinMode(PIN_ELOAD_PWM, OUTPUT);
    digitalWrite(PIN_ELOAD_PWM, LOW);
    pinMode(PIN_LIGHT_PWM, OUTPUT);
    digitalWrite(PIN_LIGHT_PWM, LOW);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    pinMode(PIN_MCU_WAKE, INPUT_PULLDOWN);
    pinMode(PIN_MCU_WAKE2, INPUT_PULLUP);

    power.begin();       Serial.println("[app] power ok");
    rgb.begin();         Serial.println("[app] rgb ok");
    buzzer.begin();      Serial.println("[app] buzzer ok");
    // mux.begin();      // temporarily disabled (per request)
    fram.begin();        Serial.println("[app] fram ok");
    loadOvpCutoff();
    monitor.begin();     Serial.println("[app] monitor(INA) ok");
    temps.begin();       Serial.println("[app] temps ok");
    time.setSource((TimeSource)fram.data().timeSource);   /* 0 = RTC, 1 = software */
    time.begin(&fram);   Serial.println("[app] time ok");
    energy.begin();      Serial.println("[app] energy ok");
    battery.begin();     Serial.println("[app] battery ok");
    sleep.begin();       Serial.println("[app] sleep ok");
    /* IP5356s sit behind the I2C mux; probing them at boot can wedge the
     * ESP32-C6 i2c-ng controller on an empty channel and break the sensor
     * bus. Leave them untouched here - accessed on demand only (screen4 /
     * CLI `ip` / `ip wake`). */

    buttons.begin([](const char *e, uint8_t i) { if (App::instance()) App::instance()->onButtonEvent(e, i); });
    Serial.println("[app] buttons ok");

    ble.begin("APB-BLE-UART");
    Serial.println("[app] BLE ok");

    /* item 2: RGB brightness default 100 (treat stored 0 as 100). */
    rgb.setBrightness(fram.data().rgbBrightness ? fram.data().rgbBrightness : 100);
    rgb.setEffect((LedEffect)fram.data().rgbEffect);
    rgb.setHeartbeat(fram.data().heartbeat != 0);
    m_lightSelect = s_lightRegime ? s_lightRegime          /* RTC memory (deep-sleep) */
                  : (fram.data().lightPercent ? fram.data().lightPercent : 60);  /* FRAM backup */
    setLight(0);                     /* flashlight off at boot */
    Serial.printf("[boot] light=%u select=%u\n", (unsigned)gLight, (unsigned)m_lightSelect);

    registerCommands(cli, *this);
    m_cmdQueue = xQueueCreate(8, 64);   /* config-menu commands -> CLI task */

    LOG_INFO("%s firmware %s (%s)", FW_NAME, FW_VERSION, FW_BUILD);
}

/* Non-blocking boot sequence: beep-beep + light blue glow, run as a background
 * task so it does not block setup and so the buzzer/RGB auto-managers (which
 * live in other tasks) keep working in parallel. */
bool App::enterDeepSleep() {    /* Refuse if MCU_WAKE is already active - several beeps as feedback */
    if (digitalRead(PIN_MCU_WAKE) == HIGH) {
        LOG_WARN("MCU_WAKE active - deep sleep refused");
        display.notify("SLEEP BLOCKED", "USB ON", 4500, 0xF59E0B);
        for (int i = 0; i < 3; i++) {
            buzzer.on();
            vTaskDelay(pdMS_TO_TICKS(80));
            buzzer.off();
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        return false;
    }
    energy.flush();
    time.saveNow();                     /* persist the software clock before sleeping */
    persistLightSelect();               /* guarantee chosen regime survives deep-sleep */
    eloadSetPercent(0);                 /* do not leave the ELOAD on in deep sleep */
    setLight(0);                        /* flashlight off */
    rgb.powerOff();                     /* force all LEDs off, including overlays */
    buzzer.off();
    ledcWrite(PIN_DPL_PWM, 0);            /* backlight off so no white screen on wake */
    vTaskDelay(pdMS_TO_TICKS(60));        /* match the working reference firmware */
    sleep.enterDeepSleep();
    return true;
}

void App::bootSequence() {
    /* Charger present: skip the boot show entirely. A brownout reset while
     * charging would otherwise loop beeps + glow, which reads as a constantly
     * lit strip (SK6812 pixels hold the last colour across the reset). */
    monitor.sampleChannel(CH_C1);
    monitor.sampleChannel(CH_C2);   /* fresh read: monitor task may not have run yet */
    if (monitor.get(CH_C1).voltage > 3.7f || monitor.get(CH_C2).voltage > 3.7f) {
        rgb.off();
        return;
    }

    buzzer.setFrequency(2000);

    /* Beep-beep first (real 2 kHz PWM tone from the Buzzer driver). No NeoPixel
     * is driving yet, so the LEDC timer cannot corrupt any RMT transmission. */
    buzzer.on();
    vTaskDelay(pdMS_TO_TICKS(110));
    buzzer.off();
    vTaskDelay(pdMS_TO_TICKS(90));
    buzzer.on();
    vTaskDelay(pdMS_TO_TICKS(110));
    buzzer.off();
    vTaskDelay(pdMS_TO_TICKS(80));

    /* Then glow: fast 0 -> 100 % (MAX), slower 100 -> 0 %, light blue
     * (no buzzer PWM now -> no LED corruption) */
    rgb.setColor(100, 200, 255, 0);   /* light blue */
    rgb.setEffect(LED_EFFECT_SOLID);
    m_bootRgbActive = true;   /* only the boot task drives the strip during the fade */
    for (int bb = 0; bb <= 100; bb += 1) {          /* fast rise on (2 ms/step) */
        rgb.setBrightness((uint8_t)bb);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    for (int bb = 100; bb >= 0; bb -= 1) {          /* slower fall off (5 ms/step) */
        rgb.setBrightness((uint8_t)bb);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    rgb.off();                                      /* 0 % */
    rgb.setBrightness(fram.data().rgbBrightness ? fram.data().rgbBrightness : 100);
    m_bootRgbActive = false;   /* boot fade done - hand the strip back to rgb.update() */
}

void App::persistLightSelect() {
    s_lightRegime = m_lightSelect;   /* RTC memory - instant, survives deep sleep */
    fram.data().lightPercent = m_lightSelect;
    fram.save();
    Serial.printf("[persist] lightPercent=%u\n", (unsigned)m_lightSelect);
}

String App::timeString() { return time.nowISO(); }

void App::lightRampTask(void *p) {
    App *a = (App *)p;
    static const uint8_t lvls[6] = {0, 50, 60, 80, 90, 100};
    /* start from the NEXT regime above the CURRENT light level (e.g. 100->0,
     * 60->80); from a non-matching/off (0) level start at 50 */
    int cur = (int)a->light();
    int li = 0;
    for (int i = 0; i < 6; i++)
        if (lvls[i] == (uint8_t)cur) { li = (i + 1) % 6; break; }
    while (a->m_lightRamp) {                 /* circular 60->80->100->50->60.. */
        a->setLight(lvls[li]);
        a->m_lightSelect = lvls[li];         /* remember selected regime */
        a->buzzer.on();                      /* beep on each regime change */
        vTaskDelay(pdMS_TO_TICKS(40));
        a->buzzer.off();
        li = (li + 1) % 6;
        vTaskDelay(pdMS_TO_TICKS(660));      /* ~700 ms per regime (incl. beep) */
    }
    a->m_lightRamp = false;
    a->persistLightSelect();                 /* save chosen regime to FRAM */
    vTaskDelete(NULL);
}

void App::beginLightRamp() {
    if (m_lightRamp) return;
    m_lightRamp = true;
    xTaskCreate(&App::lightRampTask, "lramp", 2048, this, 3, NULL);
}

void App::endLightRamp() { m_lightRamp = false; }

void App::lightBlinkTask(void *p) {
    App *a = (App *)p;
    while (a->m_lightBlink) {
        if (!a->m_lightBlink) break;
        a->setLight(100);
        vTaskDelay(pdMS_TO_TICKS(250));
        if (!a->m_lightBlink) break;
        a->setLight(0);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    vTaskDelete(NULL);   /* do not force light state here - let the caller decide */
}

void App::toggleLightBlink() {
    if (m_lightBlink) {
        m_lightBlink = false;                /* stop blink -> solid 100% */
        setLight(100);
    } else if (m_lightSolidSel) {
        setLight(100);                       /* was solid 100% -> now blink */
        m_lightBlink = true;
        xTaskCreate(&App::lightBlinkTask, "lblink", 2048, this, 3, NULL);
    } else {
        setLight(100);                       /* first double-click: solid 100% */
        m_lightSolidSel = true;
    }
}

void App::button4Click() {
    if (m_lightBlink) m_lightBlink = false;
    uint8_t target = gLight > 0 ? 0 : (m_lightSelect ? m_lightSelect : 60);
    buzzer.on();
    vTaskDelay(pdMS_TO_TICKS(40));
    buzzer.off();
    setLight(target);
}

void App::button4Double() {
    endLightRamp();
    m_lightSelect = (gLight == 100) ? 0 : 100;
    setLight(gLight == 100 ? 0 : 100);
    persistLightSelect();
    buzzer.on();
    vTaskDelay(pdMS_TO_TICKS(150));
    buzzer.off();
}

void App::button4Long() {
    beginLightRamp();
}

void App::button4LongStop() {
    endLightRamp();
}
float App::batVoltage()    { return battery.voltage(); }
float App::batCurrent()    { return battery.current(); }
float App::batPower()      { return battery.power(); }
float App::batSOC()        { return battery.soc(); }
float App::batCapacityMah(){ return battery.capacityMah(); }
float App::energyWh(PowerChannel ch) { return energy.energyWh(ch); }

void App::markActivity() {
    (void)markActivityWoke();
}

bool App::markActivityWoke() {
    bool idle = m_activityIdle;
    m_lastActivityMs = millis();
    if (!idle) return false;
    m_activityIdle = false;
    uint8_t b = m_prevBrightness ? m_prevBrightness : DISP_BL_DEFAULT;
    display.setBrightness(b);
    rgb.setIdle(false);
    if (m_chargeRgbBlanked) {               /* restart the charging indication window */
        m_chargeRgbBlanked = false;
        m_chargeRgbSince = millis();
    }
    Serial.println("[ui] activity -> wake");
    return true;
}

void App::applyActivityTimeout(uint8_t sec) {
    if (sec == 255)      m_activityTimeoutMs = 0;                    /* off */
    else if (sec == 0)   m_activityTimeoutMs = ACTIVITY_TIMEOUT_MS;  /* default */
    else                 m_activityTimeoutMs = (uint32_t)sec * 1000UL;
}

void App::setActivityTimeoutSec(uint8_t sec) {
    fram.data().activityTimeoutSec = sec;
    fram.save();
    applyActivityTimeout(sec);
    Serial.printf("[ui] activity timeout set to %u s\r\n", sec);
}

uint8_t App::activityTimeoutSec() const {
    return fram.data().activityTimeoutSec;
}

void App::loadOvpCutoff() {
    uint16_t mv = 0;
    if (fram.ovpLoad(mv)) m_ovpCutoffV = (float)mv / 1000.0f;
    else                  m_ovpCutoffV = 4.20f;
}

bool App::setOvpCutoffVoltage(float v) {
    if (v < 3.00f || v > 4.20f) return false;
    uint16_t mv = (uint16_t)((v * 1000.0f) / 10.0f + 0.5f) * 10u;
    if (mv < 3000 || mv > 4200) return false;
    m_ovpCutoffV = (float)mv / 1000.0f;
    bool saved = fram.ovpSave(mv);
    Serial.printf("[batt] OVP cutoff set to %.2f V%s\r\n",
                  m_ovpCutoffV, saved ? "" : " (not persisted)");
    return true;
}

void App::onButtonEvent(const char *event, uint8_t idx) {
    bool woke = markActivityWoke();
    String ev(event);

    /* First short press after the display blanked only wakes the screen,
     * except BUT4 which is the dedicated flashlight control. */
    if (woke && ev == "click" && idx <= 2) return;

    /* Screen3 (config) has its own button layout */
    if (display.isOnConfig()) {
        if (ev == "click") {
            if (idx == 3) { button4Click(); return; }   /* BUT4: flashlight */
            buzzer.beep(20);                    /* ack each click */
            if (display.termMode()) {
                if (idx == 0) display.configTermScroll(-1);   /* BUT1 up   */
                else if (idx == 1) display.configTermScroll(1);  /* BUT2 down */
            } else {
                if (idx == 0)      display.configLeft();      /* BUT1 left  */
                else if (idx == 1) display.configRight();     /* BUT2 right */
                else if (idx == 2) display.configConfirm();   /* BUT3 OK    */
            }
        } else if (ev == "double") {
            if (idx == 3) button4Double();         /* BUT4: 100% <-> 0% */
            else if (idx == 1) {                   /* BUT2 double: wake IP5356s */
                if (!postCommand("wakeup")) Serial.println("[ui] cmd queue full: wakeup");
                buzzer.beep(40);
            }
        } else if (ev == "long") {
            if (display.adjustActive() && idx == 2) {
                display.configCancel();             /* BUT3 long while editing: cancel */
            } else if (display.adjustActive() && (idx == 0 || idx == 1)) {
                display.startRepeat(idx == 0 ? -1 : 1);     /* fast value repeat */
            } else if (idx == 0) display.configExitSub();
            else if (idx == 2) display.configToggleMode();
            else if (idx == 1) { display.requestScreenToggle(); buzzer.beep(40); }
            else if (idx == 3) button4Long();      /* BUT4 hold: light ramp */
        } else if (ev == "longstop") {
            if (idx == 0 || idx == 1) display.stopRepeat();
            else if (idx == 3) button4LongStop();
        }
        return;
    }

    /* idx: 0=BUT1(OK/ENTER), 1=BUT2(DOWN), 2=BUT3(UP), 3=BUT4(BACK) */

    /* Screen4 (IP5356 monitor) button layout - same conventions as config */
    if (display.isOnScreen4()) {
        if (ev == "click") {
            if (idx == 3) { button4Click(); return; }   /* BUT4: flashlight */
            buzzer.beep(20);
            if (display.s4TermMode()) {
                if (idx == 0) display.s4TermScroll(-1);           /* BUT1 up   */
                else if (idx == 1) display.s4TermScroll(1);       /* BUT2 down */
            } else {
                if (idx == 0) display.ip4PrevSetting();           /* BUT1 left  */
                else if (idx == 1) display.ip4NextSetting();      /* BUT2 right */
                else if (idx == 2) display.ip4ToggleSetting();    /* BUT3 OK    */
            }
        } else if (ev == "double") {
            if (idx == 3) button4Double();         /* BUT4: 100% <-> 0% */
            else if (idx == 1) {                   /* BUT2 double: wake IP5356s */
                if (!postCommand("wakeup")) Serial.println("[ui] cmd queue full: wakeup");
                buzzer.beep(40);
            }
        } else if (ev == "long") {
            if (idx == 2) display.s4ToggleMode();                 /* BUT3 long: items <-> terminal */
            else if (idx == 1) { display.requestScreenToggle(); buzzer.beep(40); }     /* BUT2 long: next screen */
            else if (idx == 0) {                                  /* BUT1 long: exit terminal / screen */
                if (display.s4TermMode()) display.s4ToggleMode();
                else display.requestScreen(1);
            } else if (idx == 3) button4Long();    /* BUT4 hold: light ramp */
        } else if (ev == "longstop") {
            if (idx == 3) button4LongStop();
        }
        return;
    }

    if (ev == "click") {
        /* Screen2: BUT3 cycles top/bottom selection; while active BUT1/BUT2
         * change the selected half's port. */
        if (display.screen() == 2 && idx == 2) {
            display.s2SelectNext();
            buzzer.beep(40);
            return;
        }
        if (display.screen() == 2 && display.s2Selection() != 0 &&
            (idx == 0 || idx == 1)) {
            if (idx == 0) display.s2PortPrev(); else display.s2PortNext();
            buzzer.beep(40);
            return;
        }
        if (idx == 3) {                 /* BUT4 short: flashlight on/off */
            button4Click();
        } else if (idx == 0) {          /* BUT1 OK (was BUT3): short beep */
            buzzer.beep(60);
        } else if (idx == 1) {          /* BUT2: short beep + screen backlight step */
            static const uint8_t steps[] = {0, 1, 20, 30, 50, 80, 100};
            static uint8_t si = 0;
            buzzer.on();
            vTaskDelay(pdMS_TO_TICKS(50));
            buzzer.off();
            display.setBrightness(steps[si]);
            si = (uint8_t)((si + 1) % 7);
        } else if (idx == 2) {          /* BUT3 up (was BUT1): light +10 */
            uint8_t l = gLight + 10;
            if (l > 100) l = 100;
            setLight(l);
        }
    } else if (ev == "double") {
        if (idx == 3) {                 /* BUT4 double: 100% <-> 0% + beep */
            button4Double();
        } else if (idx == 1) {          /* BUT2 double: wake IP5356s */
            if (!postCommand("wakeup")) Serial.println("[ui] cmd queue full: wakeup");
            buzzer.beep(40);
        } else if (idx == 2) {          /* BUT3 double: ELOAD resistance sweep */
            startEloadSweep();
        }
    } else if (ev == "long") {
        if (idx == 1) {                 /* BUT2 long: cycle screens */
            display.requestScreenToggle();
            buzzer.beep(40);
        } else if (idx == 3) {          /* BUT4 long hold: ramp 1/20/50/80/100 */
            button4Long();
        } else if (idx == 2) {          /* BUT3 long: beep + red warning toggle */
            buzzer.on();
            vTaskDelay(pdMS_TO_TICKS(80));
            buzzer.off();
            rgb.setWarning(!rgb.warning());
            if (rgb.warning()) rgb.setEffect(LED_EFFECT_WARNING);
            else rgb.setEffect(LED_EFFECT_OFF);
        } else if (idx == 0) {          /* BUT1 long (was BUT3): deep sleep */
            if (digitalRead(PIN_MCU_WAKE) == HIGH) {
                /* wake line active - refuse with 3 separate beeps */
                LOG_WARN("MCU_WAKE active - not sleeping");
                display.notify("SLEEP BLOCKED", "USB ON", 4500, 0xF59E0B);
                for (int i = 0; i < 3; i++) { buzzer.on(); vTaskDelay(pdMS_TO_TICKS(80)); buzzer.off(); vTaskDelay(pdMS_TO_TICKS(80)); }
            } else {
                buzzer.on();
                vTaskDelay(pdMS_TO_TICKS(300));   /* beep up to 300 ms */
                buzzer.off();
                enterDeepSleep();
            }
        }
    } else if (ev == "longstop") {
        if (idx == 3) button4LongStop();   /* release stops the BUT4 light ramp */
    }
}

void App::eloadSetPercent(float p) {
    power.setELoadPercent(p);
}

bool App::startEloadSweep() {
    if (m_eloadRunning) return false;
    if (batVoltage() < RESISTANCE_MIN_CELL_VOLTAGE) {
        display.notify("ELOAD BLOCKED", "LOW BAT", 4500, 0xEF4444);
        return false;
    }
    if (m_eloadTemp >= ELOAD_TEST_MAX_TEMP_C) {
        display.notify("ELOAD BLOCKED", "OVERTEMP", 4500, 0xEF4444);
        return false;
    }

    m_eloadAbort = false;
    m_eloadRunning = true;
    m_eloadR = 0;
    if (xTaskCreate(eloadSweepTask, "eload", 4096, this, 2, NULL) != pdPASS) {
        m_eloadRunning = false;
        return false;
    }
    display.notify("ELOAD", "START", 4500, 0xF59E0B);
    return true;
}

void App::stopEloadSweep() {
    m_eloadAbort = true;
    eloadSetPercent(0);
}

void App::eloadSweepTask(void *p) {
    App *a = (App *)p;
    SerialOutput out;
    a->eloadSetPercent(0);
    vTaskDelay(pdMS_TO_TICKS(300));
    /* "ty-dyy-dyyy" start cue */
    {
        static const BuzzerNote startCue[] = { {659, 90}, {880, 120}, {1175, 220} };
        a->playMelody(startCue, (int)(sizeof(startCue) / sizeof(startCue[0])));
    }
    out.println("ELOAD test 10s");
    a->eloadSetPercent(0);
    vTaskDelay(pdMS_TO_TICKS(300));
    a->monitor.sampleChannel(CH_ELOAD);
    float v0 = a->monitor.get(CH_ELOAD).voltage;
    uint32_t t0 = millis();
    int lastSec = 10;
    const char *abortReason = nullptr;
    float rSum = 0.0f;
    int rCount = 0;

    /* 51 points, 0..50 %, fast 180 ms steps -> ~10 s total */
    for (int step = 0; step <= 50; step++) {
        if (a->m_eloadAbort) { abortReason = "cancelled"; break; }

        float t = a->temps.readC(2);
        if (isnan(t) || t >= ELOAD_TEST_MAX_TEMP_C) {
            abortReason = "overtemp";
            break;
        }
        if (a->batVoltage() < RESISTANCE_MIN_CELL_VOLTAGE) {
            abortReason = "undervoltage";
            break;
        }

        a->eloadSetPercent((float)step);
        vTaskDelay(pdMS_TO_TICKS(180));
        if (a->m_eloadAbort) { abortReason = "cancelled"; break; }
        a->monitor.sampleChannel(CH_ELOAD);
        const PowerMeasurement &pm = a->monitor.get(CH_ELOAD);

        if (fabsf(pm.current) > RESISTANCE_MAX_LOAD_A) {
            abortReason = "overcurrent";
            break;
        }
        if (pm.voltage < RESISTANCE_MIN_CELL_VOLTAGE) {
            abortReason = "undervoltage";
            break;
        }

        if (step > 0 && fabsf(pm.current) > 0.01f) {
            float r = (v0 - pm.voltage) / fabsf(pm.current) * 1000.0f;
            if (r >= 0) {
                a->m_eloadR = r;
                rSum += r;
                rCount++;
                a->buzzer.beep(25);
            }
        }

        int remain = 10 - (int)((millis() - t0) / 1000UL);
        if (remain != lastSec && remain > 0) {
            out.printf("%ds  V %.2f  I %.2f  P %.2f  R %.1f mOhm\r\n",
                       remain, pm.voltage, pm.current, pm.power, a->m_eloadR);
            lastSec = remain;
        }
    }

    a->eloadSetPercent(0);
    a->monitor.sampleChannel(CH_ELOAD);
    const PowerMeasurement &pm = a->monitor.get(CH_ELOAD);
    if (rCount > 0) a->m_eloadR = rSum / (float)rCount;
    if (abortReason) {
        out.printf("ELOAD test aborted: %s\r\n", abortReason);
        a->buzzer.beep(300);
        const char *ttl = "ELOAD ABORT";
        if      (strcmp(abortReason, "cancelled") == 0)    ttl = "ELOAD CANCELLED";
        else if (strcmp(abortReason, "overtemp") == 0)     ttl = "ELOAD OVERTEMP";
        else if (strcmp(abortReason, "undervoltage") == 0) ttl = "ELOAD LOW BAT";
        else if (strcmp(abortReason, "overcurrent") == 0)  ttl = "ELOAD OVERCURR";
        a->display.notify(ttl, "ABORTED", 4500, 0xEF4444);
    } else {
        char v[16];
        snprintf(v, sizeof(v), "%.0f mOhm", a->m_eloadR);
        a->display.notify("ELOAD DONE", v, 4500, 0x22C55E);
    }
    out.printf("ELOAD result: R %.1f mOhm | V %.2f V | I %.2f A | P %.2f W | T %.1f C\r\n",
               a->m_eloadR, pm.voltage, pm.current, pm.power, a->eloadTemp());
    if (!abortReason) a->buzzer.beep(500);
    a->m_eloadAbort = false;
    a->m_eloadRunning = false;
    vTaskDelete(NULL);
}

struct PulseCtx { App *a; uint8_t r, g, b; };

void App::rgbPulseTask(void *p) {
    PulseCtx *c = (PulseCtx *)p;
    RGB &rgb = c->a->rgb;
    LedEffect prevEff = rgb.effect();
    rgb.setColor(c->r, c->g, c->b, 0);
    rgb.setEffect(LED_EFFECT_SOLID);
    rgb.setOn();
    Serial.printf("[rgb] pulse in r=%u g=%u b=%u\r\n", c->r, c->g, c->b);
    const float pih = 1.5707963f;
    const int N = 24;
    for (int i = 0; i <= N; i++) {
        float t = (float)i / N;
        rgb.setBrightness((uint8_t)(50.0f * sinf(t * pih)));
        vTaskDelay(pdMS_TO_TICKS(7));
    }
    Serial.printf("[rgb] pulse peak bri=%u\r\n", 50);
    for (int i = 0; i <= N; i++) {
        float t = (float)i / N;
        rgb.setBrightness((uint8_t)(50.0f * cosf(t * pih)));
        vTaskDelay(pdMS_TO_TICKS(7));
    }
    uint8_t def = c->a->fram.data().rgbBrightness ? c->a->fram.data().rgbBrightness : 100;
    rgb.setBrightness(def);
    rgb.setEffect(prevEff);
    if (prevEff == LED_EFFECT_OFF) rgb.off(); else rgb.setOn();
    Serial.println("[rgb] pulse done");
    c->a->m_rgbPulseActive = false;
    delete c;
    vTaskDelete(NULL);
}

void App::rgbPulse(uint8_t r, uint8_t g, uint8_t b) {
    /* Serialize: overlapping pulse tasks restore stale effect/colour state and
     * can latch a constant SOLID colour on the strip. */
    if (m_rgbPulseActive) return;
    m_rgbPulseActive = true;
    PulseCtx *c = new PulseCtx{this, r, g, b};
    if (xTaskCreate(rgbPulseTask, "rgbpulse", 4096, c, 2, NULL) != pdPASS) {
        m_rgbPulseActive = false;
        delete c;
    }
}

bool App::disableChargingIPs() {
    /* Do not pulse the IP wake line here: waking a PMIC while it is charging
     * can disturb the power rail. The charging PMIC is already awake, so a
     * direct mux session is sufficient. Probe only the channel that has
     * charger voltage; touching an empty channel can wedge i2c-ng. */
    uint8_t chs[2];
    int n = 0;
    if (monitor.get(CH_C1).voltage > 3.7f) chs[n++] = 0;
    if (monitor.get(CH_C2).voltage > 3.7f) chs[n++] = 1;
    if (n == 0) return false;

    bool any = false;
    for (int i = 0; i < n; i++) {
        uint8_t ch = chs[i];
        if (!ipman.begin(ch)) continue;
        IP5356 &s = ipman.current();
        /* En_Charger=0 also drops the output MOS (USB-A ports on the same
         * PMIC), so remember whether any output was live and re-arm it.
         * setOutputsMos() is the only reliable re-arm for VOUT1/VOUT2. */
        bool outOn = s.isOutputEnabled(IP5356::Port::VOUT1) ||
                     s.isOutputEnabled(IP5356::Port::VOUT2);
        s.enableCharger(false);
        if (outOn) s.setOutputsMos(true);
        ipman.end();
        any = true;
    }
    return any;
}

bool App::enableChargingIPs() {
    /* Counterpart of disableChargingIPs: restore charging on USB-C inputs
     * after an OVP cutoff, once the latch is clear (new insertion / boot). */
    uint8_t chs[2];
    int n = 0;
    if (monitor.get(CH_C1).voltage > 3.7f) chs[n++] = 0;
    if (monitor.get(CH_C2).voltage > 3.7f) chs[n++] = 1;
    if (n == 0) return false;

    bool any = false;
    for (int i = 0; i < n; i++) {
        uint8_t ch = chs[i];
        if (!ipman.begin(ch)) continue;
        IP5356 &s = ipman.current();
        s.enableCharger(true);
        /* Re-detect/open the loaded USB ports: disabling the charger on this
         * PMIC also drops its output MOS, so an attached USB-A load needs the
         * re-detect sequence to come back automatically. */
        s.setOutputsMos(true);
        ipman.end();
        any = true;
    }
    return any;
}

void App::monitorLoop() {
    /* INA226 are inited in setup (like the verified simple firmware). Just a
     * lightweight fallback re-probe here if SYS wasn't found yet. */
    vTaskDelay(pdMS_TO_TICKS(400));
    if (!monitor.present(CH_SYS)) monitor.begin();
    /* Probe the mux once on the settled bus. Do NOT probe the IP5356
     * channels at boot: an empty/standby mux channel can wedge the C6
     * i2c-ng driver. IPs are probed on demand (screen4, wakeup, CLI). */
    if (!mux.isPresent()) mux.begin();
    mux.setMask(0);
    /* Re-read FRAM config now that the i2c-ng bus has settled (deep-sleep wake).
     * If still invalid/uninitialized, write defaults once. Restore the light
     * regime from the (possibly just-loaded) config. */
    if (fram.isPresent() && !fram.reloadConfig()) {
        persist_defaults(fram.data());
        fram.save();
    }
    loadOvpCutoff();
    applyActivityTimeout(fram.data().activityTimeoutSec);
    m_lightSelect = s_lightRegime ? s_lightRegime          /* RTC memory first (deep-sleep) */
                  : (fram.data().lightPercent ? fram.data().lightPercent : 60);
    Serial.printf("[mon] config reload: lightPercent=%u select=%u valid=%u\n",
                  (unsigned)fram.data().lightPercent, (unsigned)m_lightSelect,
                  (unsigned)(fram.storageValid() ? 1 : 0));
    if (!time.present()) time.begin(&fram);
    for (;;) {
        monitor.sampleAll();
        energy.update();
        refreshTimeCache();
        time.poll();   /* software clock: persist every 60 s */
        /* ELOAD overtemperature protection: TMP102 0x4A >= 100C -> ELOAD off */
        {
            float et = temps.readC(2);
            if (isnan(et)) {
                if (!m_overtemp) {
                    m_overtemp = true;
                    Serial.println("[temp] ELOAD sensor invalid -> ELOAD off");
                }
                eloadSetPercent(0);
            } else {
                m_eloadTemp = et;
                if (et >= 100.0f) {
                    if (!m_overtemp) { m_overtemp = true; Serial.println("[temp] OVERTEMP ELOAD"); }
                    eloadSetPercent(0);
                } else {
                    m_overtemp = false;
                }
            }
        }
        /* BLE connect -> small "ty-dy-dy" sound (once) */
        {
            static bool blePrev = ble.connected();
            bool conn = ble.connected();
            if (conn && !blePrev) {
                static const BuzzerNote tydy[] = {
                    {523, 110}, {659, 110}, {784, 150},
                };
                playMelody(tydy, (int)(sizeof(tydy) / sizeof(tydy[0])));
            }
            blePrev = conn;
        }
        /* connect notifications: USB-C charger -> green pulse, USB-A load -> orange pulse */
        {
            static bool prevChg = false;
            static bool prevLd  = false;
            static uint32_t lastConnectPulseMs = 0;   /* re-arm window vs current flicker */
            uint32_t detNow = millis();
            float vc = monitor.get(CH_C1).voltage;
            if (monitor.get(CH_C2).voltage > vc) vc = monitor.get(CH_C2).voltage;
            float ic = (monitor.get(CH_C1).current < 0 ? -monitor.get(CH_C1).current : monitor.get(CH_C1).current)
                     + (monitor.get(CH_C2).current < 0 ? -monitor.get(CH_C2).current : monitor.get(CH_C2).current);
            bool chg = (vc >= 1.0f) && (ic > 0.05f);
            m_charging = chg;
            /* Charger presence is voltage-based: with En_Charger=0 the port
             * draws no current, so a current edge would never re-enable it. */
            static bool prevSrcPresent = false;
            bool srcPresent = (monitor.get(CH_C1).voltage >= 3.7f) ||
                              (monitor.get(CH_C2).voltage >= 3.7f);
            if (srcPresent && !prevSrcPresent && !m_chargeOvpLatched) {
                if (enableChargingIPs())
                    Serial.println("[batt] charger present -> charging enabled");
            }
            prevSrcPresent = srcPresent;
            if (chg && !prevChg && (uint32_t)(detNow - lastConnectPulseMs) >= 5000UL) {
                lastConnectPulseMs = detNow;
                Serial.println("[det] charger connected -> emerald pulse");
                rgbPulse(0, 200, 120);
            }
            prevChg = chg;

            float va = monitor.get(CH_A1).voltage;
            if (monitor.get(CH_A2).voltage > va) va = monitor.get(CH_A2).voltage;
            float ia = (monitor.get(CH_A1).current < 0 ? -monitor.get(CH_A1).current : monitor.get(CH_A1).current)
                     + (monitor.get(CH_A2).current < 0 ? -monitor.get(CH_A2).current : monitor.get(CH_A2).current);
            bool ld = (va >= 3.0f) && (ia > 0.05f);
            if (ld && !prevLd && (uint32_t)(detNow - lastConnectPulseMs) >= 5000UL) {
                lastConnectPulseMs = detNow;
                Serial.println("[det] load connected -> orange-yellow pulse");
                rgbPulse(255, 190, 40);
            }
            prevLd = ld;
            m_externalActive = chg || ld || m_eloadRunning;   /* active charge/load keeps UI awake */

            /* Wake an idle UI on a NEW USB connection. Each source must be
             * absent for 5 s before it can wake again, so load/current noise
             * cannot keep the screen awake indefinitely. */
            uint32_t now = millis();
            bool presenceNow = (vc >= 3.0f) || (va >= 3.0f);
            bool loadNow = (ic > 0.05f) || (ia > 0.05f);
            static bool usbPresenceSeen = false;
            static bool usbLoadSeen = false;
            static bool presenceArmed = false;
            static bool loadArmed = false;
            static uint32_t noPresenceSince = 0;
            static uint32_t noLoadSince = 0;

            if (presenceNow) {
                if (!usbPresenceSeen && presenceArmed) {
                    Serial.println("[det] USB connect -> wake");
                    markActivity();
                }
                usbPresenceSeen = true;
                presenceArmed = false;
                noPresenceSince = 0;
            } else {
                if (!noPresenceSince) noPresenceSince = now;
                usbPresenceSeen = false;
                if ((uint32_t)(now - noPresenceSince) >= 5000UL) presenceArmed = true;
            }

            if (loadNow) {
                if (!usbLoadSeen && loadArmed) {
                    Serial.println("[det] USB load -> wake");
                    markActivity();
                }
                usbLoadSeen = true;
                loadArmed = false;
                noLoadSince = 0;
            } else {
                if (!noLoadSince) noLoadSince = now;
                usbLoadSeen = false;
                if ((uint32_t)(now - noLoadSince) >= 5000UL) loadArmed = true;
            }

            /* Debounced MCU_WAKE LOW -> HIGH edge. */
            static uint8_t wakeHighCount = 0;
            bool wake = wakeActive();
            if (wake) {
                if (wakeHighCount < 3) wakeHighCount++;
            } else {
                wakeHighCount = 0;
            }
            if (wakeHighCount == 3) {
                Serial.println("[det] MCU_WAKE -> wake");
                markActivity();
                wakeHighCount = 4;
            }
        }
        /* battery over-voltage protection: force the IP5356 chargers off */
        {
            float vbat = batVoltage();
            bool chargerPresent = (monitor.get(CH_C1).voltage > 3.7f) ||
                                  (monitor.get(CH_C2).voltage > 3.7f);
            uint32_t now = millis();
            SerialOutput out;

            if (!chargerPresent) {
                if (!m_chargerAbsentSince) m_chargerAbsentSince = now;
                if (m_chargeOvpLatched &&
                    (uint32_t)(now - m_chargerAbsentSince) >= BATTERY_OVP_CLEAR_MS) {
                    m_chargeOvpLatched = false;
                    m_ovpSince = 0;
                    m_ovpLastAttempt = 0;
                    out.println("[batt] OVP latch cleared (charger disconnected)");
                    display.notify("OVP", "CLEARED", 4500, 0x22C55E);
                }
            } else {
                m_chargerAbsentSince = 0;

                if (!m_chargeOvpLatched) {
                    if (vbat >= m_ovpCutoffV) {
                        if (!m_ovpSince) m_ovpSince = now;
                        if ((uint32_t)(now - m_ovpSince) >= BATTERY_OVP_DEBOUNCE_MS) {
                            m_chargeOvpLatched = true;
                            m_ovpSince = now;
                            m_ovpTripVoltage = vbat;
                            m_ovpTripMs = now;
                            m_ovpLastAttempt = now;
                            out.printf("[batt] OVERVOLTAGE %.2f V >= %.2f V cutoff -> chargers OFF (latched)\r\n", vbat, m_ovpCutoffV);
                            display.notify("OVERVOLTAGE", "CHG OFF", 4500, 0xEF4444);
                            if (!disableChargingIPs())
                                out.println("[batt] OVP: no IP5356 charger acknowledged the cutoff");
                        }
                    } else {
                        m_ovpSince = 0;
                    }
                } else if ((monitor.get(CH_C1).current > 0.05f ||
                            monitor.get(CH_C2).current > 0.05f) &&
                           (uint32_t)(now - m_ovpLastAttempt) >= 2000UL) {
                    /* The PMIC may re-enable charging after a reset/insertion.
                     * Only USB-C input current may trigger this: a USB-A load
                     * also discharges the pack (negative batCurrent) but must
                     * not be disturbed by OVP. */
                    m_ovpLastAttempt = now;
                    out.println("[batt] OVP: charger re-enabled -> forcing OFF again");
                    if (!disableChargingIPs())
                        out.println("[batt] OVP: no IP5356 charger acknowledged the cutoff");
                }
            }
        }
        /* pack charging session notifications: amber on start, green Wh on stop */
        {
            uint32_t cnow = millis();
            bool into = batCurrent() < -0.10f;   /* negative = into the pack */
            if (!m_chargeRpt) {
                if (into) {
                    if (!m_chargeEdgeMs) m_chargeEdgeMs = cnow;
                    else if ((uint32_t)(cnow - m_chargeEdgeMs) >= 1000UL) {
                        m_chargeRpt = true;
                        m_chargeEdgeMs = 0;
                        m_chargeLastMs = cnow;
                        m_chargeWh = 0.0f;
                        char val[12];
                        snprintf(val, sizeof(val), "%.0f%%", batSOC());
                        display.notify("CHARGING", val, 4500, 0xF59E0B);
                        Serial.printf("[batt] charging started (%s)\r\n", val);
                    }
                } else {
                    m_chargeEdgeMs = 0;
                }
            } else {
                float dt = (float)(cnow - m_chargeLastMs) / 1000.0f;
                if (dt > 2.0f) dt = 2.0f;        /* tolerate scheduling gaps */
                m_chargeLastMs = cnow;
                float pin = -batPower();         /* positive while charging */
                if (pin > 0.0f) m_chargeWh += pin * dt / 3600.0f;
                if (batCurrent() > -0.03f) {
                    if (!m_chargeEdgeMs) m_chargeEdgeMs = cnow;
                    else if ((uint32_t)(cnow - m_chargeEdgeMs) >= 2000UL) {
                        m_chargeRpt = false;
                        m_chargeEdgeMs = 0;
                        if (m_chargeWh >= 0.01f) {
                            char val[16];
                            snprintf(val, sizeof(val), "%.2f Wh", m_chargeWh);
                            display.notify("CHARGED", val, 4500, 0x22C55E);
                            Serial.printf("[batt] charging stopped, session %s\r\n", val);
                        }
                    }
                } else {
                    m_chargeEdgeMs = 0;
                }
            }
        }
        /* battery full (charge done): charger present + SoC ~100% + low C1+C2 current */
        {
            float soc = batSOC();
            float v1 = monitor.get(CH_C1).voltage;
            float v2 = monitor.get(CH_C2).voltage;
            float i1 = monitor.get(CH_C1).current;
            float i2 = monitor.get(CH_C2).current;
            /* only when a charger (USB-C) is actually connected and the pack
             * is not sourcing a load through the same port */
            bool chargerConnected = (v1 > 3.7f) || (v2 > 3.7f);
            float ichg = (i1 < 0 ? -i1 : i1) + (i2 < 0 ? -i2 : i2);
            bool full = chargerConnected && (soc >= 99.5f) &&
                        (ichg <= 0.25f) && (batCurrent() < 0.05f);
            static bool fullCandidate = false;
            static uint32_t fullSince = 0;
            if (full) {
                if (!fullCandidate) { fullCandidate = true; fullSince = millis(); }
            } else {
                fullCandidate = false;
            }
            if (fullCandidate && (uint32_t)(millis() - fullSince) >= 2000 &&
                !m_chargeDoneNotified) {
                m_chargeDoneNotified = true;
                Serial.println("[charge] battery full -> charge-done alarm");
                startChargeDoneAlarm();
            } else if (!full) {
                m_chargeDoneNotified = false;
            }
        }
        /* low battery: short + 2 long beeps once; dim red flash every 5 min */
        {
            float soc = batSOC();
            bool charging = batCurrent() < -0.05f;   /* negative = into the pack */
            if (!m_eloadRunning) {
                static uint32_t clearSince = 0;
                bool clearCandidate = (soc >= BATTERY_LOW_CLEAR_PCT) && !charging;
                if (clearCandidate) {
                    if (!clearSince) clearSince = millis();
                    if ((uint32_t)(millis() - clearSince) >= 5000UL &&
                        m_lowBatteryLatched) {
                        m_lowBatteryLatched = false;
                        char v[8];
                        snprintf(v, sizeof(v), "%.0f%%", soc);
                        display.notify("BATTERY OK", v, 4500, 0x22C55E);
                    }
                } else {
                    clearSince = 0;
                }
                if (!charging && soc <= BATTERY_LOW_PCT && !m_lowBatteryLatched) {
                    m_lowBatteryLatched = true;
                    Serial.println("[batt] LOW BATTERY -> beep + red flash every 5 min");
                    char v[8];
                    snprintf(v, sizeof(v), "%.0f%%", soc);
                    display.notify("LOW BATTERY", v, 4500, 0xEF4444);
                    startLowBatteryAlarm();
                }
            }
            rgb.setLowBattery(m_lowBatteryLatched && !charging);
        }
        vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_MS));   /* 100 ms: finer mAh/Wh integration */
    }
}

void App::chargeDoneAlarmTask(void *p) {
    App *a = (App *)p;
    a->rgb.setChargeDone(CHARGE_DONE_RGB_MS);
    static const BuzzerNote chime[] = { {880, 140}, {659, 250}, {523, 420} };
    a->playMelody(chime, (int)(sizeof(chime) / sizeof(chime[0])));
    vTaskDelete(NULL);
}

void App::startChargeDoneAlarm() {
    display.notify("CHARGE DONE", "100%", 4500, 0x22C55E);
    xTaskCreate(chargeDoneAlarmTask, "chardone", 4096, this, 2, NULL);
}

void App::startLowBatteryAlarm() {
    static const BuzzerNote lowBat[] = {
        {2000, 80}, {0, 100}, {2000, 300}, {0, 150}, {2000, 300}
    };
    playMelody(lowBat, (int)(sizeof(lowBat) / sizeof(lowBat[0])));
}

void App::playMelodyTask(void *p) {
    BuzzerNote *pack = (BuzzerNote *)p;   /* pack[0].durMs = count; notes follow */
    int count = (int)pack[0].durMs;
    const BuzzerNote *notes = pack + 1;
    Buzzer &buz = App::instance()->buzzer;
    for (int i = 0; i < count; i++) {
        if (notes[i].freq == 0) {
            buz.off();
            vTaskDelay(pdMS_TO_TICKS(notes[i].durMs));   /* rest */
        } else {
            buz.setFrequency(notes[i].freq);
            buz.beep(notes[i].durMs);                    /* timer auto-off */
            vTaskDelay(pdMS_TO_TICKS(notes[i].durMs));
        }
    }
    buz.setFrequency(2000);   /* restore default tone so later beeps aren't left on the last note */
    buz.off();
    App *a = App::instance();
    a->m_melodyTask = nullptr;
    a->m_melodyPack = nullptr;
    delete[] pack;
    vTaskDelete(NULL);
}

void App::playMelody(const BuzzerNote *notes, int count) {
    /* stop any currently-playing melody first */
    if (m_melodyTask) {
        vTaskDelete((TaskHandle_t)m_melodyTask);
        m_melodyTask = nullptr;
        if (m_melodyPack) { delete[] m_melodyPack; m_melodyPack = nullptr; }
        buzzer.off();
    }
    /* pack count + a copy of the notes into one heap buffer for the task */
    BuzzerNote *pack = new BuzzerNote[(size_t)count + 1];
    pack[0].freq  = 0;
    pack[0].durMs = (uint16_t)count;
    for (int i = 0; i < count; i++) pack[i + 1] = notes[i];
    m_melodyPack = pack;
    xTaskCreate(playMelodyTask, "melody", 4096, pack, 2, (TaskHandle_t *)&m_melodyTask);
}

void App::batteryLoop() {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));   /* reserved for future calibration work */
    }
}

void App::uiLoop() {
    Serial.println("[ui] display init start");
    display.begin(*this);
    Serial.println("[ui] display init done");
    /* default to 100% brightness after boot; only restore a saved non-zero value */
    uint8_t saved = fram.data().displayBrightness;
    display.setBrightness(saved ? saved : DISP_BL_DEFAULT);
    for (;;) {
        display.update();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void App::buttonLoop() {
    for (;;) {
        buttons.update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void App::systemLoop() {
    for (;;) {
        buzzer.update();
        if (!m_bootRgbActive) rgb.update();   /* avoid double-driving the strip during boot fade */

        /* port session notices -> transient on-screen banner */
        {
            char p[4];
            float wh = 0;
            bool disconnected = false;
            if (energy.takePortNotice(p, sizeof(p), wh, disconnected)) {
                char title[20];
                char val[24];
                snprintf(title, sizeof(title), "%s %s", p,
                         disconnected ? "DELIVERED" : "CLEARED");
                snprintf(val, sizeof(val), "%.2f Wh", wh);
                display.notify(title, val, 4500,
                               disconnected ? 0x22C55E : 0xF59E0B);
            }
        }

        if (m_externalActive) {
            /* Charging or powering a USB load counts as active use. */
            if (m_activityIdle) markActivity();
            else m_lastActivityMs = millis();
        } else if (!m_activityIdle && m_activityTimeoutMs > 0 &&
                   (uint32_t)(millis() - m_lastActivityMs) >= m_activityTimeoutMs) {
            m_activityIdle = true;
            m_prevBrightness = display.brightness();
            display.setBrightness(0);
            rgb.setIdle(true);
            Serial.println("[ui] activity timeout -> display/RGB off");
        }

        /* Charging indication timeout: charging keeps m_externalActive true, so
         * without this the strip would stay lit forever while charging. After
         * RGB_CHARGE_INDICATION_MS force the strip idle (low-battery/warning
         * still show). Charger removal or user activity restarts the window. */
        if (m_charging) {
            if (!m_chargeRgbSince) m_chargeRgbSince = millis();
            if (!m_chargeRgbBlanked && !m_activityIdle &&
                (uint32_t)(millis() - m_chargeRgbSince) >= RGB_CHARGE_INDICATION_MS) {
                rgb.setIdle(true);
                m_chargeRgbBlanked = true;
                Serial.println("[rgb] charging indication timeout -> RGB idle");
            }
        } else {
            m_chargeRgbSince = 0;
            if (m_chargeRgbBlanked) {
                m_chargeRgbBlanked = false;
                if (!m_activityIdle) rgb.setIdle(false);   /* hand the strip back */
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void serviceLine(CliEngine &cli, Output &out, String &line) {
    line.trim();
    if (line.length() > 0) {
        App *app = App::instance();
        if (app) { app->markActivity(); app->rgb.flashActivity(false); }   /* rx: light yellow */
        cli.dispatch(line, out);
        if (app) app->rgb.flashActivity(true);    /* tx: light blue */
    }
    line = "";
}

void App::cliLoop() {
    SerialOutput serial;
    String sLine;
    String bLine;
    /* print the prompt once; echo a fresh prompt after each executed command */
    Serial.print(CLI_PROMPT);
    char last = 0;
    for (;;) {
        while (serial.available() > 0) {
            char c = (char)serial.read();
            if (c == '\r') {
                if (sLine.length() > 0) {
                    serviceLine(cli, serial, sLine);
                    Serial.print(CLI_PROMPT);
                }
                sLine = "";
            } else if (c == '\n') {
                /* absorb the LF that follows a CR so "\r\n" is one terminator */
                if (last != '\r' && sLine.length() > 0) {
                    serviceLine(cli, serial, sLine);
                    Serial.print(CLI_PROMPT);
                }
                sLine = "";
            } else {
                sLine += c;
                if (sLine.length() > CLI_MAX_LINE) sLine = "";
            }
            last = c;
        }
        char blast = 0;
        while (ble.available() > 0) {
            char c = (char)ble.read();
            if (c == '\r') {
                if (bLine.length() > 0) serviceLine(cli, ble.output(), bLine);
                bLine = "";
            } else if (c == '\n') {
                if (blast != '\r' && bLine.length() > 0) serviceLine(cli, ble.output(), bLine);
                bLine = "";
            } else {
                bLine += c;
                if (bLine.length() > CLI_MAX_LINE) bLine = "";
            }
            blast = c;
        }
        /* config-menu commands are queued here so they never run on the
         * button/UI tasks (avoids CLI reentrancy and LVGL blocking). */
        char qbuf[64];
        while (m_cmdQueue && xQueueReceive(m_cmdQueue, qbuf, 0) == pdTRUE) {
            String q(qbuf);
            serviceLine(cli, serial, q);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

















































