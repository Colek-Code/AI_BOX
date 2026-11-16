/**
 * @file d_lvgl_profile_config.h
 * @brief 为 SDK 性能探针提供显式开启的薄配置适配。
 */
#ifndef D_LVGL_PROFILE_CONFIG_H
#define D_LVGL_PROFILE_CONFIG_H

/* 以 SDK 原配置为基础，不另建显示、操作系统或渲染参数分支。 */
#include "lv_conf.h"
#undef LV_USE_PROFILER
#define LV_USE_PROFILER 1
#undef LV_USE_PROFILER_BUILTIN
#define LV_USE_PROFILER_BUILTIN 0
#undef LV_PROFILER_INCLUDE
#define LV_PROFILER_INCLUDE "framework/d_lvgl_profile.h"

/* __func__ 和显式标签在每个 SDK 调用点都是常量；未选中的钩子应被编译消除。
 * 使用探针数据前，仍须核对目标 IR/ELF 是否覆盖对应调用点。 */
#define D_LVGL_PROFILE_FUNCTION_SCOPE(name) \
    (__builtin_strcmp((name), "lv_obj_update_layout") == 0 ? D_LVGL_PROFILE_OBJECT_LAYOUT : \
     __builtin_strcmp((name), "lv_draw_dispatch") == 0 ? D_LVGL_PROFILE_DISPATCH : \
     __builtin_strcmp((name), "call_flush_cb") == 0 ? D_LVGL_PROFILE_FLUSH_CALLBACK : \
     __builtin_strcmp((name), "execute_drawing") == 0 ? D_LVGL_PROFILE_SW_RASTER : \
     __builtin_strcmp((name), "lv_draw_sw_label") == 0 ? D_LVGL_PROFILE_SW_LABEL : D_LVGL_PROFILE_COUNT)
#define D_LVGL_PROFILE_FUNCTION_MARK(entering) do { \
    const uint8_t d_lvgl_scope = D_LVGL_PROFILE_FUNCTION_SCOPE(__func__); \
    if (d_lvgl_scope < D_LVGL_PROFILE_COUNT) d_lvgl_profile_mark(d_lvgl_scope, (entering)); \
} while (0)
#undef LV_PROFILER_BEGIN
#define LV_PROFILER_BEGIN D_LVGL_PROFILE_FUNCTION_MARK(1U)
#undef LV_PROFILER_END
#define LV_PROFILER_END D_LVGL_PROFILE_FUNCTION_MARK(0U)
#undef LV_PROFILER_BEGIN_TAG
#define LV_PROFILER_BEGIN_TAG(tag) do { \
    if (__builtin_strcmp((tag), "layout") == 0) d_lvgl_profile_mark(D_LVGL_PROFILE_REFRESH_LAYOUT, 1U); \
} while (0)
#undef LV_PROFILER_END_TAG
#define LV_PROFILER_END_TAG(tag) do { \
    if (__builtin_strcmp((tag), "layout") == 0) d_lvgl_profile_mark(D_LVGL_PROFILE_REFRESH_LAYOUT, 0U); \
} while (0)
#endif
