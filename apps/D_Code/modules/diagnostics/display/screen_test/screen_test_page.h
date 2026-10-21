/**
 * @file screen_test_page.h
 * @brief GC9307/HYN screen-only validation page.
 */

#ifndef __SCREEN_TEST_PAGE_H__
#define __SCREEN_TEST_PAGE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_SCREEN_TEST)

int screen_test_page_register(void);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_SCREEN_TEST) */
#endif /* __SCREEN_TEST_PAGE_H__ */
