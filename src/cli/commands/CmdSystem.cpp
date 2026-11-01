#include "cli/commands.h"
#include "app/App.h"
#include "config/config.h"
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static void cmdVersion(Output &out, const std::vector<String> &a) {
    (void)a;
    out.println("APB");
    out.println("Hardware : " FW_HARDWARE);
    out.println("Firmware : " FW_VERSION);
    out.println("Build    : " FW_BUILD);
    out.println("MCU      : " FW_MCU);
    out.println("OK");
}

static void cmdId(Output &out, const std::vector<String> &a) {
    (void)a;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    out.println("APB device identity");
    out.println("Hardware : " FW_HARDWARE);
    out.printf("MAC      : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    out.printf("Chip ID  : %02X%02X%02X%02X%02X%02X\r\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    out.printf("MCU      : %s rev %u\r\n", ESP.getChipModel(), ESP.getChipRevision());
    out.println("OK");
}

static void cmdUptime(Output &out, const std::vector<String> &a) {
    (void)a;
    unsigned long s = millis() / 1000UL;
    out.printf("Uptime: %lu s (%lu:%02lu:%02lu)\r\n", s, s / 3600UL, (s / 60UL) % 60UL, s % 60UL);
    out.println("OK");
}

static void cmdStatus(Output &out, const std::vector<String> &a) {
    (void)a;
    out.println("APB");
    out.println("----------------");
    out.println("Version : " FW_VERSION);
    out.println("Uptime  : " + f2str(millis() / 1000.0f, 0) + " s");
    out.println("Battery : " + f2str(App::instance()->batVoltage()) + " V");
    out.println("SOC     : " + f2str(App::instance()->batSOC(), 0) + " %");
    out.println("Power   : " + f2str(App::instance()->batPower()) + " W");
    out.println("RTC     : " + App::instance()->timeString());
    out.println("MCU temp: " + f2str(temperatureRead(), 1) + " C");
    out.printf("Power rail (3V3+5V): %s\r\n", App::instance()->power.get3V3() ? "ON" : "OFF");
    out.printf("BLE     : %s\r\n", App::instance()->ble.connected() ? "connected" : "advertising");
    out.printf("I2C mux : %s\r\n", App::instance()->mux.isPresent() ? "present" : "missing");
    out.printf("FRAM    : %s\r\n", App::instance()->fram.isPresent() ? "present" : "missing");
    out.printf("Wake    : %s (pin %s)\r\n",
               App::instance()->sleep.wakeWasReason() ? "yes" : "no",
               App::instance()->wakeActive() ? "HIGH" : "LOW");
    out.println("OK");
}

static void cmdMcu(Output &out, const std::vector<String> &a) {
    String sub = (a.size() > 1) ? a[1] : "";
    sub.toLowerCase();
    if (sub == "info") {
        out.printf("Chip    : %s rev %u\r\n", ESP.getChipModel(), ESP.getChipRevision());
        out.printf("Flash   : %u bytes\r\n", ESP.getFlashChipSize());
        out.printf("Free    : %u bytes heap\r\n", ESP.getFreeHeap());
        out.printf("MinFree : %u bytes heap\r\n", ESP.getMinFreeHeap());
        out.printf("Freq    : %u MHz\r\n", getCpuFrequencyMhz());
        out.printf("Reset   : %d\r\n", (int)esp_reset_reason());
        out.println("OK");
    } else if (sub == "uptime") {
        cmdUptime(out, a);
    } else if (sub == "heap") {
        out.printf("Free heap: %u\r\n", ESP.getFreeHeap());
        out.printf("Min heap : %u\r\n", ESP.getMinFreeHeap());
        out.println("OK");
    } else if (sub == "reset-reason") {
        out.printf("Reset reason: %d\r\n", (int)esp_reset_reason());
        out.println("OK");
    } else if (sub == "freq") {
        out.printf("CPU freq: %u MHz\r\n", getCpuFrequencyMhz());
        out.println("OK");
    } else if (sub == "temperature") {
        out.printf("MCU temperature: %.2f C\r\n", temperatureRead());
        out.println("OK");
    } else if (sub == "reboot") {
        out.println("OK rebooting...");
        out.flush();
        App::instance()->time.saveNow();   /* keep the software clock as fresh as possible */
        ESP.restart();
    } else if (sub == "light-sleep") {
        App::instance()->energy.flush();
        App::instance()->sleep.enterLightSleep(0);
        out.println("OK woke from light sleep");
    } else if (sub == "deep-sleep") {
        out.println("OK entering deep sleep");
        out.flush();
        App::instance()->enterDeepSleep();
    } else {
        out.println("usage: mcu <info|uptime|heap|reset-reason|freq|temperature|reboot|light-sleep|deep-sleep>");
    }
}

static void cmdRtos(Output &out, const std::vector<String> &a) {
    String sub = (a.size() > 1) ? a[1] : "tasks";
    sub.toLowerCase();
    if (sub == "heap") {
        out.printf("Free heap: %u  min: %u\r\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
        out.println("OK");
        return;
    }
    if (sub == "tasks" || sub == "stack") {
        static TaskStatus_t arr[24];
        UBaseType_t cnt = uxTaskGetSystemState(arr, 24, NULL);
        out.println(" name            prio state  hwmB");
        for (UBaseType_t i = 0; i < cnt && i < 24; i++) {
            out.printf(" %-16s %4lu %5lu %6lu\r\n",
                       arr[i].pcTaskName, (unsigned long)arr[i].uxCurrentPriority,
                       (unsigned long)arr[i].eCurrentState,
                       (unsigned long)arr[i].usStackHighWaterMark);
        }
        out.println("OK");
        return;
    }
    if (sub == "queues") {
        out.println("Queue/command stats not collected in this build");
        out.println("OK");
        return;
    }
    out.println("usage: rtos <tasks|heap|stack|queues>");
}

void cmdRegisterSystem(CliEngine &cli, App &app) {
    (void)app;
    cli.registerCommand("version", "Firmware/hardware identification", "version", cmdVersion);
    cli.registerCommand("id", "Unique device identity (MAC/chip ID)", "id", cmdId);
    cli.registerCommand("uptime", "System uptime", "uptime", cmdUptime);
    cli.registerCommand("runtime", "System uptime (alias)", "runtime", cmdUptime);
    cli.registerCommand("mcu", "System/MCU commands", "mcu <info|uptime|heap|reset-reason|freq|temperature|reboot|light-sleep|deep-sleep>", cmdMcu);
    cli.registerCommand("rtos", "RTOS diagnostics (tasks/heap/stack/queues)", "rtos <tasks|heap|stack|queues>", cmdRtos);
    cli.registerCommand("status", "Concise system overview", "status", cmdStatus);
    cli.registerCommand("reboot", "Reboot the unit", "reboot", [](Output &out, const std::vector<String> &a) {
        (void)a; out.println("Rebooting..."); out.flush();
        if (App::instance()) App::instance()->time.saveNow();
        delay(100); ESP.restart();
    });
}


