#pragma once

/* General firmware / timing configuration. */

/* Core INFO */
#define FW_NAME     "APB"
#define FW_HARDWARE "APB-PCB-001-R01"
#define FW_VERSION  "1.0.0"
#define FW_BUILD    __DATE__ " " __TIME__
#define FW_MCU      "ESP32-C6"

/* CLI */
#define CLI_PROMPT    "apb> "
#define CLI_TIMEOUT_MS 1500   /* line input idle to infer end of line (BLE) */
#define CLI_MAX_LINE   128

/* Task configuration */
#define TASK_CLI_PRIO      3
#define TASK_SENSOR_PRIO   7
#define TASK_MONITOR_PRIO  6
#define TASK_BATTERY_PRIO  7
#define TASK_UI_PRIO       5
#define TASK_BLE_PRIO      4
#define TASK_BUTTON_PRIO   6
#define TASK_SYSTEM_PRIO   1

/* I2C */
#define I2C_BUS_SPEED 300000

/* LEDC channel assignment */
#define LEDC_CH_ELOAD  0
#define LEDC_CH_BUZZER 1
#define LEDC_CH_LIGHT  2

/* Charge-complete RGB overlay */
#define CHARGE_DONE_RGB_MS      30000UL
#define CHARGE_DONE_GREEN_LEVEL 51      /* 20% of full green */

/* Efficiency is not meaningful below this port power (measurement noise). */
#define EFF_MIN_LOAD_W          0.10f

/* Display/RGB inactivity blanking */
#define ACTIVITY_TIMEOUT_MS     60000UL
