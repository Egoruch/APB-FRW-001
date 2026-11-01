#pragma once

/* Centralized GPIO definitions.
 * Source of truth: verified hardware tests (APB-PCB-001-R01).
 * Board: ESP32-C6 (esp32-c6-devkitc-1, Arduino core 3.x). */

/* I2C bus */
#define PIN_I2C1_SDA 6
#define PIN_I2C1_SCL 7

/* Power switching */
#define PIN_3V3_SW_EN 5            // HIGH = 3V3 bus enabled
#define PIN_ELOAD_PWM 13           // electronic load

/* Outputs / PWM */
#define PIN_BUZZER     12
#define PIN_LIGHT_PWM  15
#define PIN_LED_ADDR   14          // addressable RGBW strip

/* Display (ST7789, 240x320) and backlight */
#define PIN_DPL_PWM  21
#define PIN_TFT_CS   22
#define PIN_TFT_RST  23
#define PIN_TFT_DC   18
#define PIN_TFT_MOSI 20
#define PIN_TFT_SCLK 19

/* Buttons (active low, external pull-up assumed + OneButton pullup) */
#define PIN_BUT1 2
#define PIN_BUT2 3
#define PIN_BUT3 1
#define PIN_BUT4 4

/* Wake / inputs */
#define PIN_MCU_WAKE 0
#define PIN_MCU_WAKE2 8   /* also known as PIR_OUT */
