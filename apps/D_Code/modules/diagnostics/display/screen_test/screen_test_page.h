/**
 * @file screen_test_page.h
 * @brief GC9307/HYN 扫描线与触摸点诊断页注册入口。
 */

#ifndef __SCREEN_TEST_PAGE_H__
#define __SCREEN_TEST_PAGE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_SCREEN_TEST)

/**
 * @brief 将GC9307/HYN 扫描线与触摸点诊断页描述注册到页面管理器。
 * @return 页面管理器的注册结果，0 成功，负值失败。
 * @note 仅注册描述，不创建页面对象；实际页面由页面管理器按需创建。
 */
int screen_test_page_register(void);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_SCREEN_TEST) */
#endif /* __SCREEN_TEST_PAGE_H__ */
