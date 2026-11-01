/**
 * @file home_gen.h
 */

#ifndef LVGL_PRO_HOME_GEN_H
#define LVGL_PRO_HOME_GEN_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
    #include "lvgl_private.h"
#else
    #include "lvgl/lvgl.h"
    #include "lvgl/lvgl_private.h"
#endif

#if defined(LV_USE_XML) && LV_USE_XML
    #include "lv_xml/lv_xml.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/



lv_obj_t * home_create(void);

/* Exported value labels for firmware updates */
extern lv_obj_t * ui_soc;
extern lv_obj_t * ui_time;
extern lv_obj_t * ui_date;
extern lv_obj_t * ui_batt_fill;
extern lv_obj_t * ui_batV; extern lv_obj_t * ui_batA; extern lv_obj_t * ui_batW; extern lv_obj_t * ui_batWh;
extern lv_obj_t * ui_c1V;  extern lv_obj_t * ui_c1A;  extern lv_obj_t * ui_c1W;  extern lv_obj_t * ui_c1Wh;
extern lv_obj_t * ui_c2V;  extern lv_obj_t * ui_c2A;  extern lv_obj_t * ui_c2W;  extern lv_obj_t * ui_c2Wh;
extern lv_obj_t * ui_elW;  extern lv_obj_t * ui_elR; extern lv_obj_t * ui_elA; extern lv_obj_t * ui_elT;
extern lv_obj_t * ui_sysA; extern lv_obj_t * ui_sysW; extern lv_obj_t * ui_wake;
extern lv_obj_t * ui_light;
extern lv_obj_t * ui_rte;
extern lv_obj_t * ui_effC1; extern lv_obj_t * ui_effC2; extern lv_obj_t * ui_effA1; extern lv_obj_t * ui_effA2;
extern lv_obj_t * ui_a1V;  extern lv_obj_t * ui_a1A;  extern lv_obj_t * ui_a1W;  extern lv_obj_t * ui_a1Wh;
extern lv_obj_t * ui_a2V;  extern lv_obj_t * ui_a2A;  extern lv_obj_t * ui_a2W;  extern lv_obj_t * ui_a2Wh;
extern lv_obj_t * ui_t1;   extern lv_obj_t * ui_t2;   extern lv_obj_t * ui_t3;   extern lv_obj_t * ui_t4;

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LVGL_PRO_HOME_GEN_H*/