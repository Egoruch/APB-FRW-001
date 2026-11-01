#pragma once
#include <stddef.h>
#include <stdint.h>

/* Tiny shared terminal ring-buffer used by Screen3's console view.
 * Feed it any text produced by the hardware serial or BLE CLI outputs. */
void termAppend(const char *s);
/* Copy the most recent text (up to max-1 bytes) as a NUL-terminated string. */
size_t termSnapshot(char *out, size_t max);
/* Clear the terminal buffer. */
void termClear(void);
/* Number of complete lines currently buffered (incl. a pending partial line). */
int termLineCount(void);
/* Monotonic counter bumped each time a complete line is pushed. Use this
 * instead of termLineCount() to detect "new data arrived" - it keeps counting
 * even after the line ring is full (s_count saturates there). */
uint32_t termGeneration(void);
/* Render `lines` newest lines (offset back from the newest) into out. */
int termLineSnapshot(char *out, size_t outMax, int lines, int offset);
