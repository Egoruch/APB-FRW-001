#pragma once
#include <Arduino.h>
#include "cli/CliEngine.h"
#include "infra/term.h"

/* Output targeting the hardware UART (Serial). Also feeds the Screen3 terminal. */
class SerialOutput : public Output {
public:
    virtual void print(const String &s)   { Serial.print(s); termAppend(s.c_str()); }
    virtual void println(const String &s) { Serial.println(s); termAppend(s.c_str()); termAppend("\n"); }
    virtual void println()                { Serial.println(); termAppend("\n"); }
    virtual void flush()                  { Serial.flush(); }
    virtual int  available()              { return Serial.available(); }
    virtual int  read()                   { return Serial.read(); }
};
