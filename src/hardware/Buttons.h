#pragma once
#include <Arduino.h>
#include <OneButton.h>
#include "config/pins.h"

/* 4 user buttons (BUT1..BUT4) via OneButton, non-blocking.
 * Button events are forwarded to an optional callback so neither the UI nor
 * RTOS tasks are blocked. Active low with internal pull-ups enabled. */
typedef void (*ButtonEventFn)(const char *event, uint8_t index);  /* event: click/double/long */

class Buttons {
public:
    void begin(ButtonEventFn cb);

    void update();   /* call periodically to poll/tick all buttons */

    OneButton &b(uint8_t i) { return m_b[i]; }

private:
    OneButton m_b[4] = {
        OneButton(PIN_BUT1, true, true),
        OneButton(PIN_BUT2, true, true),
        OneButton(PIN_BUT3, true, true),
        OneButton(PIN_BUT4, true, true)
    };
    ButtonEventFn m_cb = nullptr;
    void emit(const char *event, uint8_t idx);
    static Buttons *s_inst;
};
