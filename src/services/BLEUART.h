#pragma once
#include <Arduino.h>
#include "cli/CliEngine.h"
#include "infra/term.h"
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

/* A minimal Output that forwards text to the BLE notify characteristic.
 * The send hook is installed by BLEUART once the server is up. */
class BleOutput : public Output {
public:
    BleOutput() : m_sendHook(nullptr) {}
    void setHook(void (*hook)(const char *)) { m_sendHook = hook; }

    virtual void print(const String &s)  { if (m_sendHook && s.length()>0) { m_sendHook(s.c_str()); termAppend(s.c_str()); } }
    virtual void println(const String &s){ if (m_sendHook) { m_sendHook(s.c_str()); m_sendHook("\r\n"); } termAppend(s.c_str()); termAppend("\n"); }
    virtual void println()               { if (m_sendHook) m_sendHook("\r\n"); termAppend("\n"); }
    virtual void flush()                 {}
    virtual int  available()             { return 0; }
    virtual int  read()                  { return -1; }

private:
    void (*m_sendHook)(const char *);
};

/* BLE UART (Nordic UART Service) bridging the same CLI engine over BLE. */
class BLEUART {
public:
    void begin(const char *name);
    bool connected() const { return m_conn; }

    /* RX FIFO of received bytes, drained by the CLI task */
    size_t available();
    int read();

    BleOutput &output() { return m_out; }
    static BLEUART *instance() { return s_inst; }

    /* called by BLE callback classes */
    void onRxData(const uint8_t *data, size_t len);
    void onConnected(bool c);
    static void sendHook(const char *s);

private:
    BleOutput m_out;
    StreamBufferHandle_t m_fifo = nullptr;   /* SPSC ring: BLE host -> CLI task */
    volatile bool m_conn = false;
    static BLEUART *s_inst;
};
