#include <Arduino.h>
#include "app/App.h"
#include "config/config.h"
#include "config/pins.h"
#include "infra/Logger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static App app;

static void taskMonitor(void *p) { App::instance()->monitorLoop(); }
static void taskUi(void *p)      { App::instance()->uiLoop(); }
static void taskButtons(void *p) { App::instance()->buttonLoop(); }
static void taskSystem(void *p)  { App::instance()->systemLoop(); }
static void taskCli(void *p)     { App::instance()->cliLoop(); }
static void taskBoot(void *p)    { App::instance()->bootSequence(); vTaskDelete(NULL); }

void setup() {
    /* ASAP: force the ELOAD gate low before anything else so the load FET
     * cannot be driven during startup (avoids FET damage). */
    pinMode(PIN_ELOAD_PWM, OUTPUT);
    digitalWrite(PIN_ELOAD_PWM, LOW);

    /* Backlight off immediately (esp. after deep-sleep wake) so the display
     * can't show white until the dashboard is actually drawn. */
    pinMode(PIN_DPL_PWM, OUTPUT);
    digitalWrite(PIN_DPL_PWM, LOW);

    Serial.begin(115200);
    delay(20);
    Serial.flush();
    Serial.println("[setup] serial ok, entering app.begin");

    app.begin();

    Serial.println("[setup] app.begin done, starting tasks");

    LOG_INFO("Starting RTOS tasks...");
    xTaskCreate(taskMonitor, "monitor", 8192,  NULL, TASK_MONITOR_PRIO, NULL);
    xTaskCreate(taskUi,      "ui",      16384, NULL, TASK_UI_PRIO,      NULL);
    xTaskCreate(taskButtons, "buttons", 4096,  NULL, TASK_BUTTON_PRIO,  NULL);
    xTaskCreate(taskSystem,  "system",  8192,  NULL, TASK_SYSTEM_PRIO,  NULL);
    xTaskCreate(taskCli,     "cli",     8192,  NULL, TASK_CLI_PRIO,     NULL);
    xTaskCreate(taskBoot,    "boot",    8192,  NULL, 2,                 NULL);
    Serial.println("[setup] all tasks created");
    LOG_INFO("All tasks created");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));   /* scheduler owns everything else */
}



