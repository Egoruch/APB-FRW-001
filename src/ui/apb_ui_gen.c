/**
 * @file apb_ui_gen.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "apb_ui_gen.h"

#if defined(LV_USE_XML) && LV_USE_XML
#endif /* LV_USE_XML */

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void check_font(lv_font_t ** font, const char * name);

/**********************
 *  STATIC VARIABLES
 **********************/

static uint32_t apb_ui_target = APB_UI_TARGET_ALL;

/*----------------
 * Translations
 *----------------*/

#ifndef LV_EDITOR_PREVIEW
    static const char * translation_languages[] = {"en", "de", NULL};
    static const char * translation_tags[] = {"dog", "cat", "house", NULL};
    static const char * translation_texts[] = {
        "This is a dog", "Das ist ein Hund", /* dog */
        "A curious little cat", "Eine neugierige kleine Katze", /* cat */
        "The house is cozy and warm", "Das Haus ist gemütlich und warm", /* house */
    };
#endif

/**********************
 *  GLOBAL VARIABLES
 **********************/

/*--------------------
 *  Permanent screens
 *-------------------*/

/*----------------
 * Fonts
 *----------------*/

lv_font_t * font_lbl;
extern lv_font_t font_lbl_data;
lv_font_t * font_val;
extern lv_font_t font_val_data;
lv_font_t * font_h2;
extern lv_font_t font_h2_data;
lv_font_t * font_big;   /* left NULL; font_big not used by the layout */

/*----------------
 * Images
 *----------------*/



/*----------------
 * Global styles
 *----------------*/

/*----------------
 * Subjects
 *----------------*/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void apb_ui_init_gen(const char * asset_path)
{

    /*----------------
     * Fonts
     *----------------*/

    /* Targets: any */

    #if APB_UI_CHECK_COMPILE_TARGET(APB_UI_TARGET_ALL)
    if (apb_ui_check_target(APB_UI_TARGET_ALL)) {
        if (!font_lbl) {
            /* font_lbl */
            /* get font 'font_lbl' from a C array */
            font_lbl = &font_lbl_data;

        }
        if (!font_val) {
            /* font_val */
            /* get font 'font_val' from a C array */
            font_val = &font_val_data;

        }
        if (!font_h2) {
            /* font_h2 */
            /* get font 'font_h2' from a C array */
            font_h2 = &font_h2_data;

        }
    }
    #endif

    /*----------------
     * Images
     *----------------*/



    /*----------------
     * Global styles
     *----------------*/

    /*----------------
     * Subjects
     *----------------*/
    /*----------------
     * Translations
     *----------------*/

    #ifndef LV_EDITOR_PREVIEW
        lv_translation_add_static(translation_languages, translation_tags, translation_texts);
        lv_translation_set_language(translation_languages[0]);
    #endif

#if defined(LV_USE_XML) && LV_USE_XML
    /* Register widgets */

    /* Check all fonts / default if needed. This prevents fonts that are used in one target but
       defined in another from causing assertion failures during rendering of the Preview. */
    check_font(&font_lbl, "font_lbl");
    check_font(&font_val, "font_val");
    check_font(&font_h2, "font_h2");

    /* Register fonts */
    lv_xml_register_font(NULL, "font_lbl", font_lbl);
    lv_xml_register_font(NULL, "font_val", font_val);
    lv_xml_register_font(NULL, "font_h2", font_h2);

    /* Register subjects */

    /* Register callbacks */
#endif

    /* Register all the global assets so that they won't be created again when globals.xml is parsed.
     * While running in the editor skip this step to update the preview when the XML changes */
#if defined(LV_USE_XML) && LV_USE_XML && !defined(LV_EDITOR_PREVIEW)
    /* Register images */
#endif

#if !defined(LV_USE_XML) || LV_USE_XML == 0
    /*--------------------
     *  Permanent screens
     *-------------------*/
    /* If XML is enabled it's assumed that the permanent screens are created
     * manually from XML using lv_xml_create() */
#endif
}

void apb_ui_set_target(uint32_t target)
{
    apb_ui_target = target;
}

uint32_t apb_ui_get_target(void)
{
    return apb_ui_target;
}

bool apb_ui_check_target(uint32_t target)
{
    return (apb_ui_target & target) ? true : false;
}

/* Callbacks */

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void check_font(lv_font_t ** font, const char * name)
{
    if (!(*font)) {
        *font = (lv_font_t *)LV_FONT_DEFAULT;
        LV_LOG_WARN("font `%s` was not set. Using `LV_FONT_DEFAULT` instead", name);
    }
}