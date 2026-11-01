#include "infra/Logger.h"
#include "config/config.h"
#include <stdarg.h>
#include <stdio.h>

Logger &Logger::instance() {
    static Logger inst;
    return inst;
}

static const char *levelTag(ApbLogLevel lv) {
    switch (lv) {
        case APB_LEVEL_ERROR: return "E";
        case APB_LEVEL_WARN:  return "W";
        case APB_LEVEL_INFO:  return "I";
        case APB_LEVEL_DEBUG: return "D";
    }
    return "?";
}

void Logger::begin() {
    m_buf[0] = 0;
    setLevel(APB_LEVEL_INFO);
    setEnabled(true);
}

void Logger::log(ApbLogLevel lv, const char *fmt, ...) {
    if (!m_enabled || lv > m_level) return;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m_buf, sizeof(m_buf), fmt, ap);
    va_end(ap);

    unsigned long ms = millis();
    char tag[16];
    snprintf(tag, sizeof(tag), "[%lu.%03lu][%s]", ms / 1000UL, ms % 1000UL, levelTag(lv));

    Serial.print(tag);
    Serial.print(" ");
    Serial.println(m_buf);
    if (m_ble) m_ble(m_buf);
}


