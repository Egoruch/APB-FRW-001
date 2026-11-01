#pragma once

/* Display configuration (ST7789 240x320 + LVGL). */

#define DISP_HOR_RES 240
#define DISP_VER_RES 320
#define DISP_ROTATION 0

/* SPI clock for the TFT pixel bus (esp_lcd). 80 MHz can corrupt long DMA bursts
 * on this panel -> 40 MHz is reliable. */
#define DISP_SPI_FREQ_HZ 56000000

/* Panel options to match the module once flashed (flip to fix colors/orientation) */
#define DISP_INVERT_COLOR 0   /* 1 = invert (INVON), 0 = not inverted (INVOF) */
#define DISP_MIRROR_X     1   /* mirror around vertical axis (fix 180 rotation) */
#define DISP_MIRROR_Y     1   /* mirror around horizontal axis (fix 180 rotation) */
#define DISP_USE_BGR      0   /* 1 = BGR element order, 0 = RGB (panel is RGB) */

/* Backlight (GPIO 21) even is driven as LEDC PWM for real 0-100 dimming */
#define DISP_BL_DEFAULT 100
#define DISP_BL_PWM_MAX 255
#define DISP_BL_LEDC_CH 3

/* LVGL draw buffer: small partial (DMA-safe), single buffer */
#define DISP_DRAW_BUF_SIZE ((DISP_HOR_RES * DISP_VER_RES) / 5 * (LV_COLOR_DEPTH / 8))
