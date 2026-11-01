#pragma once
#include <Arduino.h>
#include <stdint.h>

enum ApbLogLevel { APB_LEVEL_ERROR = 0, APB_LEVEL_WARN, APB_LEVEL_INFO, APB_LEVEL_DEBUG };

class Logger {
public:
    static Logger &instance();

    void begin();
    void setLevel(ApbLogLevel lv)  { m_level = lv; }
    ApbLogLevel level() const      { return m_level; }
    void setEnabled(bool on)    { m_enabled = on; }
    bool enabled() const        { return m_enabled; }

    /* route logs to a BLE UART sink as well (off by default) */
    void setBleSink(void (*fn)(const char *)) { m_ble = fn; }

    void log(ApbLogLevel lv, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

private:
    Logger() {}
    ApbLogLevel m_level = APB_LEVEL_INFO;
    bool m_enabled = true;
    void (*m_ble)(const char *) = nullptr;
    char m_buf[192];
};

#define LOG_ERROR(...) Logger::instance().log(APB_LEVEL_ERROR, __VA_ARGS__)
#define LOG_WARN(...)  Logger::instance().log(APB_LEVEL_WARN,  __VA_ARGS__)
#define LOG_INFO(...)  Logger::instance().log(APB_LEVEL_INFO,  __VA_ARGS__)
#define LOG_DEBUG(...) Logger::instance().log(APB_LEVEL_DEBUG, __VA_ARGS__)

