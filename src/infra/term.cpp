#include "infra/term.h"
#include <string.h>

/* Raw byte ring (legacy snapshot path). */
static char s_buf[4096];
static volatile size_t s_head = 0;
static size_t s_used = 0;

/* Line ring: newest `s_count` complete lines, plus a pending partial line. */
#define TERM_LINES 60
#define TERM_LEN   72
static char s_lines[TERM_LINES][TERM_LEN];
static volatile int s_count = 0;   /* lines in the ring (0..TERM_LINES) */
static int s_headLine = 0;         /* ring index of the oldest stored line */
static char s_cur[TERM_LEN];
static int s_curLen = 0;
static volatile uint32_t s_gen = 0;   /* bumped on every completed line */

static void pushLine(void) {
    if (s_count < TERM_LINES) {
        int idx = (s_headLine + s_count) % TERM_LINES;
        memcpy(s_lines[idx], s_cur, TERM_LEN);
        s_count++;
    } else {
        memcpy(s_lines[s_headLine], s_cur, TERM_LEN);
        s_headLine = (s_headLine + 1) % TERM_LINES;
    }
    s_cur[0] = 0;
    s_curLen = 0;
    s_gen++;                        /* generation tick - even when ring is full */
}

void termClear(void) {
    s_head = 0; s_used = 0; s_count = 0; s_headLine = 0;
    s_cur[0] = 0; s_curLen = 0;
    s_gen++;
}

void termAppend(const char *s) {
    if (!s) return;
    for (const char *p = s; *p; p++) {
        char c = *p;
        /* raw byte ring (kept for the legacy snapshot API) */
        s_buf[s_head] = c;
        s_head = (s_head + 1) % sizeof(s_buf);
        if (s_used < sizeof(s_buf)) s_used++;

        if (c == '\n') pushLine();
        else if (s_curLen < TERM_LEN - 1) { s_cur[s_curLen++] = c; s_cur[s_curLen] = 0; }
    }
}

size_t termSnapshot(char *out, size_t max) {
    if (max == 0) return 0;
    size_t take = s_used < (max - 1) ? s_used : (max - 1);
    size_t start = (s_head + sizeof(s_buf) - s_used) % sizeof(s_buf);
    for (size_t i = 0; i < take; i++) out[i] = s_buf[(start + i) % sizeof(s_buf)];
    out[take] = 0;
    return take;
}

int termLineCount(void) {
    return s_count + (s_curLen > 0 ? 1 : 0);
}

uint32_t termGeneration(void) {
    return s_gen;
}

int termLineSnapshot(char *out, size_t outMax, int lines, int offset) {
    int cnt = termLineCount();
    if (cnt <= 0 || outMax == 0) { if (outMax > 0) out[0] = 0; return 0; }
    if (offset > cnt - 1) offset = cnt - 1;
    if (offset < 0) offset = 0;
    int take = lines; if (take > cnt - offset) take = cnt - offset;
    if (take < 1) take = 1;
    int start = cnt - take - offset;
    if (start < 0) start = 0;   /* guard: offset near the oldest data */
    size_t oi = 0;
    for (int li = start; li < start + take; li++) {
        const char *txt = (li < s_count) ? s_lines[(s_headLine + li) % TERM_LINES] : s_cur;
        if (li != start && oi + 1 < outMax) out[oi++] = '\n';
        for (const char *q = txt; *q && oi + 1 < outMax; q++) out[oi++] = *q;
    }
    out[oi] = 0;
    return (int)oi;
}
