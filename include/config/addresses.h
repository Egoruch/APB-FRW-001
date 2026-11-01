#pragma once

/* Centralized I2C address map (APB-PCB-001-R01). */

#define ADDR_RTC_BM8563      0x51
#define ADDR_I2C_MUX         0x70   /* PCA9546A / TCA9546A */
#define ADDR_IP5356          0x75   /* data/ADC/status bank (0xEA >> 1) */
#define ADDR_IP5356_CTL      0x74   /* control bank (0xE8 >> 1) */

/* INA226 current/voltage monitors (all on main bus) */
#define INA226_SYS_ADDR   0x40
#define INA226_IP1_ADDR   0x41
#define INA226_IP2_ADDR   0x42
#define INA226_C1_ADDR    0x43
#define INA226_C2_ADDR    0x44
#define INA226_A1_ADDR    0x45
#define INA226_A2_ADDR    0x46
#define INA226_ELOAD_ADDR 0x47

/* TMP102 temperature sensors */
#define TMP102_PB1_ADDR   0x48
#define TMP102_PB2_ADDR   0x49
#define TMP102_ELOAD_ADDR 0x4A
#define TMP102_AIR_ADDR   0x4B

/* FRAM MB85RC16 (2 KB, fold-back aliases 0x50..0x57) */
#define FRAM_ADDR_BASE    0x50
#define FRAM_ADDR_RTC_BLOCK 0x51  /* collides with RTC - do not store here */
#define FRAM_ADDR_LIGHT_SENSOR_BLOCK 0x53 /* reserved/collision */
/* FRAM memory range that is addressed as device 0x51 (the RTC). Any access
 * here also reaches the BM8563; writes destroy the clock. Protect it. */
#define FRAM_RTC_PAGE_START 0x0100u
#define FRAM_RTC_PAGE_END   0x01FFu

/* I2C mux channels */
#define MUX_CH_IP1 0
#define MUX_CH_IP2 1
#define MUX_CH_IP_WAKE 3
