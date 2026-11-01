/**
 * @file home_gen.c
 * @brief APB Dashboard screen (LVGL). Fixed 240x320 grid, no scrolling.
 *        Values are right-aligned so unit columns (V/A/W/Wh) stay stable.
 */

#include "home_gen.h"
#include "../apb_ui.h"

extern const lv_font_t font_lbl18_data;
extern const lv_font_t font_tiny_data;

#define ACCENT  lv_color_hex(0x00d26a)
#define BG      lv_color_hex(0x101020)
#define MUTED   lv_color_hex(0x94a3b8)
#define COL_V   lv_color_hex(0xFFFF00)  /* yellow   - voltage  */
#define COL_A   lv_color_hex(0x00FF00)  /* green    - current  */
#define COL_W   lv_color_hex(0xFFA500)  /* orange   - power    */
#define COL_WH  lv_color_hex(0x00FFFF)  /* cyan     - energy   */
#define COL_T   lv_color_hex(0xFF0000)  /* red      - temp     */

/* Exported value labels (updated by firmware) */
lv_obj_t * ui_soc;
lv_obj_t * ui_time;
lv_obj_t * ui_date;
lv_obj_t * ui_batt_fill;
lv_obj_t * ui_batV; lv_obj_t * ui_batA; lv_obj_t * ui_batW; lv_obj_t * ui_batWh;
lv_obj_t * ui_c1V;  lv_obj_t * ui_c1A;  lv_obj_t * ui_c1W;  lv_obj_t * ui_c1Wh;
lv_obj_t * ui_c2V;  lv_obj_t * ui_c2A;  lv_obj_t * ui_c2W;  lv_obj_t * ui_c2Wh;
lv_obj_t * ui_elW;  lv_obj_t * ui_elR; lv_obj_t * ui_elA; lv_obj_t * ui_elT;
lv_obj_t * ui_sysA; lv_obj_t * ui_sysW; lv_obj_t * ui_wake;
lv_obj_t * ui_light;
lv_obj_t * ui_rte;
lv_obj_t * ui_effC1; lv_obj_t * ui_effC2; lv_obj_t * ui_effA1; lv_obj_t * ui_effA2;
lv_obj_t * ui_a1V;  lv_obj_t * ui_a1A;  lv_obj_t * ui_a1W;  lv_obj_t * ui_a1Wh;
lv_obj_t * ui_a2V;  lv_obj_t * ui_a2A;  lv_obj_t * ui_a2W;  lv_obj_t * ui_a2Wh;
lv_obj_t * ui_t1;   lv_obj_t * ui_t2;   lv_obj_t * ui_t3;   lv_obj_t * ui_t4;

/* Right-aligned value label (fixed width -> stable unit column) */
static lv_obj_t * rlbl(lv_obj_t * par, lv_color_t col, int x, int y,
                       int w, const char * txt)
{
    lv_obj_t * l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(l, font_lbl, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_bg_opa(l, 0, 0);
    return l;
}

/* Measurement + smaller unit suffix, right-aligned as one group. The flex
 * row packs [number][unit] from the right edge, so V/A/W/Wh hug the value. */
#define UNIT_ROW_H 23
static lv_obj_t * rvlbl(lv_obj_t * par, lv_color_t col, int x, int y,
                        int w, const char * num, const char * unit)
{
    lv_obj_t * row = lv_obj_create(par);
    lv_obj_set_pos(row, x, y);
    lv_obj_set_size(row, w, UNIT_ROW_H);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 1, 0);
    lv_obj_set_style_bg_opa(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END);

    lv_obj_t * v = lv_label_create(row);
    lv_label_set_text(v, num);
    lv_obj_set_style_text_font(v, font_lbl, 0);
    lv_obj_set_style_text_color(v, col, 0);
    lv_obj_set_style_bg_opa(v, 0, 0);

    lv_obj_t * u = lv_label_create(row);
    lv_label_set_text(u, unit);
    lv_obj_set_style_text_font(u, &font_tiny_data, 0);
    lv_obj_set_style_text_color(u, col, 0);
    lv_obj_set_style_bg_opa(u, 0, 0);
    lv_obj_set_style_translate_y(u, 1, 0);
    return v;
}

/* Rotated (90 deg) section label */
static void vlbl(lv_obj_t * par, const char * txt, int x, int y)
{
    lv_obj_t * l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, &font_lbl18_data, 0);
    lv_obj_set_style_text_color(l, MUTED, 0);
    lv_obj_set_style_bg_opa(l, 0, 0);
    lv_obj_set_style_transform_angle(l, 900, 0);
    lv_obj_set_style_transform_pivot_x(l, 0, 0);
    lv_obj_set_style_transform_pivot_y(l, 0, 0);
}

/* Rotated (90 deg) value label, returned for firmware updates */
static lv_obj_t * vval(lv_obj_t * par, const char * txt, int x, int y, lv_color_t col)
{
    lv_obj_t * l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, &font_lbl18_data, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_bg_opa(l, 0, 0);
    lv_obj_set_style_transform_angle(l, 900, 0);
    lv_obj_set_style_transform_pivot_x(l, 0, 0);
    lv_obj_set_style_transform_pivot_y(l, 0, 0);
    return l;
}

lv_obj_t * home_create(void)
{
    lv_obj_t * root = lv_obj_create(NULL);
    lv_obj_set_width(root, lv_pct(100));
    lv_obj_set_height(root, lv_pct(100));
    lv_obj_set_style_bg_color(root, BG, 0);
    lv_obj_set_style_bg_opa(root, 255, 0);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);

    /* ---- Battery icon (vertical, left of SOC %) ---- */
    lv_obj_t * batt_body = lv_obj_create(root);
    lv_obj_set_pos(batt_body, 4, 6);
    lv_obj_set_size(batt_body, 20, 28);
    lv_obj_set_style_radius(batt_body, 3, 0);
    lv_obj_set_style_border_width(batt_body, 2, 0);
    lv_obj_set_style_border_color(batt_body, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(batt_body, 0, 0);

    lv_obj_t * batt_nub = lv_obj_create(root);
    lv_obj_set_pos(batt_nub, 11, 2);
    lv_obj_set_size(batt_nub, 6, 4);
    lv_obj_set_style_radius(batt_nub, 1, 0);
    lv_obj_set_style_bg_color(batt_nub, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(batt_nub, 255, 0);

    ui_batt_fill = lv_obj_create(root);
    lv_obj_set_pos(ui_batt_fill, 6, 32);
    lv_obj_set_size(ui_batt_fill, 16, 0);
    lv_obj_set_style_radius(ui_batt_fill, 1, 0);
    lv_obj_set_style_bg_color(ui_batt_fill, COL_A, 0);
    lv_obj_set_style_bg_opa(ui_batt_fill, 255, 0);

    /* SOC % (big) to the left of the clock, using the freed space */
    ui_soc  = lv_label_create(root);
    lv_label_set_text(ui_soc, "76%");
    lv_obj_set_pos(ui_soc, 30, 6);
    lv_obj_set_style_text_font(ui_soc, font_val, 0);
    lv_obj_set_style_text_color(ui_soc, ACCENT, 0);
    lv_obj_set_style_bg_opa(ui_soc, 0, 0);

    ui_time = lv_label_create(root);
    lv_label_set_text(ui_time, "--:--:--");
    lv_obj_set_pos(ui_time, 135, 3);
    lv_obj_set_width(ui_time, 100);
    lv_obj_set_style_text_align(ui_time, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(ui_time, font_lbl, 0);
    lv_obj_set_style_text_color(ui_time, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ui_time, 0, 0);

    ui_date = lv_label_create(root);
    lv_label_set_text(ui_date, "Mo 23-09");
    lv_obj_set_pos(ui_date, 135, 18);
    lv_obj_set_width(ui_date, 100);
    lv_obj_set_style_text_align(ui_date, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(ui_date, font_lbl, 0);
    lv_obj_set_style_text_color(ui_date, MUTED, 0);
    lv_obj_set_style_bg_opa(ui_date, 0, 0);

    /* ---- Header divider at y=36 ---- */
    static lv_point_precise_t hline[2] = {{0, 36}, {239, 36}};
    lv_obj_t * divider = lv_line_create(root);
    lv_line_set_points(divider, hline, 2);
    lv_obj_set_style_line_color(divider, MUTED, 0);
    lv_obj_set_style_line_width(divider, 1, 0);

    /* ---- Left column: section x=16, right-aligned values x=24 ---- */
    vlbl(root, "BAT", 16, 40);
    ui_batV  = rvlbl(root, COL_V, 27, 40, 92, "0.000", "V");
    ui_batA  = rvlbl(root, COL_A, 27, 56, 92, "0.000", "A");
    ui_batW  = rvlbl(root, COL_W, 27, 72, 92, "0.000", "W");
    ui_batWh = rvlbl(root, COL_WH, 27, 89, 92, "0.00", "Wh");

    vlbl(root, "C1", 16, 112);
    ui_effC1 = vval(root, "99%", 17, 135, lv_color_white());
    ui_c1V  = rvlbl(root, COL_V, 27, 112, 92, "0.000", "V");
    ui_c1A  = rvlbl(root, COL_A, 27, 128, 92, "0.000", "A");
    ui_c1W  = rvlbl(root, COL_W, 27, 144, 92, "0.000", "W");
    ui_c1Wh = rvlbl(root, COL_WH, 27, 161, 92, "0.00", "Wh");

    vlbl(root, "C2", 16, 184);
    ui_effC2 = vval(root, "99%", 17, 207, lv_color_white());
    ui_c2V  = rvlbl(root, COL_V, 27, 184, 92, "0.000", "V");
    ui_c2A  = rvlbl(root, COL_A, 27, 200, 92, "0.000", "A");
    ui_c2W  = rvlbl(root, COL_W, 27, 216, 92, "0.000", "W");
    ui_c2Wh = rvlbl(root, COL_WH, 27, 233, 92, "0.00", "Wh");

    vlbl(root, "ELOAD", 16, 252);
    ui_elR = rvlbl(root, lv_color_hex(0xFF69B4), 27, 252, 92, "--", "m");
    ui_elA = rvlbl(root, COL_A, 27, 268, 92, "0.00", "A");
    ui_elW = rvlbl(root, COL_W, 27, 284, 92, "0.00", "W");
    ui_elT = rvlbl(root, COL_T, 27, 300, 92, "0.0", "C");

    /* ---- Right column: section x=142, right-aligned values to screen edge ---- */
    vlbl(root, "SYS", 140, 40);
    ui_sysA  = rvlbl(root, COL_A, 145, 40, 92, "0.000", "A");
    ui_sysW  = rvlbl(root, COL_W, 145, 56, 92, "0.000", "W");
    ui_light = rvlbl(root, lv_color_white(), 145, 72, 60, "0", "%");
    ui_wake  = rlbl(root, lv_color_white(), 205, 74, 30, "S");
    ui_rte   = rlbl(root, lv_color_hex(0x9B30FF), 145, 92, 92, "0.0h");

    vlbl(root, "A1", 140, 112);
    ui_effA1 = vval(root, "99%", 140, 135, lv_color_white());
    ui_a1V  = rvlbl(root, COL_V, 145, 112, 92, "0.000", "V");
    ui_a1A  = rvlbl(root, COL_A, 145, 128, 92, "0.000", "A");
    ui_a1W  = rvlbl(root, COL_W, 145, 144, 92, "0.000", "W");
    ui_a1Wh = rvlbl(root, COL_WH, 145, 161, 92, "0.00", "Wh");

    vlbl(root, "A2", 140, 184);
    ui_effA2 = vval(root, "99%", 140, 207, lv_color_white());
    ui_a2V  = rvlbl(root, COL_V, 145, 184, 92, "0.000", "V");
    ui_a2A  = rvlbl(root, COL_A, 145, 200, 92, "0.000", "A");
    ui_a2W  = rvlbl(root, COL_W, 145, 216, 92, "0.000", "W");
    ui_a2Wh = rvlbl(root, COL_WH, 145, 233, 92, "0.00", "Wh");

    vlbl(root, "TEMP", 140, 252);
    ui_t1 = rvlbl(root, COL_T, 145, 252, 92, "0.00", "C");
    ui_t2 = rvlbl(root, COL_T, 145, 268, 92, "0.00", "C");
    ui_t3 = rvlbl(root, COL_T, 145, 284, 92, "0.00", "C");
    ui_t4 = rvlbl(root, COL_T, 145, 300, 92, "0.00", "C");

    return root;
}

















