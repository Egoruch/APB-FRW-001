#include "hardware/Display.h"
#include "config/pins.h"
#include "config/config.h"
#include "config/display_config.h"
#include "config/battery_config.h"
#include "app/App.h"
#include "ui/apb_ui.h"
#include "ui/screens/home_gen.h"
#include "infra/SerialOutput.h"
#include "infra/term.h"
#include <RTClib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>

extern const lv_font_t font_osc_data;

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

extern const lv_font_t font_osc_data;
extern const lv_font_t font_tiny_data;

/* ST7789 via ESP-IDF esp_lcd + SPI2 (FSPI), DMA double-buffered flush */
#define LCD_CMD_CASET 0x2A
#define LCD_CMD_RASET 0x2B
#define LCD_CMD_RAMWR 0x2C

static esp_lcd_panel_io_handle_t g_io    = nullptr;
static esp_lcd_panel_handle_t    g_panel = nullptr;
static lv_display_t *g_disp = nullptr;
static uint32_t draw_buf0[DISP_DRAW_BUF_SIZE / 4];
static uint32_t draw_buf1[DISP_DRAW_BUF_SIZE / 4];

/* FPS / SPI self-meter (serial debug) */
static volatile uint32_t gFlushCnt = 0;
static volatile uint64_t gFlushUs = 0;
static uint32_t gLastStats = 0;

/* Called on SPI DMA colour-transfer completion -> tell LVGL this area is done.
 * This lets LVGL render the next buffer while DMA is still sending. */
static bool flush_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx) {
    (void)io; (void)e;
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
}

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    uint32_t t0 = micros();
    /* Panel wants big-endian RGB565; LVGL gives little-endian. Swap in place. */
    uint16_t *pix = (uint16_t *)px_map;
    for (size_t i = 0; i < (size_t)w * h; i++) pix[i] = __builtin_bswap16(pix[i]);
    esp_err_t rc = esp_lcd_panel_draw_bitmap(g_panel, area->x1, area->y1,
                                             area->x2 + 1, area->y2 + 1, px_map);
    if (rc != ESP_OK) lv_display_flush_ready(disp);   /* queue error: complete now */
    gFlushCnt++;
    gFlushUs += (uint64_t)(micros() - t0);
}

static void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
}

static uint32_t my_tick(void) { return millis(); }

void Display::begin(App &app) {
    m_app = &app;

    /* backlight (GPIO21) as LEDC PWM, held OFF until the first frame renders */
    ledcAttachChannel(PIN_DPL_PWM, 5000, 8, DISP_BL_LEDC_CH);
    ledcWrite(PIN_DPL_PWM, 0);
    m_blPending = true;

    Serial.println("[disp] spi begin");
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num   = PIN_TFT_SCLK;
    buscfg.mosi_io_num   = PIN_TFT_MOSI;
    buscfg.miso_io_num   = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = DISP_DRAW_BUF_SIZE + 64;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t iocfg = {};
    iocfg.dc_gpio_num   = PIN_TFT_DC;
    iocfg.cs_gpio_num   = PIN_TFT_CS;
    iocfg.pclk_hz       = DISP_SPI_FREQ_HZ;
    iocfg.lcd_cmd_bits  = 8;
    iocfg.lcd_param_bits = 8;
    iocfg.spi_mode      = 0;
    iocfg.trans_queue_depth = 10;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &iocfg, &g_io));

    esp_lcd_panel_dev_config_t pcfg = {};
    pcfg.reset_gpio_num = PIN_TFT_RST;
    pcfg.color_space    = DISP_USE_BGR ? ESP_LCD_COLOR_SPACE_BGR : ESP_LCD_COLOR_SPACE_RGB;
    pcfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(g_io, &pcfg, &g_panel));

    esp_lcd_panel_reset(g_panel);
    esp_lcd_panel_init(g_panel);
    esp_lcd_panel_invert_color(g_panel, DISP_INVERT_COLOR);
    if (DISP_MIRROR_X || DISP_MIRROR_Y)
        esp_lcd_panel_mirror(g_panel, DISP_MIRROR_X, DISP_MIRROR_Y);
    esp_lcd_panel_disp_on_off(g_panel, true);
    Serial.println("[disp] esp_lcd ready, lv_init");

    lv_init();
    lv_tick_set_cb(my_tick);
    g_disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
    lv_display_set_flush_cb(g_disp, disp_flush_cb);
    lv_display_set_buffers(g_disp, draw_buf0, draw_buf1, sizeof(draw_buf0), LV_DISPLAY_RENDER_MODE_PARTIAL);

    esp_lcd_panel_io_callbacks_t ioCbs = {};
    ioCbs.on_color_trans_done = flush_done_cb;
    esp_lcd_panel_io_register_event_callbacks(g_io, &ioCbs, g_disp);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);

    /* the exact working UI from the example */
    apb_ui_init(NULL);
    lv_screen_load_anim(home_create(), LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    Serial.println("[disp] ui built");
    m_screen1 = lv_screen_active();  /* remember the main dashboard screen */
    buildScreen2();

    buildBluetoothIcon();
    buildScreen3();
    buildScreen4();
    lv_timer_create(&Display::timerCb, 200, this);   /* 5 Hz screen data refresh */

    m_ready = true;
    Serial.println("[disp] ready");
}

void Display::setBrightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    m_bright = pct;
    if (!m_blPending)
        ledcWrite(PIN_DPL_PWM, (uint32_t)(pct * 255UL / 100UL));
}

void Display::update() {
    if (!m_ready) return;
    lv_timer_handler();
    if (m_switchReq) {
        m_switchReq = false;
        lv_screen_load(m_onScreen3 ? m_screen3 : (m_onScreen2 ? m_screen2 : (m_onScreen4 ? m_screen4 : m_screen1)));
        if (m_onScreen2) updateScreen2();
        if (m_onScreen3) updateScreen3();
        if (m_onScreen4) updateScreen4();
    }
    if (m_onScreen3) updateScreen3();   /* keep the config terminal live */
    if (m_onScreen4) updateScreen4();   /* keep IP5356 readouts live */
    if (m_blPending) {
        /* first frame drawn -> now light the backlight at the requested level */
        m_blPending = false;
        ledcWrite(PIN_DPL_PWM, (uint32_t)(m_bright * 255UL / 100UL));
    }
    static uint32_t lastStats = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastStats >= 2000) {
        uint32_t flushCnt = gFlushCnt;
        uint64_t flushUs = gFlushUs;
        gFlushCnt = 0;
        gFlushUs = 0;
        lastStats = nowMs;
        float avgMs = flushCnt ? (float)flushUs / 1000.0f / flushCnt : 0.0f;
        Serial.printf("[gfx] fl/s=%.0f avgF=%.2fms heap=%u\r\n",
                      flushCnt / 2.0f, avgMs, (unsigned)ESP.getFreeHeap());
    }
}

void Display::requestScreenToggle() {
    /* cycle main(00) -> screen2(10) -> screen3(01) -> screen4(11) -> main */
    if (!m_onScreen2 && !m_onScreen3 && !m_onScreen4) { m_onScreen2 = true;  m_onScreen3 = false; m_onScreen4 = false; }
    else if (m_onScreen2)                              { m_onScreen2 = false; m_onScreen3 = true;  m_onScreen4 = false; }
    else if (m_onScreen3)                              { m_onScreen2 = false; m_onScreen3 = false; m_onScreen4 = true;  }
    else                                               { m_onScreen2 = false; m_onScreen3 = false; m_onScreen4 = false; }
    m_switchReq = true;
    m_cfgDirty = true;
    if (m_onScreen4) { m_s4ForceProbe = true; m_s4Present = false; m_s4Dirty = true; }
}

void Display::requestScreen(int n) {
    /* jump straight to a screen; anything else clears the overlay flags */
    m_onScreen2 = (n == 2);
    m_onScreen3 = (n == 3);
    m_onScreen4 = (n == 4);
    m_switchReq = true;
    m_cfgDirty = true;
    if (m_onScreen4) { m_s4ForceProbe = true; m_s4Present = false; m_s4Dirty = true; }
}

int Display::screen() const {
    if (m_onScreen4) return 4;
    if (m_onScreen3) return 3;
    if (m_onScreen2) return 2;
    return 1;
}

static void setTextIfChanged(lv_obj_t *l, const char *txt);

static lv_obj_t *s2label(lv_obj_t *par, const char *txt, int x, int y, const lv_font_t *f, lv_color_t col) {
    lv_obj_t *l = lv_label_create(par);
    setTextIfChanged(l, txt);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_bg_opa(l, 0, 0);
    return l;
}

static void setTextIfChanged(lv_obj_t *l, const char *txt) {
    const char *cur = lv_label_get_text(l);
    if (cur && strcmp(cur, txt) == 0) return;
    lv_label_set_text(l, txt);
}

static lv_obj_t *s2val(lv_obj_t *par, const char *txt, int x, int y, const lv_font_t *f, lv_color_t col) {
    lv_obj_t *l = s2label(par, txt, x, y, f, col);
    lv_obj_set_width(l, 224);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    return l;
}

static void setEff(lv_obj_t *l, float num, float den) {
    char b[16];
    float n = num < 0.0f ? -num : num;
    float d = den < 0.0f ? -den : den;
    bool active = (n >= EFF_MIN_LOAD_W && d > 0.05f);
    if (n < EFF_MIN_LOAD_W || d <= 0.05f) {
        active = false;
        snprintf(b, sizeof(b), "0%%");
    } else {
        float e = n / d;
        if (e > 1.0f) e = 1.0f / e;
        float pct = e * 100.0f;
        if (pct < 2.0f) {
            active = false;
            snprintf(b, sizeof(b), "0%%");   /* suppress idle noise */
        } else {
            snprintf(b, sizeof(b), "%.0f%%", pct);
        }
    }
    setTextIfChanged(l, b);
    lv_obj_set_style_text_color(l, active ? lv_color_white()
                                         : lv_color_hex(0x64748B), 0);
}

/* 2 decimals when >=1, 3 decimals below 1 (shows 0.000 in the low range) */
static void fmtVal(char *b, size_t n, float v) {
    float a = v < 0.0f ? -v : v;
    if (a >= 1.0f) snprintf(b, n, "%.2f", v);
    else           snprintf(b, n, "%.3f", v);
}

/* Efficiency denominator: prefer this port's rail power, else battery power. */
static float effDen(float rail, float bat) {
    float r = rail < 0.0f ? -rail : rail;
    return r > 0.05f ? rail : bat;
}

void Display::buildScreen2() {
    m_screen2 = lv_obj_create(NULL);
    lv_obj_set_size(m_screen2, 240, 320);
    lv_obj_set_style_bg_color(m_screen2, lv_color_hex(0x101020), 0);
    lv_obj_set_style_bg_opa(m_screen2, 255, 0);
    lv_obj_set_scrollbar_mode(m_screen2, LV_SCROLLBAR_MODE_OFF);

    /* top panel: battery SOC % (left) + Wh value (same size as battery %) + small Wh unit */
    m_s2soc   = s2label(m_screen2, "100%", 8, 2, font_val, lv_color_hex(0x00d26a));
    m_s2batWh = s2label(m_screen2, "0.000", 100, 2, font_val, lv_color_hex(0x00FFFF));
    lv_obj_set_width(m_s2batWh, 110);
    lv_obj_set_style_text_align(m_s2batWh, LV_TEXT_ALIGN_RIGHT, 0);
    s2label(m_screen2, "Wh", 214, 16, &lv_font_montserrat_14, lv_color_hex(0x00FFFF));

    /* block tags (same font as battery %) */
    s2label(m_screen2, "A1", 4, 48, font_val, lv_color_hex(0x44AAFF));
    s2label(m_screen2, "C2", 4, 186, font_val, lv_color_hex(0x44AAFF));

    /* 8 stacked value rows (A1 block then C2 block), right-aligned */
    m_s2a2V  = s2val(m_screen2, "--", 8, 42,  &font_osc_data, lv_color_hex(0xFFFF00));
    m_s2a2A  = s2val(m_screen2, "--", 8, 74,  &font_osc_data, lv_color_hex(0x00FF00));
    m_s2a2W  = s2val(m_screen2, "--", 8, 106, &font_osc_data, lv_color_hex(0xFFA500));
    m_s2a2Wh = s2val(m_screen2, "--", 8, 138, &font_osc_data, lv_color_hex(0x00FFFF));

    m_s2c1V  = s2val(m_screen2, "--", 8, 184, &font_osc_data, lv_color_hex(0xFFFF00));
    m_s2c1A  = s2val(m_screen2, "--", 8, 216, &font_osc_data, lv_color_hex(0x00FF00));
    m_s2c1W  = s2val(m_screen2, "--", 8, 248, &font_osc_data, lv_color_hex(0xFFA500));
    m_s2c1Wh = s2val(m_screen2, "--", 8, 280, &font_osc_data, lv_color_hex(0x00FFFF));
}

void Display::updateScreen2() {
    if (!m_app) return;
    App &a = *m_app;
    char b[24];

    snprintf(b, sizeof(b), "%.0f%%", a.batSOC());
    setTextIfChanged(m_s2soc, b);
    snprintf(b, sizeof(b), "%.3f", a.energyWh(CH_BAT));
    setTextIfChanged(m_s2batWh, b);

    const PowerMeasurement &a1 = a.monitor.get(CH_A1);
    const PowerMeasurement &c2 = a.monitor.get(CH_C2);
    snprintf(b, sizeof(b), "%.3f V",  a1.voltage); setTextIfChanged(m_s2a2V,  b);
    snprintf(b, sizeof(b), "%.3f A",  a1.current); setTextIfChanged(m_s2a2A,  b);
    snprintf(b, sizeof(b), "%.3f W",  a1.power);   setTextIfChanged(m_s2a2W,  b);
    snprintf(b, sizeof(b), "%.3f Wh", a.energyWh(CH_A1)); setTextIfChanged(m_s2a2Wh, b);
    snprintf(b, sizeof(b), "%.3f V",  c2.voltage); setTextIfChanged(m_s2c1V,  b);
    snprintf(b, sizeof(b), "%.3f A",  c2.current); setTextIfChanged(m_s2c1A,  b);
    snprintf(b, sizeof(b), "%.3f W",  c2.power);   setTextIfChanged(m_s2c1W,  b);
    snprintf(b, sizeof(b), "%.3f Wh", a.energyWh(CH_C2)); setTextIfChanged(m_s2c1Wh, b);
}

typedef struct { const char *name; const char *cmd; } CfgItem;
static const CfgItem kCfg[] = {
    {"clr","clr"}, {"display",NULL}, {"timeout","timeout"},
    {"energy","energy battery set"}, {"light","light",},
    {"test","test"}, {"temperature","temperature all"}, {"i2c","i2c scan"}, {"version","version"}, {"id","id"},
    {"uptime","uptime"}, {"monitor","monitor all"}, {"mcu","mcu"},
    {"battery","battery"}, {"ovp","battery ovp set"}, {"buzzer","buzzer"}, {"eload","eload status"},
    {"eload test","eload test"}, {"fram","fram"}, {"melody","melody"},
    {"power","power status"}, {"rgb","rgb"},
    {"screen","display screen"},
    {"rtc","rtc"}, {"sensor","sensor list"}, {"status","status"},
    {"rtos","rtos tasks"}, {"wakeup","wakeup"}, {"chargefull","chargefull"},
    {"cable","cable"},
    {"cable test","cable test 1.0"},
    {"rte","rte"},
    {"debug","debug status"}, {"log","log"}, {"reboot","reboot"},
};
static const int kCfgCount = (int)(sizeof(kCfg) / sizeof(kCfg[0]));
static bool isDisp(int i) { return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "display") == 0; }
static bool isTimeout(int i){ return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "timeout") == 0; }
static bool isMcu(int i)  { return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "mcu") == 0; }
static bool isMelody(int i){ return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "melody") == 0; }
static bool isBuzzer(int i){ return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "buzzer") == 0; }
static bool isEnergy(int i){ return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "energy") == 0; }
static bool isOvp(int i)   { return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "ovp") == 0; }
static bool isFram(int i) { return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "fram") == 0; }
static bool isRgb(int i)  { return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "rgb") == 0; }
static bool isLight(int i){ return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "light") == 0; }
static bool isScreen(int i){ return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "screen") == 0; }
static bool isRtc(int i)   { return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "rtc") == 0; }
static bool isLog(int i)   { return i >= 0 && i < kCfgCount && strcmp(kCfg[i].name, "log") == 0; }
static bool isSubList(int i) { return isMcu(i)||isMelody(i)||isBuzzer(i)||isFram(i)||isRgb(i)||isScreen(i)||isRtc(i)||isTimeout(i)||isLog(i); }

/* subcommand list for list-type items */
static const char *const *subList(int i, int *n) {
    static const char *const subMcu[6]    = {"info","heap","freq","temperature","light-sleep","deep-sleep"};
    static const char *const subMelody[3] = {"oginski","jingle","svoi"};
    static const char *const subBuzzer[5] = {"status","on","off","beep","test"};
    static const char *const subFram[5]   = {"info","read","write","erase","test"};
    static const char *const subRgb[5]    = {"status","on","off","blink","heartbeat"};
    static const char *const subLight[2]  = {"status","off"};
    static const char *const subScreen[4] = {"1","2","3","4"};
    static const char *const subRtc[4]    = {"get","source ext","source soft","raw"};
    static const char *const subTimeout[5] = {"off","30","60","120","180"};
    static const char *const subLog[7] = {"status","enable","disable","level error","level warn","level info","level debug"};
    if (isMcu(i))     { *n = 6; return subMcu; }
    if (isMelody(i))  { *n = 3; return subMelody; }
    if (isBuzzer(i))  { *n = 5; return subBuzzer; }
    if (isFram(i))    { *n = 5; return subFram; }
    if (isRgb(i))     { *n = 5; return subRgb; }
    if (isLight(i))   { *n = 2; return subLight; }
    if (isScreen(i))  { *n = 4; return subScreen; }
    if (isRtc(i))     { *n = 4; return subRtc; }
    if (isTimeout(i)) { *n = 5; return subTimeout; }
    if (isLog(i))     { *n = 7; return subLog; }
    *n = 0; return nullptr;
}

#define TERM_VIEW 14   /* terminal visible lines */

void Display::buildScreen3() {
    m_screen3 = lv_obj_create(NULL);
    lv_obj_set_size(m_screen3, 240, 320);
    lv_obj_set_style_bg_color(m_screen3, lv_color_hex(0x101020), 0);
    lv_obj_set_style_bg_opa(m_screen3, 255, 0);
    lv_obj_set_scrollbar_mode(m_screen3, LV_SCROLLBAR_MODE_OFF);

    /* top panel: same as main screen (SOC + clock + date) */
    m_s3SoC = lv_label_create(m_screen3);
    lv_label_set_text(m_s3SoC, "--%");
    lv_obj_set_pos(m_s3SoC, 8, 2);
    lv_obj_set_style_text_font(m_s3SoC, font_val, 0);
    lv_obj_set_style_text_color(m_s3SoC, lv_color_hex(0x00d26a), 0);
    m_s3Clock = lv_label_create(m_screen3);
    lv_label_set_text(m_s3Clock, "--:--:--");
    lv_obj_set_pos(m_s3Clock, 135, 3);
    lv_obj_set_width(m_s3Clock, 100);
    lv_obj_set_style_text_align(m_s3Clock, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(m_s3Clock, font_lbl, 0);
    lv_obj_set_style_text_color(m_s3Clock, lv_color_white(), 0);
    m_s3Date = lv_label_create(m_screen3);
    lv_label_set_text(m_s3Date, "--");
    lv_obj_set_pos(m_s3Date, 135, 18);
    lv_obj_set_width(m_s3Date, 100);
    lv_obj_set_style_text_align(m_s3Date, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(m_s3Date, font_lbl, 0);
    lv_obj_set_style_text_color(m_s3Date, lv_color_hex(0x94a3b8), 0);

    /* top half: scrollable menu (below the header) */
    lv_obj_t *mc = lv_obj_create(m_screen3);
    lv_obj_set_pos(mc, 0, 30);
    lv_obj_set_size(mc, 240, 137);
    lv_obj_set_style_bg_opa(mc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mc, 1, 0);
    lv_obj_set_style_border_side(mc, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(mc, lv_color_hex(0x334155), 0);
    lv_obj_set_scroll_dir(mc, LV_DIR_VER);
    lv_obj_add_flag(mc, LV_OBJ_FLAG_SCROLLABLE);

    static const char *sub[6]    = {"info","heap","freq","temperature","light-sleep","deep-sleep"};
    for (int i = 0; i < kCfgCount; i++) {
        lv_obj_t *l = lv_label_create(mc);
        lv_label_set_text(l, kCfg[i].name);
        lv_obj_set_pos(l, 4, 2 + i * 16);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_bg_opa(l, 0, 0);
        m_cfgItems[i] = l;
    }
    /* mcu sub-menu: fixed on the right, outside the scroll container (no crop) */
    for (int i = 0; i < 6; i++) {
        lv_obj_t *l = lv_label_create(m_screen3);
        lv_label_set_text(l, sub[i]);
        lv_obj_set_pos(l, 140, 36 + i * 16);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x94A3B8), 0);
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        m_cfgSubLabels[i] = l;
    }

    /* bottom half: terminal (black bg, poison green). Text sits in a
     * content-sized label inside a scrollable box; we scroll to the newest
     * line on every refresh so nothing is ever clipped by a fixed height. */
    lv_obj_t *termbox = lv_obj_create(m_screen3);
    lv_obj_set_pos(termbox, 0, 168);
    lv_obj_set_size(termbox, 240, 152);
    lv_obj_set_style_bg_color(termbox, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(termbox, 255, 0);
    lv_obj_set_style_border_width(termbox, 0, 0);
    lv_obj_set_style_radius(termbox, 0, 0);
    lv_obj_add_flag(termbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(termbox, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(termbox, LV_SCROLLBAR_MODE_OFF);
    m_termBox = termbox;
    m_termLabel = lv_label_create(termbox);
    lv_label_set_text(m_termLabel, "");
    lv_obj_set_pos(m_termLabel, 0, 0);
    lv_obj_set_width(m_termLabel, 236);
    lv_obj_set_height(m_termLabel, LV_SIZE_CONTENT);
    lv_label_set_long_mode(m_termLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(m_termLabel, &font_tiny_data, 0);
    lv_obj_set_style_text_color(m_termLabel, lv_color_hex(0x39FF14), 0);
}

void Display::updateScreen3() {
    if (!m_screen3) return;
    if (m_app) {
        char bb[32];
        snprintf(bb, sizeof(bb), "%.0f%%", m_app->batSOC());
        setTextIfChanged(m_s3SoC, bb);
        setTextIfChanged(m_s3Clock, m_app->timeCached());
        setTextIfChanged(m_s3Date, m_app->dateCached());
    }
    /* Rebuild the 35 config rows only when something changed (button/config)
     * or at 5 Hz for externally changed values (CLI). This keeps a button
     * press responsive without churning LVGL every 5 ms. */
    uint32_t nowMs = millis();
    static uint32_t lastRebuildMs = 0;
    if (m_cfgDirty || (nowMs - lastRebuildMs >= 200)) {
        m_cfgDirty = false;
        lastRebuildMs = nowMs;

        static uint32_t lastCol[64] = {0};
        char b[64];
        for (int i = 0; i < kCfgCount; i++) {
            if      (isDisp(i))   snprintf(b, sizeof(b), "%s display %u%%", (i == m_cfgSel ? ">" : " "), (unsigned)m_bright);
            else if (isTimeout(i)) {
                uint8_t ts = m_app ? m_app->activityTimeoutSec() : 0;
                if (ts == 255) snprintf(b, sizeof(b), "%s timeout off", (i == m_cfgSel ? ">" : " "));
                else if (ts == 0) snprintf(b, sizeof(b), "%s timeout 60s", (i == m_cfgSel ? ">" : " "));
                else snprintf(b, sizeof(b), "%s timeout %us", (i == m_cfgSel ? ">" : " "), ts);
            }
            else if (isLight(i))  snprintf(b, sizeof(b), "%s light %u%%", (i == m_cfgSel ? ">" : " "), (unsigned)(m_app ? m_app->light() : 0));
            else if (isEnergy(i)) snprintf(b, sizeof(b), "%s energy %.1f Wh", (i == m_cfgSel ? ">" : " "), m_cfgNum);
            else if (isOvp(i)) {
                float v = (m_cfgActive && i == m_cfgSel) ? m_cfgNum
                                                        : (m_app ? m_app->ovpCutoffVoltage() : 4.20f);
                snprintf(b, sizeof(b), "%s ovp %.2fV", (i == m_cfgSel ? ">" : " "), v);
            }
            else                  snprintf(b, sizeof(b), "%s %s", (i == m_cfgSel ? ">" : " "), kCfg[i].name);
            setTextIfChanged(m_cfgItems[i], b);
            uint32_t colHex;
            if (m_cfgMode == 1) colHex = 0x556070;
            else if (m_cfgActive && i == m_cfgSel) colHex = 0xFFA500;   /* selected + active */
            else if (i == m_cfgSel)                colHex = 0x00E676;
            else                                   colHex = 0x94A3B8;
            if (lastCol[i] != colHex) {
                lastCol[i] = colHex;
                lv_obj_set_style_text_color(m_cfgItems[i], lv_color_hex(colHex), 0);
            }
        }

        int subCnt = 0;
        const char *const *subs = (m_cfgMode == 0 && m_cfgActive && isSubList(m_cfgSel))
                           ? subList(m_cfgSel, &subCnt) : nullptr;
        static uint32_t lastSubCol[6] = {0};
        int start = 0;
        if (subs && subCnt > 6) {
            start = m_cfgSub - 2;
            if (start < 0) start = 0;
            if (start > subCnt - 6) start = subCnt - 6;
        }
        for (int i = 0; i < 6; i++) {
            int si = start + i;
            if (subs && si < subCnt) {
                setTextIfChanged(m_cfgSubLabels[i], subs[si]);
                lv_obj_remove_flag(m_cfgSubLabels[i], LV_OBJ_FLAG_HIDDEN);
                uint32_t ch = (si == m_cfgSub) ? 0xFFA500 : 0x94A3B8;
                if (lastSubCol[i] != ch) {
                    lastSubCol[i] = ch;
                    lv_obj_set_style_text_color(m_cfgSubLabels[i], lv_color_hex(ch), 0);
                }
            } else {
                lv_obj_add_flag(m_cfgSubLabels[i], LV_OBJ_FLAG_HIDDEN);
                lastSubCol[i] = 0;
            }
        }
    }

    if (m_cfgScrollToSel) {
        m_cfgScrollToSel = false;
        if (m_cfgSel >= 0 && m_cfgSel < kCfgCount && m_cfgItems[m_cfgSel])
            lv_obj_scroll_to_view(m_cfgItems[m_cfgSel], LV_ANIM_OFF);
    }

    static char win[TERM_VIEW * 76];
    static char lastWin[TERM_VIEW * 76];
    static uint32_t lastGen = 0;
    static int lastTermOff = -1;
    static uint32_t lastTermMs = 0;
    uint32_t g = termGeneration();
    bool genChanged = (g != lastGen);
    if (genChanged) {
        lastGen = g;
        m_cfgTermOff = 0;   /* new data -> back to the newest */
    }
    if (genChanged || m_cfgTermOff != lastTermOff || (nowMs - lastTermMs) >= 50) {
        lastTermOff = m_cfgTermOff;
        lastTermMs = nowMs;
        termLineSnapshot(win, sizeof(win), TERM_VIEW, m_cfgTermOff);
        if (strcmp(win, lastWin) != 0) {
            memcpy(lastWin, win, sizeof(lastWin));
            lv_label_set_text(m_termLabel, win);
            /* follow the newest line of the rendered window (auto-scroll on new
             * data; when the user scrolled up this pins the window's bottom) */
            if (m_termBox) lv_obj_scroll_to_y(m_termBox, LV_COORD_MAX, LV_ANIM_OFF);
        }
    }
}

void Display::configToggleMode() {
    m_cfgMode ^= 1;
    m_cfgDirty = true;
}

void Display::configTermScroll(int dir) {
    int d = (dir < 0) ? 1 : -1;   /* up -> read older (+), down -> towards newest (-) */
    m_cfgTermOff += d;
    if (m_cfgTermOff < 0) m_cfgTermOff = 0;
    /* re-render + pin bottom happens on the next updateScreen3 tick */
}

bool Display::adjustActive() const {
    return m_cfgActive && (isDisp(m_cfgSel) || isLight(m_cfgSel) || isEnergy(m_cfgSel) || isOvp(m_cfgSel));
}

void Display::startRepeat(int dir) {
    if (m_repeat) return;
    m_repeat = true;
    m_repeatDir = dir;
    xTaskCreate(repeatTaskFn, "cfgrep", 4096, this, 2, (TaskHandle_t *)&m_repeatTask);
}

void Display::stopRepeat() {
    m_repeat = false;
    m_repeatDir = 0;
}

void Display::repeatTaskFn(void *p) {
    Display *d = (Display *)p;
    int delayMs = 140;
    while (d->m_repeat) {
        if (d->m_repeatDir < 0) d->configLeft(); else d->configRight();
        vTaskDelay(pdMS_TO_TICKS(delayMs));
        if (delayMs > 30) delayMs -= 10;   /* accelerate while held */
    }
    d->m_repeatTask = nullptr;
    vTaskDelete(NULL);
}

void Display::configCancel() {
    if (!m_cfgActive) return;
    if (isDisp(m_cfgSel))        setBrightness(m_initBright);
    else if (isLight(m_cfgSel) && m_app) m_app->setLight(m_initLight);
    else if (isEnergy(m_cfgSel)) m_cfgNum = m_initNum;
    else if (isOvp(m_cfgSel))    m_cfgNum = m_initNum;
    m_cfgActive = false;
    m_repeat = false;
    m_repeatDir = 0;
    m_cfgDirty = true;
}

void Display::configLeft() {
    if (!m_onScreen3) return;
    m_cfgDirty = true;
    if (m_cfgActive && isDisp(m_cfgSel)) {
        uint8_t nb = m_bright == 0 ? 100 : (uint8_t)(m_bright - 10);
        setBrightness(nb);
    } else if (m_cfgActive && isLight(m_cfgSel)) {
        uint8_t l = m_app ? m_app->light() : 0;
        l = l == 0 ? 100 : (uint8_t)(l - 1);
        if (m_app) m_app->setLight(l);
    } else if (m_cfgActive && isSubList(m_cfgSel) && m_cfgSel < kCfgCount) {
        int n = 0; subList(m_cfgSel, &n); if (n) { m_cfgSub--; if (m_cfgSub < 0) m_cfgSub = n - 1; }
    } else if (m_cfgActive && isEnergy(m_cfgSel)) {
        float v = m_cfgNum - 0.5f; if (v < 0) v = 0; m_cfgNum = v;
    } else if (m_cfgActive && isOvp(m_cfgSel)) {
        float v = m_cfgNum - 0.01f;
        if (v < 3.00f) v = 3.00f;
        m_cfgNum = v;
    } else {
        m_cfgSel--; if (m_cfgSel < 0) m_cfgSel = kCfgCount - 1;
        m_cfgScrollToSel = true;
    }
}

void Display::configRight() {
    if (!m_onScreen3) return;
    m_cfgDirty = true;
    if (m_cfgActive && isDisp(m_cfgSel)) {
        uint8_t nb = m_bright == 100 ? 0 : (uint8_t)(m_bright + 10);
        setBrightness(nb);
    } else if (m_cfgActive && isLight(m_cfgSel)) {
        uint8_t l = m_app ? m_app->light() : 0;
        l = l == 100 ? 0 : (uint8_t)(l + 1);
        if (m_app) m_app->setLight(l);
    } else if (m_cfgActive && isSubList(m_cfgSel) && m_cfgSel < kCfgCount) {
        int n = 0; subList(m_cfgSel, &n); if (n) { m_cfgSub++; if (m_cfgSub >= n) m_cfgSub = 0; }
    } else if (m_cfgActive && isEnergy(m_cfgSel)) {
        float v = m_cfgNum + 0.5f; if (v > 2000) v = 2000; m_cfgNum = v;
    } else if (m_cfgActive && isOvp(m_cfgSel)) {
        float v = m_cfgNum + 0.01f;
        if (v > 4.20f) v = 4.20f;
        m_cfgNum = v;
    } else {
        m_cfgSel++; if (m_cfgSel >= kCfgCount) m_cfgSel = 0;
        m_cfgScrollToSel = true;
    }
}

void Display::configConfirm() {
    if (!m_onScreen3) return;
    m_cfgDirty = true;
    if (m_cfgActive) {
        if      (isSubList(m_cfgSel)) runSelectedSub();
        else if (isEnergy(m_cfgSel)) runEnergySet();
        else if (isOvp(m_cfgSel))    runOvpSet();
        else if (isDisp(m_cfgSel))   m_cfgActive = false;
        else if (isLight(m_cfgSel))  m_cfgActive = false;
    } else {
        if (isSubList(m_cfgSel) || isDisp(m_cfgSel) || isEnergy(m_cfgSel) ||
            isLight(m_cfgSel) || isOvp(m_cfgSel)) {
            m_cfgActive = true;
            if (isDisp(m_cfgSel))       m_initBright = m_bright;
            else if (isLight(m_cfgSel)) m_initLight = m_app ? m_app->light() : 0;
            else if (isEnergy(m_cfgSel)) m_initNum = m_cfgNum;
            else if (isOvp(m_cfgSel)) {
                m_cfgNum = m_app ? m_app->ovpCutoffVoltage() : 4.20f;
                m_initNum = m_cfgNum;
            }
            if (isSubList(m_cfgSel)) m_cfgSub = 0;   /* select first subcommand on entry */
        } else {
            runItem();
        }
    }
}

void Display::configExitSub() {
    if (m_cfgActive) {
        m_cfgActive = false;   /* BUT1 long: close any active mode/submenu */
        m_cfgDirty = true;
        return;
    }
    if (m_onScreen3) requestScreen(1);   /* already at top level: leave config */
}

const char *Display::itemCmd(int idx) {
    if (idx < 0 || idx >= kCfgCount) return NULL;
    return kCfg[idx].cmd;
}

void Display::runItem() {
    const char *cmd = itemCmd(m_cfgSel);
    if (!cmd || !m_app) return;
    if (!m_app->postCommand(cmd))
        Serial.printf("[ui] cmd queue full: %s\r\n", cmd);
}

void Display::runSelectedSub() {
    if (!m_app) return;
    int n = 0; const char *const *subs = subList(m_cfgSel, &n);
    if (!subs || n == 0) return;
    int s = m_cfgSub; if (s < 0) s = 0; if (s >= n) s = n - 1;
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "%s %s", kCfg[m_cfgSel].cmd ? kCfg[m_cfgSel].cmd : "", subs[s]);
    if (!m_app->postCommand(cmd))
        Serial.printf("[ui] cmd queue full: %s\r\n", cmd);
}

void Display::runEnergySet() {
    if (!m_app) return;
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "energy battery set %.1f", m_cfgNum);
    if (!m_app->postCommand(cmd))
        Serial.printf("[ui] cmd queue full: %s\r\n", cmd);
    m_cfgActive = false;
}

void Display::runOvpSet() {
    if (!m_app) return;
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "battery ovp set %.2f", m_cfgNum);
    if (!m_app->postCommand(cmd))
        Serial.printf("[ui] cmd queue full: %s\r\n", cmd);
    m_cfgActive = false;
}



void Display::timerCb(lv_timer_t *t) {
    Display *d = (Display *)lv_timer_get_user_data(t);
    if (d) d->updateLabels();
}

void Display::buildBluetoothIcon() {
    /* Bluetooth glyph rasterized from the provided SVG (16x26) as fill runs. */
    static const uint8_t runs[][3] = {
        {0,6,1}, {1,6,2}, {2,6,3}, {3,6,4}, {4,6,6},
        {5,1,2}, {5,6,7}, {6,0,4}, {6,6,3}, {6,10,4},
        {7,2,3}, {7,6,3}, {7,11,5}, {8,3,6}, {8,11,4},
        {9,4,5}, {9,10,4}, {10,5,8}, {11,6,6}, {12,6,5},
        {13,6,5}, {14,5,7}, {15,4,9}, {16,3,6}, {16,10,4},
        {17,2,7}, {17,11,4}, {18,1,4}, {18,6,3}, {18,11,4},
        {19,0,4}, {19,6,3}, {19,10,4}, {20,1,2}, {20,6,7},
        {21,6,6}, {22,6,4}, {23,6,3}, {24,6,2}, {25,6,1},
    };
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_pos(box, 112, 5);
    lv_obj_set_size(box, 16, 26);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); i++) {
        lv_obj_t *r = lv_obj_create(box);
        lv_obj_set_pos(r, runs[i][1], runs[i][0]);
        lv_obj_set_size(r, runs[i][2], 1);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(r, 0, 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_bg_opa(r, 255, 0);
    }
    m_bt[0] = box;
    updateBluetooth();
}

void Display::updateBluetooth() {
    /* connected blue, not connected grey */
    bool connected = m_app && m_app->ble.connected();
    lv_color_t col = connected ? lv_color_hex(0x2F80ED) : lv_color_hex(0x4B5563);
    if (!m_bt[0]) return;
    uint32_t n = lv_obj_get_child_cnt(m_bt[0]);
    for (uint32_t i = 0; i < n; i++)
        lv_obj_set_style_bg_color(lv_obj_get_child(m_bt[0], i), col, 0);
}

void Display::paintBattery(float soc) {
    if (!ui_batt_fill) return;
    int fh = (int)(24.0f * soc / 100.0f);   /* vertical battery, anchored at y=32 */
    if (fh < 0) fh = 0;
    if (fh > 24) fh = 24;
    lv_obj_set_height(ui_batt_fill, fh);
    lv_obj_set_y(ui_batt_fill, 32 - fh);
    lv_color_t fc = soc > 50 ? lv_color_hex(0x00FF00)
                   : soc > 25 ? lv_color_hex(0xFFFF00)
                              : lv_color_hex(0xFF0000);
    lv_obj_set_style_bg_color(ui_batt_fill, fc, 0);
}

void Display::updateLabels() {
    if (!m_app) return;
    App &a = *m_app;
    char b[24];

    snprintf(b, sizeof(b), "%.0f%%", a.batSOC());
    setTextIfChanged(ui_soc, b);
    updateBluetooth();
    setTextIfChanged(ui_time, a.timeCached());
    setTextIfChanged(ui_date, a.dateCached());

    fmtVal(b, sizeof(b), a.batVoltage()); setTextIfChanged(ui_batV, b);
    fmtVal(b, sizeof(b), a.batCurrent()); setTextIfChanged(ui_batA, b);
    fmtVal(b, sizeof(b), a.batPower());   setTextIfChanged(ui_batW, b);
    fmtVal(b, sizeof(b), a.energyWh(CH_BAT)); setTextIfChanged(ui_batWh, b);
    paintBattery(a.batSOC());

    const PowerMeasurement &sys = a.monitor.get(CH_SYS);
    fmtVal(b, sizeof(b), sys.current); setTextIfChanged(ui_sysA, b);
    fmtVal(b, sizeof(b), sys.power);   setTextIfChanged(ui_sysW, b);
    bool wake = a.wakeActive();
    snprintf(b, sizeof(b), "%s", wake ? "A" : "S"); setTextIfChanged(ui_wake, b);
    lv_obj_set_style_text_color(ui_wake, wake ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000), 0);
    snprintf(b, sizeof(b), "%u", (unsigned)a.light()); setTextIfChanged(ui_light, b);
    float batP  = a.batPower();
    float ip1P  = a.monitor.get(CH_IP1).power;
    float ip2P  = a.monitor.get(CH_IP2).power;
    setEff(ui_effC1, a.monitor.get(CH_C1).power, effDen(ip1P, batP));
    setEff(ui_effC2, a.monitor.get(CH_C2).power, effDen(ip1P, batP));
    setEff(ui_effA1, a.monitor.get(CH_A1).power, effDen(ip2P, batP));
    setEff(ui_effA2, a.monitor.get(CH_A2).power, effDen(ip2P, batP));
    float rteRemaining = a.battery.energyRemainingWh();
    float rteFull = a.batCapacityMah() / 1000.0f * BATTERY_NOMINAL_VOLTAGE;
    float rteW  = a.batPower();   /* negative = charging */
    if (fabsf(rteW) > 0.05f) {
        float need = (rteW < 0) ? (rteFull - rteRemaining) : rteRemaining;
        if (need < 0) need = 0;
        int totMin = (int)(need / fabsf(rteW) * 60.0f + 0.5f);
        if (totMin < 0) totMin = 0;
        snprintf(b, sizeof(b), "%02dh:%02dm", totMin / 60, totMin % 60);
    } else {
        snprintf(b, sizeof(b), "00h:00m");
    }
    setTextIfChanged(ui_rte, b);

#define SET_CH(ch,V,A,W,Wh) { \
        const PowerMeasurement &pm = a.monitor.get(ch); \
        fmtVal(b,sizeof(b),pm.voltage); setTextIfChanged(V,b); \
        fmtVal(b,sizeof(b),pm.current); setTextIfChanged(A,b); \
        fmtVal(b,sizeof(b),pm.power);   setTextIfChanged(W,b); \
        fmtVal(b,sizeof(b),a.energyWh(ch)); setTextIfChanged(Wh,b); }
    SET_CH(CH_C1, ui_c1V, ui_c1A, ui_c1W, ui_c1Wh)
    SET_CH(CH_C2, ui_c2V, ui_c2A, ui_c2W, ui_c2Wh)
    SET_CH(CH_A1, ui_a1V, ui_a1A, ui_a1W, ui_a1Wh)
    SET_CH(CH_A2, ui_a2V, ui_a2A, ui_a2W, ui_a2Wh)
#undef SET_CH

    /* line1: resistance (mOhm) */
    float er = a.eloadR();
    if (er > 0.01f) snprintf(b, sizeof(b), "%.1f", er);
    else            snprintf(b, sizeof(b), "--");
    setTextIfChanged(ui_elR, b);
    const PowerMeasurement &el = a.monitor.get(CH_ELOAD);
    bool loadOn = a.power.getELoadPercent() > 0.5f;
    /* line2: A (current, 0x47); hold last valid while load is on */
    float ca = el.current;
    if (!(loadOn && fabsf(ca) < 0.05f)) {
        snprintf(b, sizeof(b), "%.2f", ca);
        setTextIfChanged(ui_elA, b);
    }
    /* line3: W from the 0x47 (ELOAD) INA226; hold last valid while load is on
     * (avoids transient 0W right after a PWM change) */
    float pw = el.power;
    if (!(loadOn && fabsf(pw) < 0.1f)) {
        snprintf(b, sizeof(b), "%.2f", pw);
        setTextIfChanged(ui_elW, b);
    }
    /* line3: temp (0x4A) */
    snprintf(b, sizeof(b), "%.1f", a.eloadTemp());
    setTextIfChanged(ui_elT, b);

    for (int i = 0; i < 4; i++) {
        float t = a.temps.readC(i);
        float v = isnan(t) ? 0.0f : t;
        snprintf(b, sizeof(b), "%.2f", v);
        if (i == 0) setTextIfChanged(ui_t1, b);
        else if (i == 1) setTextIfChanged(ui_t2, b);
        else if (i == 2) setTextIfChanged(ui_t3, b);
        else setTextIfChanged(ui_t4, b);
    }
    if (m_onScreen2) updateScreen2();
}

static const char *ipProtName(IP5356::Protocol p) {
    switch (p) {
        case IP5356::Protocol::DCP:     return "DCP(5V)";
        case IP5356::Protocol::QC_OK:   return "QC 5V";
        case IP5356::Protocol::QC2:     return "QC2.0";
        case IP5356::Protocol::QC3:     return "QC3.0";
        case IP5356::Protocol::FCP_SCP: return "FCP/SCP";
        case IP5356::Protocol::AFC:     return "AFC";
        case IP5356::Protocol::SFCP:    return "SFCP";
        case IP5356::Protocol::NONE:    return "none";
        default:                        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Screen4 - IP5356 config + all implemented IP commands               */
/* ------------------------------------------------------------------ */
#define IP4_CFG_ITEMS 12
static const char *const kIp4Names[IP4_CFG_ITEMS] = {
    "Wake", "Autowake", "Chip", "VOUT1", "VOUT2", "VBUS",
    "CC", "LLshut", "MinLoad", "NTC", "Charge", "Boost"
};
/* Commands appended after the config rows (relative to "ip <chip> ...") */
static const char *const kIp4Cmds[] = {
    "status", "cstatus", "adc", "protocol",
    "sys", "chg", "port", "pd", "stat2", "cap", "mfp", "sweep",
    "pd off", "pd on", "pd vmax 5", "pd vmax 9", "pd vmax 12",
    "prot src on", "prot src off", "prot sink on", "prot sink off",
    "outputs on", "outputs off", "sleep", "ntc", "ntc cur 80",
    "isyslow 5", "powlow 5", "llo vout2 on", "llo vout2 off"
};
#define IP4_CMD_COUNT (int)(sizeof(kIp4Cmds) / sizeof(kIp4Cmds[0]))
#define IP4_ITEMS (IP4_CFG_ITEMS + IP4_CMD_COUNT)

static const char *ip4RowName(int idx) {
    if (idx < 0 || idx >= IP4_ITEMS) return "";
    return (idx < IP4_CFG_ITEMS) ? kIp4Names[idx] : kIp4Cmds[idx - IP4_CFG_ITEMS];
}

/* current value string for a config row (chip selected) */
static void ip4Value(int idx, char *out, size_t n, IP5356 &s, uint8_t ch, bool autoWake) {
    if (idx >= IP4_CFG_ITEMS) { snprintf(out, n, "run"); return; }
    switch (idx) {
        case 0:  snprintf(out, n, "-"); break;                       /* Wake */
        case 1:  snprintf(out, n, "%s", autoWake ? "on" : "off"); break;   /* Autowake */
        case 2:  snprintf(out, n, "IP%u", ch + 1); break;              /* Chip */
        case 3:  snprintf(out, n, "%s", s.isOutputEnabled(IP5356::Port::VOUT1) ? "on" : "off"); break;
        case 4:  snprintf(out, n, "%s", s.isOutputEnabled(IP5356::Port::VOUT2) ? "on" : "off"); break;
        case 5:  snprintf(out, n, "%s", s.isVbusOk() ? "on" : "off"); break;
        case 6:  snprintf(out, n, "%s", s.getCCMode() == IP5356::CCMode::DFP ? "DFP" : "DRP"); break;
        case 7:  { uint8_t v = 0; s.readCtlReg(0x03, &v);
                   snprintf(out, n, "%s", (v & 0x0C) ? "on" : "off"); break; }
        case 8:  { uint8_t v = 0; s.readCtlReg(0x49, &v); snprintf(out, n, "%u", v); break; }
        case 9:  snprintf(out, n, "%s", s.isNTCEnabled() ? "on" : "off"); break;
        case 10: snprintf(out, n, "%s", s.isChargerEnabled() ? "on" : "off"); break;
        default: snprintf(out, n, "%s", s.isBoostEnabled() ? "on" : "off"); break;      /* Boost */
    }
}

/* apply the selected config row action (toggle) for chip-backed rows */
static void ip4Apply(int idx, IP5356 &s) {
    switch (idx) {
        case 3: { bool on = !s.isOutputEnabled(IP5356::Port::VOUT1); s.enableOutput(IP5356::Port::VOUT1, on); } break;
        case 4: { bool on = !s.isOutputEnabled(IP5356::Port::VOUT2); s.enableOutput(IP5356::Port::VOUT2, on); } break;
        case 5: break;   /* VBUS MOS is read-only; status only */
        case 6: { bool dfp = s.getCCMode() != IP5356::CCMode::DFP;
                  s.setCCMode(dfp ? IP5356::CCMode::DFP : IP5356::CCMode::DRP); } break;
        case 7: { uint8_t v = 0; s.readCtlReg(0x03, &v);
                  s.writeCtlReg(0x03, (v & 0x0C) ? (v & ~0x0C) : (v | 0x0C)); } break;  /* LLshut */
        case 8: { uint8_t v = 0; s.readCtlReg(0x49, &v);
                  uint8_t nv = (v == 0) ? 0x40 : 0x00;   /* min<->default IMOSLOW */
                  s.writeCtlReg(0x49, nv); s.writeCtlReg(0x4a, nv); s.writeCtlReg(0x4b, nv); } break;
        case 9:  s.enableNTC(!s.isNTCEnabled()); break;
        case 10: s.enableCharger(!s.isChargerEnabled()); break;
        case 11: s.enableBoost(!s.isBoostEnabled());     break;
        default: break;                                  /* Wake/Autowake/Chip handled elsewhere */
    }
}

/* Echo a line into the CLI terminal via the CLI task (thread-safe). */
static void s4Echo(App *app, const char *fmt, ...) {
    if (!app) return;
    char text[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "echo %s", text);
    app->postCommand(cmd);
}

void Display::buildScreen4() {
    m_screen4 = lv_obj_create(NULL);
    lv_obj_set_size(m_screen4, 240, 320);
    lv_obj_set_style_bg_color(m_screen4, lv_color_hex(0x102020), 0);
    lv_obj_set_style_bg_opa(m_screen4, 255, 0);
    lv_obj_set_scrollbar_mode(m_screen4, LV_SCROLLBAR_MODE_OFF);

    m_s4Ch = s2label(m_screen4, "IP5356 IP1 (MUX0)", 6, 4, &lv_font_montserrat_14, lv_color_hex(0x00FFFF));
    /* top-left: scrollable item list with a visible slider */
    m_s4Box = lv_obj_create(m_screen4);
    lv_obj_set_pos(m_s4Box, 0, 24);
    lv_obj_set_size(m_s4Box, 148, 140);
    lv_obj_set_style_bg_opa(m_s4Box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_s4Box, 1, 0);
    lv_obj_set_style_border_side(m_s4Box, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(m_s4Box, lv_color_hex(0x334155), 0);
    lv_obj_set_style_pad_all(m_s4Box, 0, 0);
    lv_obj_set_style_radius(m_s4Box, 0, 0);
    lv_obj_set_scrollbar_mode(m_s4Box, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_width(m_s4Box, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(m_s4Box, lv_color_hex(0x64748B), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(m_s4Box, 255, LV_PART_SCROLLBAR);
    lv_obj_set_scroll_dir(m_s4Box, LV_DIR_VER);
    lv_obj_add_flag(m_s4Box, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < IP4_ITEMS; i++) {
        m_s4Rows[i] = s2label(m_s4Box, " ", 4, i * 14,
                              &lv_font_montserrat_14, lv_color_hex(0x94A3B8));
        lv_obj_set_width(m_s4Rows[i], 140);
        lv_label_set_long_mode(m_s4Rows[i], LV_LABEL_LONG_CLIP);
    }

    /* top-right: IP values, one per line (original color scheme) */
    static const uint32_t kS4TeleCol[10] = {
        0xFFFF00,  /* Vbat  - yellow */
        0x00FF00,  /* Ibat  - green  */
        0x00d26a,  /* SOC   - green  */
        0x00FF00,  /* Iin   - green  */
        0x00FF00,  /* Isys  - green  */
        0x00FF00,  /* Io1   - green  */
        0x00FF00,  /* Io2   - green  */
        0xFF2020,  /* T     - red    */
        0xC96BFF,  /* Vp    - purple */
        0xFFA500   /* C/D   - orange */
    };
    for (int i = 0; i < 10; i++) {
        m_s4Tele[i] = s2label(m_screen4, "--", 150, 24 + i * 14,
                              &lv_font_montserrat_14, lv_color_hex(kS4TeleCol[i]));
        lv_obj_set_width(m_s4Tele[i], 86);
        lv_obj_set_style_text_align(m_s4Tele[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(m_s4Tele[i], LV_LABEL_LONG_CLIP);
    }

    /* bottom: terminal, same position/size as the config screen */
    lv_obj_t *tb = lv_obj_create(m_screen4);
    lv_obj_set_pos(tb, 0, 168);
    lv_obj_set_size(tb, 240, 152);
    lv_obj_set_style_bg_color(tb, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(tb, 255, 0);
    lv_obj_set_style_border_width(tb, 0, 0);
    lv_obj_set_style_radius(tb, 0, 0);
    lv_obj_add_flag(tb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tb, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(tb, LV_SCROLLBAR_MODE_OFF);
    m_termBox4 = tb;
    m_termLabel4 = lv_label_create(tb);
    lv_label_set_text(m_termLabel4, "");
    lv_obj_set_pos(m_termLabel4, 0, 0);
    lv_obj_set_width(m_termLabel4, 236);
    lv_obj_set_height(m_termLabel4, LV_SIZE_CONTENT);
    lv_label_set_long_mode(m_termLabel4, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(m_termLabel4, &font_tiny_data, 0);
    lv_obj_set_style_text_color(m_termLabel4, lv_color_hex(0x39FF14), 0);
}

void Display::updateScreen4() {
    if (!m_app) return;
    static uint32_t lastMs = 0;
    uint32_t now = millis();
    bool force = m_s4Dirty || m_s4ForceProbe;
    if (!force && (now - lastMs) < 200) return;   /* 5 Hz unless a button changed it */
    lastMs = now;
    m_s4Dirty = false;

    char b[48];
    char t[28];
    IP5356Manager &ip = m_app->ipman;
    uint8_t ch = m_ip4Ch;
    snprintf(b, sizeof(b), "IP5356 IP%u (MUX%u)%s", ch + 1, ch, m_s4Mode ? " [TERM]" : "");
    setTextIfChanged(m_s4Ch, b);

    /* Only probe the mux when we have to: on a user action (force) or, when
     * the chip is known-alive, at ~1 Hz. Never auto-probe a sleeping/absent
     * chip in a loop - probing an empty mux channel can crash the ESP32-C6
     * i2c-ng driver (Store access fault -> reboots). */
    bool probe = m_s4ForceProbe || (m_s4Present && (uint32_t)(now - m_s4LastProbe) >= 1000);
    bool present = m_s4Present;

    if (probe) {
        m_s4ForceProbe = false;
        m_s4LastProbe = now;
        present = false;
        if (ip.begin(ch)) {
            IP5356 &s = ip.current();
            present = s.isConnected();
            if (present) {
                for (int i = 0; i < IP4_ITEMS; i++) {
                    char val[20];
                    ip4Value(i, val, sizeof(val), s, ch, m_s4AutoWake);
                    m_s4Vals[i] = val;
                }
                snprintf(t, sizeof(t), "Vbat %.2fV", s.getBatteryVoltageV()); setTextIfChanged(m_s4Tele[0], t);
                snprintf(t, sizeof(t), "Ibat %.2fA", s.getBatteryCurrentA()); setTextIfChanged(m_s4Tele[1], t);
                snprintf(t, sizeof(t), "SOC %u%%", s.getSOC());              setTextIfChanged(m_s4Tele[2], t);
                snprintf(t, sizeof(t), "Iin %.2fA", s.getInputCurrentA());    setTextIfChanged(m_s4Tele[3], t);
                snprintf(t, sizeof(t), "Isys %.2fA", s.getVsysCurrentA());    setTextIfChanged(m_s4Tele[4], t);
                snprintf(t, sizeof(t), "Io1 %.2fA", s.getVout1CurrentA());    setTextIfChanged(m_s4Tele[5], t);
                snprintf(t, sizeof(t), "Io2 %.2fA", s.getVout2CurrentA());    setTextIfChanged(m_s4Tele[6], t);
                snprintf(t, sizeof(t), "T %.1fC", s.getNtcCelsius());         setTextIfChanged(m_s4Tele[7], t);
                snprintf(t, sizeof(t), "Vp %s", ipProtName(s.getVbusProtocol())); setTextIfChanged(m_s4Tele[8], t);
                snprintf(t, sizeof(t), "C %s D %s", s.isCharging() ? "ON" : "off",
                         s.isDischarging() ? "ON" : "off");                  setTextIfChanged(m_s4Tele[9], t);
            } else {
                for (int i = 0; i < IP4_ITEMS; i++) m_s4Vals[i] = "--";
                for (int i = 0; i < 10; i++) setTextIfChanged(m_s4Tele[i], "--");
            }
            ip.end();
        } else {
            for (int i = 0; i < IP4_ITEMS; i++) m_s4Vals[i] = "--";
            for (int i = 0; i < 10; i++) setTextIfChanged(m_s4Tele[i], "--");
        }
        m_s4Present = present;
    }

    /* ALWAYS re-draw the config rows so the selection marker/colour tracks
     * B1/B2 immediately even when the chip is asleep (no probe this tick). */
    for (int i = 0; i < IP4_ITEMS; i++) {
        const char *sel = (i == m_ip4Edit) ? ">" : " ";
        const char *val = m_s4Vals[i].isEmpty() ? "--" : m_s4Vals[i].c_str();
        snprintf(b, sizeof(b), "%s%-12s %s", sel, ip4RowName(i), val);
        setTextIfChanged(m_s4Rows[i], b);
        lv_color_t col = (m_s4Mode == 1) ? lv_color_hex(0x556070)
                        : (i == m_ip4Edit) ? lv_color_hex(0x00E676)
                                           : lv_color_hex(0x94A3B8);
        lv_obj_set_style_text_color(m_s4Rows[i], col, 0);
    }
    if (m_s4ScrollToEdit && m_s4Rows[m_ip4Edit]) {
        m_s4ScrollToEdit = false;
        lv_obj_scroll_to_view(m_s4Rows[m_ip4Edit], LV_ANIM_OFF);
    }

    /* optional auto-wake: only when explicitly enabled AND chip is absent */
    if (!m_s4Present && m_s4AutoWake && (uint32_t)(millis() - m_s4LastWake) > 3000) {
        m_s4LastWake = millis();
        m_app->ipman.wake();
        m_s4ForceProbe = true;
    }

    /* terminal, same place/size as the config screen; follow newest line */
    static char win4[TERM_VIEW * 76];
    static char lastWin4[TERM_VIEW * 76];
    static uint32_t lastGen4 = 0;
    static uint32_t lastTermMs4 = 0;
    uint32_t g = termGeneration();
    bool genChanged = (g != lastGen4);
    if (genChanged) {
        lastGen4 = g;
        m_s4TermOff = 0;   /* new data -> back to the newest */
    }
    if (genChanged || (now - lastTermMs4) >= 50) {
        lastTermMs4 = now;
        termLineSnapshot(win4, sizeof(win4), TERM_VIEW, m_s4TermOff);
        if (strcmp(win4, lastWin4) != 0) {
            memcpy(lastWin4, win4, sizeof(lastWin4));
            lv_label_set_text(m_termLabel4, win4);
            if (m_termBox4) lv_obj_scroll_to_y(m_termBox4, LV_COORD_MAX, LV_ANIM_OFF);
        }
    }
}

void Display::ip4CycleChannel() {
    m_ip4Ch ^= 1;
    m_s4Present = false;
    m_s4ForceProbe = true;
    m_s4Dirty = true;      /* UI task redraws; no LVGL call from the button task */
}

void Display::ip4PrevSetting() {
    m_ip4Edit = (m_ip4Edit == 0) ? (IP4_ITEMS - 1) : (m_ip4Edit - 1);
    m_s4Dirty = true;
    m_s4ScrollToEdit = true;
}

void Display::ip4NextSetting() {
    m_ip4Edit++;
    if (m_ip4Edit >= IP4_ITEMS) m_ip4Edit = 0;
    m_s4Dirty = true;
    m_s4ScrollToEdit = true;
}

void Display::ip4ToggleSetting() {
    if (!m_app) return;
    m_s4Dirty = true;
    if (m_ip4Edit == 0) {
        s4Echo(m_app, "> WAKE IP%u", m_ip4Ch + 1);
        m_app->ipman.wake();
        m_s4ForceProbe = true;
        return;
    }
    if (m_ip4Edit == 1) {
        m_s4AutoWake = !m_s4AutoWake;
        s4Echo(m_app, "> AUTOWAKE %s", m_s4AutoWake ? "on" : "off");
        m_s4ForceProbe = true;
        return;
    }
    if (m_ip4Edit == 2) {
        ip4CycleChannel();
        s4Echo(m_app, "> CHIP IP%u (MUX%u)", m_ip4Ch + 1, m_ip4Ch);
        return;
    }
    if (m_ip4Edit >= IP4_CFG_ITEMS) {
        const char *cmdTail = kIp4Cmds[m_ip4Edit - IP4_CFG_ITEMS];
        char cmd[48];
        snprintf(cmd, sizeof(cmd), "ip %u %s", m_ip4Ch + 1, cmdTail);
        s4Echo(m_app, "> %s", cmd);
        m_app->postCommand(cmd);
        m_s4ForceProbe = true;
        return;
    }
    IP5356Manager &ip = m_app->ipman;
    if (!ip.begin(m_ip4Ch)) {
        s4Echo(m_app, "> %s (no chip)", ip4RowName(m_ip4Edit));
        return;
    }
    IP5356 &s = ip.current();
    if (!s.isConnected()) {
        s4Echo(m_app, "> %s (asleep)", ip4RowName(m_ip4Edit));
        ip.end();
        return;
    }

    switch (m_ip4Edit) {
        case 3:  { bool on = !s.isOutputEnabled(IP5356::Port::VOUT1);
                   s4Echo(m_app, "> VOUT1 %s", on ? "on" : "off"); } break;
        case 4:  { bool on = !s.isOutputEnabled(IP5356::Port::VOUT2);
                   s4Echo(m_app, "> VOUT2 %s", on ? "on" : "off"); } break;
        case 5:  s4Echo(m_app, "> VBUS %s (read-only)", s.isVbusOk() ? "on" : "off"); break;
        case 6:  { bool dfp = s.getCCMode() != IP5356::CCMode::DFP;
                   s4Echo(m_app, "> CC %s", dfp ? "DFP" : "DRP"); } break;
        case 7:  { uint8_t v = 0; s.readCtlReg(0x03, &v);
                   s4Echo(m_app, "> LLSHUT %s", (v & 0x0C) ? "off" : "on"); } break;
        case 8:  { uint8_t v = 0; s.readCtlReg(0x49, &v);
                   s4Echo(m_app, "> MINLOAD %u", (v == 0) ? 0x40 : 0); } break;
        case 9:  s4Echo(m_app, "> NTC %s", s.isNTCEnabled() ? "off" : "on"); break;
        case 10: s4Echo(m_app, "> CHARGE %s", s.isChargerEnabled() ? "off" : "on"); break;
        case 11: s4Echo(m_app, "> BOOST %s", s.isBoostEnabled() ? "off" : "on"); break;
        default: s4Echo(m_app, "> %s", ip4RowName(m_ip4Edit)); break;
    }

    if (m_ip4Edit != 5) ip4Apply(m_ip4Edit, s);
    ip.end();
    m_s4ForceProbe = true;   /* re-read actual state in the UI task */
}
















