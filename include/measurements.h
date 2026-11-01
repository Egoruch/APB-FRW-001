#pragma once
#include <stdint.h>
#include <stddef.h>

/* Common measurement structures (requirement 34).
 * A single PowerMeasurement is reused for every monitored rail. */

struct PowerMeasurement {
    float voltage;      /* V   */
    float current;      /* A   */
    float power;        /* W   */
    float charge_mAh;   /* accumulated mAh */
    float energy_Wh;    /* accumulated Wh  */
    uint32_t timestamp; /* ms since boot when sampled */
};

/* Power rail identifiers */
enum PowerChannel {
    CH_BAT = 0,   /* battery (derived) */
    CH_SYS,
    CH_IP1,
    CH_IP2,
    CH_C1,
    CH_C2,
    CH_A1,
    CH_A2,
    CH_ELOAD,
    CH_COUNT
};

static const char *const kChannelName[CH_COUNT] = {
    "BAT", "SYS", "IP1", "IP2", "C1", "C2", "A1", "A2", "ELOAD"
};
