#pragma once
#include <stdint.h>
#include "measurements.h"

/* Persistent configuration & statistics stored in FRAM (versioned + CRC).
 * Layout: header {magic,version,length,flags} + crc + payload. */

#define PERSIST_MAGIC     0x41504230u   /* "APB0" */
#define PERSIST_VERSION   1u
#define PERSIST_LENGTH    sizeof(PersistData)

struct PersistData {
    float batteryCapacityMah;
    float batteryCalOffset;              /* SOC calibration offset (%) */
    float energyWh[CH_COUNT];
    float chargeMah[CH_COUNT];
    uint8_t displayBrightness;
    uint8_t rgbBrightness;
    uint8_t logEnabled;
    uint8_t logLevel;
    uint8_t debugEnabled;
    uint8_t rgbEffect;
    uint8_t lightPercent;
    uint8_t backlight;
    uint8_t heartbeat;             /* last-LED heartbeat indicator (0=off) */
    uint8_t timeSource;            /* 0 = external I2C RTC, 1 = software (FRAM-backed) */
    uint8_t activityTimeoutSec;    /* 0 = default 60 s, 255 = off, else seconds */
};

struct PersistHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t crc;
};

/* build a defaults-filled PersistData (used on first boot / mismatch) */
void persist_defaults(PersistData &d);
/* compute CRC32 over payload bytes */
uint32_t persist_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
constexpr size_t kPersistHeaderSize = sizeof(PersistHeader);
constexpr size_t kPersistTotalSize  = sizeof(PersistHeader) + sizeof(PersistData);
#endif
