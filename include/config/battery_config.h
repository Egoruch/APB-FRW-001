#pragma once

/* Battery model / calibration (configurable, persisted to FRAM when runtime-
 * adjustable). Values here are the defaults used at first boot. */

#define BATTERY_FULL_VOLTAGE      4.20f
#define BATTERY_EMPTY_VOLTAGE     3.00f
#define BATTERY_NOMINAL_VOLTAGE   3.70f
#define BATTERY_CAPACITY_MAH      4800.0f   /* nominal 3s? no: pack capacity mAh */
#define BATTERY_PACKS_IN_PARALLEL 1
#define BATTERY_ENERGY_FULL_WH    (BATTERY_CAPACITY_MAH / 1000.0f * BATTERY_NOMINAL_VOLTAGE)

/* Battery over-voltage protection: latch the IP5356 chargers off at/above
 * this cell voltage; require an unplug before clearing the latch. */
#define BATTERY_OVP_CUTOFF_V       4.20f
#define BATTERY_OVP_DEBOUNCE_MS    1000UL
#define BATTERY_OVP_CLEAR_MS       5000UL

/* Measurement interval for battery integration (ms) */
#define BATTERY_SAMPLE_MS 100

/* Low-battery warning: one-shot beep + a dim red RGB flash every 5 minutes
 * while the battery stays low. Latch clears above the clear threshold. */
#define BATTERY_LOW_PCT          15.0f
#define BATTERY_LOW_CLEAR_PCT    20.0f
#define BATTERY_LOW_RGB_PERIOD_MS (5UL * 60UL * 1000UL)
#define BATTERY_LOW_RGB_FLASH_MS  2000UL
#define BATTERY_LOW_RGB_LEVEL     30

/* How often accumulated counters are flushed to FRAM (ms) - wear conscious */
#define BATTERY_FRAM_FLUSH_MS 15000

/* Internal resistance safety checks */
#define RESISTANCE_MIN_CELL_VOLTAGE 2.50f    /* abort if V0 < this */
#define RESISTANCE_MAX_LOAD_A       40.0f
#define RESISTANCE_STEP_MS          120
#define ELOAD_TEST_MAX_TEMP_C       120.0f   /* abort ELOAD test above this */

#ifdef __cplusplus
#define ARRAYSZ(a) (sizeof(a) / sizeof((a)[0]))
#endif
