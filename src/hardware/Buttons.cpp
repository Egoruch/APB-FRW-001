#include "hardware/Buttons.h"

Buttons *Buttons::s_inst = nullptr;

void Buttons::emit(const char *event, uint8_t idx) {
    if (m_cb) m_cb(event, idx);
}

void Buttons::begin(ButtonEventFn cb) {
    s_inst = this;
    m_cb = cb;
    m_b[0].attachClick([] { if (Buttons::s_inst) Buttons::s_inst->emit("click", 0); });
    m_b[1].attachClick([] { if (Buttons::s_inst) Buttons::s_inst->emit("click", 1); });
    m_b[2].attachClick([] { if (Buttons::s_inst) Buttons::s_inst->emit("click", 2); });
    m_b[3].attachClick([] { if (Buttons::s_inst) Buttons::s_inst->emit("click", 3); });

    m_b[0].attachDoubleClick([] { if (Buttons::s_inst) Buttons::s_inst->emit("double", 0); });
    m_b[1].attachDoubleClick([] { if (Buttons::s_inst) Buttons::s_inst->emit("double", 1); });
    m_b[2].attachDoubleClick([] { if (Buttons::s_inst) Buttons::s_inst->emit("double", 2); });
    m_b[3].attachDoubleClick([] { if (Buttons::s_inst) Buttons::s_inst->emit("double", 3); });

    m_b[0].attachLongPressStart([] { if (Buttons::s_inst) Buttons::s_inst->emit("long", 0); });
    m_b[1].attachLongPressStart([] { if (Buttons::s_inst) Buttons::s_inst->emit("long", 1); });
    m_b[2].attachLongPressStart([] { if (Buttons::s_inst) Buttons::s_inst->emit("long", 2); });
    m_b[3].attachLongPressStart([] { if (Buttons::s_inst) Buttons::s_inst->emit("long", 3); });

    /* long-press release, so apps can implement "hold while pressed" */
    m_b[0].attachLongPressStop([] { if (Buttons::s_inst) Buttons::s_inst->emit("longstop", 0); });
    m_b[1].attachLongPressStop([] { if (Buttons::s_inst) Buttons::s_inst->emit("longstop", 1); });
    m_b[2].attachLongPressStop([] { if (Buttons::s_inst) Buttons::s_inst->emit("longstop", 2); });
    m_b[3].attachLongPressStop([] { if (Buttons::s_inst) Buttons::s_inst->emit("longstop", 3); });

    /* Fast single-click detection on every button (default is 400 ms).
     * BUT3 keeps a slightly longer window (200 ms) so its double-click
     * (ELOAD sweep) still has time to register. */
    m_b[0].setClickMs(150);
    m_b[1].setClickMs(150);
    m_b[2].setClickMs(200);
    m_b[3].setClickMs(150);
}

void Buttons::update() {
    for (int i = 0; i < 4; i++) m_b[i].tick();
}




