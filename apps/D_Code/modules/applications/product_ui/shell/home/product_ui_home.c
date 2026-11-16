/** @file product_ui_home.c @brief Product UI 首页壁纸、有限应用槽、横向分页及统一导航。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shell/home/product_ui_home.h"
#include "modules/applications/product_ui/shell/app_registry/product_ui_app_registry.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/assets/product_ui_color_icons.h"
#include "modules/applications/product_ui/assets/product_ui_icons.h"
#include "modules/applications/product_ui/assets/product_ui_wallpapers.h"
#include "modules/applications/product_ui/runtime/preferences/product_ui_appearance.h"
#include "platform/screen/transition/d_page_transition.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include "modules/applications/product_ui/debug/product_ui_perf.h"
#include "modules/applications/product_ui/debug/product_ui_visual_capture.h"
#include <string.h>
#include "modules/applications/product_ui/features/time/product_ui_pomodoro_notice.h"
#include "modules/applications/product_ui/features/time/product_ui_alarm_notice.h"

#define PRODUCT_UI_HOME_SLOT_COUNT        6
#define PRODUCT_UI_HOME_PAGE_COUNT        2
#define PRODUCT_UI_HOME_CAPTURE_PX        8
#define PRODUCT_UI_HOME_COMMIT_PX        80
#define PRODUCT_UI_HOME_FLICK_PX         16
#define PRODUCT_UI_HOME_FLICK_SPEED     450
#define PRODUCT_UI_HOME_FLICK_SNAP_MIN_MS 68U
#define PRODUCT_UI_HOME_DOT_SIZE           6
#define PRODUCT_UI_HOME_DOTS_Y           190

typedef struct {
    lv_obj_t *root;
    lv_obj_t *normal;
    uint8_t wallpaper_id;
} product_ui_home_scene_t;

static product_ui_home_scene_t s_home_scene;

/**
 * @brief 删除已建壁纸场景并清空其静态引用。
 * @return 清理后返回 -1，供创建失败路径透传。
 */
static int product_ui_home_scene_fail(void)
{
    if (s_home_scene.root != NULL) {
        lv_obj_delete(s_home_scene.root);
    }
    memset(&s_home_scene, 0, sizeof(s_home_scene));
    return -1;
}

/**
 * @brief 场景根删除时清空背景对象及已绑定壁纸身份。
 * @param[in] event 场景 DELETE 事件，当前未读取载荷。
 */
static void product_ui_home_scene_delete_cb(lv_event_t *event)
{
    (void)event;
    memset(&s_home_scene, 0, sizeof(s_home_scene));
}

/**
 * @brief 创建静态确认壁纸背景，已有场景时复用当前实例。
 * @param[in,out] parent 首页根容器。
 * @return 成功或已有场景返回 0，父对象、资源或图像创建失败返回 -1 并清局部场景。
 */
static int product_ui_home_scene_create(lv_obj_t *parent)
{
    product_ui_appearance_snapshot_t appearance;
    const product_ui_wallpaper_t *wallpaper;

    if (parent == NULL) {
        return -1;
    }
    if (s_home_scene.root != NULL) {
        return 0;
    }
    s_home_scene.root = lv_obj_create(parent);
    if (s_home_scene.root == NULL) {
        return -1;
    }
    lv_obj_remove_style_all(s_home_scene.root);
    lv_obj_set_size(s_home_scene.root, PRODUCT_UI_SCREEN_W,
                    PRODUCT_UI_SCREEN_H);
    lv_obj_set_pos(s_home_scene.root, 0, 0);
    lv_obj_set_style_bg_opa(s_home_scene.root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_home_scene.root,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_home_scene.root, product_ui_home_scene_delete_cb,
                        LV_EVENT_DELETE, NULL);

    product_ui_appearance_get_snapshot(&appearance);
    wallpaper = product_ui_wallpaper_get(appearance.wallpaper_id);
    if (wallpaper == NULL) return product_ui_home_scene_fail();
    s_home_scene.normal = lv_image_create(s_home_scene.root);
    if (s_home_scene.normal == NULL) {
        return product_ui_home_scene_fail();
    }
    lv_image_set_src(s_home_scene.normal, wallpaper->image);
    s_home_scene.wallpaper_id = appearance.wallpaper_id;
    lv_obj_set_pos(s_home_scene.normal, 0, 0);
    lv_obj_clear_flag(s_home_scene.normal,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_move_background(s_home_scene.root);
    return 0;
}

/**
 * @brief 把外观服务最新确认的壁纸应用到现有背景，未变时不重绑。
 */
void product_ui_home_enter(void)
{
    product_ui_appearance_snapshot_t appearance;
    const product_ui_wallpaper_t *wallpaper;
    if (s_home_scene.normal == NULL) return;
    product_ui_appearance_get_snapshot(&appearance);
    if (appearance.wallpaper_id == s_home_scene.wallpaper_id) return;
    wallpaper = product_ui_wallpaper_get(appearance.wallpaper_id);
    if (wallpaper == NULL) return;
    lv_image_set_src(s_home_scene.normal, wallpaper->image);
    s_home_scene.wallpaper_id = appearance.wallpaper_id;
    d_screen_request_composed_frame();
}

typedef struct {
    product_ui_app_slot_t view;
    const product_ui_app_entry_t *app;
    const d_ui_module_t *module;
} product_ui_home_hotspot_t;

/* 状态栏下方的三列两行网格，为横屏彩色图标保留较大画布。 */
static const int32_t s_home_columns[] = {80, 80, 80, LV_GRID_TEMPLATE_LAST};
static const int32_t s_home_rows[] = {80, 80, LV_GRID_TEMPLATE_LAST};

#ifdef D_PRODUCT_UI_PAGER_TRACE_ENABLE
#define PRODUCT_UI_PAGER_LOG(...) \
    printf("[ProductUI-PAGER] " __VA_ARGS__)
#else
#define PRODUCT_UI_PAGER_LOG(...)
#endif

static product_ui_page_view_t s_home_page_view;
static product_ui_home_hotspot_t s_home_slots[PRODUCT_UI_HOME_PAGE_COUNT][PRODUCT_UI_HOME_SLOT_COUNT];
/* 仅轮询两个提醒标题，槽重新绑定后不能跨身份复用有效文字缓存。 */
static product_ui_label_cache_t s_home_notice_text[2];
static lv_obj_t *s_home_layers[PRODUCT_UI_HOME_PAGE_COUNT];
static lv_obj_t *s_home_page_dots;
static uint8_t s_home_page_count;
static uint8_t s_home_current_page;
static uint8_t s_home_dragged;
static struct {
    product_ui_motion_t motion;
    lv_point_t start;
    int32_t start_offset;
    int32_t velocity;
    int32_t last_x;
    uint32_t last_tick;
    uint8_t start_page;
    uint8_t target_page;
    uint8_t is_tracking;
    uint8_t is_captured;
    uint8_t has_two_layers;
} s_home_pager;

/**
 * @brief 清除对象默认样式及滚动能力。
 * @param[in,out] obj 调用链保证有效的对象。
 */
static void product_ui_home_clean_obj(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 分页手势未捕获时按槽内静态模块身份执行统一导航。
 * @param[in] event 应用槽点击事件，用户数据借用槽状态。
 * @note 拖动之后的旧点击直接忽略；导航失败只提示，不创建替代模块。
 */
static void product_ui_home_app_cb(lv_event_t *event)
{
    product_ui_home_hotspot_t *slot;
    int result;

    slot = lv_event_get_user_data(event);
    if (s_home_dragged) {
        PRODUCT_UI_TRACE("app_click suppressed after drag\n");
        return;
    }
    if ((slot != NULL) && (slot->app != NULL) && (slot->module != NULL)) {
        PRODUCT_UI_TRACE("app_click id=%s title=%s\n", slot->module->id,
                         slot->module->display_name);
        result = product_ui_open_module(slot->module->id);
        PRODUCT_UI_TRACE("app_result id=%s result=%d\n",
                         slot->module->id, result);
        if (result != 0) {
            product_ui_toast_show("应用打开失败", 1200U);
        }
    }
}

/**
 * @brief 按已经收敛的当前页更新分页点透明度。
 */
static void product_ui_home_update_page_dots(void)
{
    lv_obj_t *dot;
    uint32_t index;
    uint32_t count;

    if (s_home_page_dots == NULL) {
        return;
    }
    count = lv_obj_get_child_count(s_home_page_dots);
    for (index = 0; index < count; index++) {
        dot = lv_obj_get_child(s_home_page_dots, (int32_t)index);
        lv_obj_set_style_bg_opa(dot, (index == s_home_current_page) ?
                               LV_OPA_COVER : LV_OPA_40, 0);
    }
}

/**
 * @brief 显式把单色模块图标身份映射到首页彩色资源编号。
 * @param[in] icon_id 应用目录提供的图标身份。
 * @param[out] out_id 成功接收彩色资源编号，失败不改。
 * @return 找到并写入为 0；空输出或未映射为 -1，不回退成设置图标。
 */
static int product_ui_home_color_icon_id(product_ui_icon_id_t icon_id,
                                         product_ui_color_icon_id_t *out_id)
{
    if (out_id == NULL) {
        return -1;
    }
    switch (icon_id) {
    case PRODUCT_UI_ICON_MUSIC:
        *out_id = PRODUCT_UI_COLOR_ICON_MUSIC;
        return 0;
    case PRODUCT_UI_ICON_CALENDAR:
        *out_id = PRODUCT_UI_COLOR_ICON_CALENDAR;
        return 0;
    case PRODUCT_UI_ICON_CALCULATOR:
        *out_id = PRODUCT_UI_COLOR_ICON_CALCULATOR;
        return 0;
    case PRODUCT_UI_ICON_GALLERY:
        *out_id = PRODUCT_UI_COLOR_ICON_GALLERY;
        return 0;
    case PRODUCT_UI_ICON_ALARM:
        *out_id = PRODUCT_UI_COLOR_ICON_ALARM;
        return 0;
    case PRODUCT_UI_ICON_TIMER:
        *out_id = PRODUCT_UI_COLOR_ICON_POMODORO;
        return 0;
    case PRODUCT_UI_ICON_SCHEDULE:
        *out_id = PRODUCT_UI_COLOR_ICON_SCHEDULE;
        return 0;
    case PRODUCT_UI_ICON_SETTINGS:
        *out_id = PRODUCT_UI_COLOR_ICON_SETTINGS;
        return 0;
    case PRODUCT_UI_ICON_AI_CHAT:
        *out_id = PRODUCT_UI_COLOR_ICON_AI_CHAT;
        return 0;
    case PRODUCT_UI_ICON_CAMERA:
        *out_id = PRODUCT_UI_COLOR_ICON_CAMERA;
        return 0;
    default:
        /* ASSET_MANIFEST: forbidden_fallback_icon=settings */
        return -1;
    }
}

/**
 * @brief 按首页顺序键读取静态应用并绑定有限槽，空位使用独立空槽图像。
 * @param[in] layer_index 调用方保证小于两层的实际图层下标。
 * @param[in] slot_index 当前层六槽下标。
 * @param[in] page_index 该层当前应代表的逻辑首页页号。
 * @return 完整绑定为 0，槽、目录、描述符或资源不符为 -1。
 * @note 重绑先清提醒文字缓存，不跨应用身份复用已确认标签请求。
 */
static int product_ui_home_bind_slot(uint8_t layer_index, uint8_t slot_index, uint8_t page_index)
{
    product_ui_home_hotspot_t *slot;
    product_ui_app_slot_spec_t spec = {0};
    product_ui_color_icon_id_t color_id = PRODUCT_UI_COLOR_ICON_EMPTY_SLOT;
    uint8_t catalog_index;

    if (slot_index >= PRODUCT_UI_HOME_SLOT_COUNT) {
        return -1;
    }
    slot = &s_home_slots[layer_index][slot_index];
    memset(s_home_notice_text, 0, sizeof(s_home_notice_text));
    catalog_index = (uint8_t)(page_index * PRODUCT_UI_HOME_SLOT_COUNT + slot_index);
    slot->app = NULL;
    slot->module = NULL;
    if (catalog_index < product_ui_app_registry_home_count()) {
        slot->app = product_ui_app_registry_home_get(catalog_index);
        if (slot->app == NULL) {
            return -1;
        }
        slot->module = product_ui_app_entry_module(slot->app);
        if ((slot->module == NULL) ||
            (product_ui_home_color_icon_id(slot->app->home_icon, &color_id) != 0)) {
            return -1;
        }
        spec.title = slot->module->display_name;
        spec.is_enabled = product_ui_module_available(slot->module->id);
    }
    spec.image = product_ui_color_icon_get(color_id);
    return product_ui_app_slot_bind(&slot->view, &spec);
}

/**
 * @brief 撤销两层全部应用槽的完整按压资格及按下外观。
 */
static void product_ui_home_cancel_slots(void)
{
    uint8_t page, index;
    for (page = 0U; page < PRODUCT_UI_HOME_PAGE_COUNT; page++) {
        for (index = 0U; index < PRODUCT_UI_HOME_SLOT_COUNT; index++) {
            product_ui_app_slot_cancel(&s_home_slots[page][index].view);
        }
    }
}

/**
 * @brief 按动画位移更新实际图层请求坐标，仅变化时请求组合帧。
 * @param[in] context 动画上下文，当前未使用。
 * @param[in] offset 分页整体横向像素位移。
 * @note 比较目标局部样式而非可能延后布局的坐标；单层回退保持图层在零位。
 */
static void product_ui_home_apply_offset(void *context, int32_t offset)
{
    uint8_t page;
    uint8_t is_changed = 0U;
    (void)context;
    for (page = 0U; page < PRODUCT_UI_HOME_PAGE_COUNT; page++) {
        if (s_home_layers[page] != NULL) {
            int32_t target_x = s_home_pager.has_two_layers ?
                offset + page * PRODUCT_UI_SCREEN_W : 0;
            /* 布局可能延后，比较请求位置而不是过期坐标。 */
            if (lv_obj_get_style_x(s_home_layers[page], LV_PART_MAIN) != target_x) {
                lv_obj_set_x(s_home_layers[page], target_x);
                is_changed = 1U;
            }
        }
    }
    if (is_changed) d_screen_request_composed_frame();
}

/**
 * @brief 终态确认当前页，单层回退重绑该逻辑页，再同步分页点和诊断观察请求。
 * @param[in] context 动画完成上下文，当前未使用。
 */
static void product_ui_home_page_settled(void *context)
{
    uint8_t index;
    uint8_t is_page_changed = s_home_current_page != s_home_pager.target_page;
    (void)context;
    s_home_current_page = s_home_pager.target_page;
    if (!s_home_pager.has_two_layers) {
        for (index = 0U; index < PRODUCT_UI_HOME_SLOT_COUNT; index++) {
            (void)product_ui_home_bind_slot(0U, index, s_home_current_page);
        }
    }
    product_ui_home_update_page_dots();
    /* 最终分页点及单层回退重绑也须进入组合帧。 */
    if (is_page_changed) d_screen_request_composed_frame();
    product_ui_visual_capture_request(
        s_home_current_page == 0U ? "HM-00" : "HM-02");
}

/**
 * @brief 验证目标页并按距离及同向释放速度选择有限吸附时长。
 * @param[in] page_index 有效目标首页页号。
 * @param[in] release_velocity 释放时横向像素每秒，负值向左。
 * @note 先取消槽手势；单层回退或减少动态效果沿用共用终态路径，不改变显示刷新节奏。
 */
static void product_ui_home_switch_page(uint8_t page_index, int32_t release_velocity)
{
    int32_t target, distance, delta;
    uint32_t duration, minimum = PRODUCT_UI_MOTION_ENTER_MS;
    if (page_index >= s_home_page_count) return;
    product_ui_home_cancel_slots();
    s_home_pager.target_page = page_index;
    target = -(int32_t)page_index * PRODUCT_UI_SCREEN_W;
    delta = target - s_home_pager.motion.current;
    distance = LV_ABS(delta);
    duration = (uint32_t)distance;
    if (((delta < 0 && release_velocity < 0) || (delta > 0 && release_velocity > 0)) &&
        LV_ABS(release_velocity) >= PRODUCT_UI_HOME_FLICK_SPEED) {
        /* 指针坐标限定距离及速度；让接近终点的快速甩动及时收敛，
         * 不改变显示节奏及慢拖策略。 */
        duration = (uint32_t)distance * 1000U / (uint32_t)LV_ABS(release_velocity);
        minimum = PRODUCT_UI_HOME_FLICK_SNAP_MIN_MS;
    }
    duration = s_home_pager.has_two_layers ?
        LV_CLAMP(minimum, duration, PRODUCT_UI_MOTION_SNAP_MS) : 0U;
    product_ui_motion_start(&s_home_pager.motion, target, duration);
}

/**
 * @brief 未捕获拖动时，把分页点下标转换为显式切页。
 * @param[in] event 分页点点击事件。
 */
static void product_ui_home_dot_cb(lv_event_t *event)
{
    lv_obj_t *dot;
    int32_t index;

    dot = lv_event_get_target(event);
    index = lv_obj_get_index(dot);
    if (index >= 0 && !s_home_pager.is_captured) {
        s_home_pager.is_tracking = 0U;
        product_ui_home_switch_page((uint8_t)index, 0);
    }
}

/**
 * @brief 把分页范围外的拖动收敛为最大十二像素弹性偏移。
 * @param[in] offset 按下初始位移加当前指针差。
 * @return 正常区间原值，越界区间按三分之一并限制弹性距离后的值。
 */
static int32_t product_ui_home_drag_offset(int32_t offset)
{
    int32_t minimum = -(int32_t)(s_home_page_count - 1U) * PRODUCT_UI_SCREEN_W;
    if (offset > 0) return LV_MIN(12, offset / 3);
    if (offset < minimum) return minimum - LV_MIN(12, (minimum - offset) / 3);
    return offset;
}

/**
 * @brief 跟踪单个横向分页手势，捕获后取消应用点击，释放按速度或距离决定目标。
 * @param[in] event 按下、移动、松手、丢失或输入重置事件。
 * @note 丢失／重置回到按下起始页，旧点击被标记抑制；慢拖和快速近端甩动分别遵守既有时长门限。
 */
static void product_ui_home_swipe_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = code == LV_EVENT_INDEV_RESET ? NULL : lv_event_get_indev(event);
    lv_point_t point;
    int32_t dx, dy, displacement;
    int32_t target;
    uint32_t now, elapsed;

    if (code == LV_EVENT_PRESSED) {
        if (indev != NULL) {
            s_home_dragged = s_home_pager.motion.is_active;
            product_ui_motion_cancel(&s_home_pager.motion, 0U);
            if (s_home_dragged) product_ui_home_cancel_slots();
            lv_indev_get_point(indev, &s_home_pager.start);
            s_home_pager.start_offset = s_home_pager.motion.current;
            s_home_pager.start_page = s_home_current_page;
            s_home_pager.last_x = s_home_pager.start.x;
            s_home_pager.last_tick = lv_tick_get();
            s_home_pager.velocity = 0;
            s_home_pager.is_tracking = 1U;
            s_home_pager.is_captured = 0U;
        }
        return;
    }
    if (!s_home_pager.is_tracking) return;
    if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
        s_home_pager.is_tracking = 0U;
        s_home_dragged = 1U;
        product_ui_home_switch_page(s_home_pager.start_page, 0);
        return;
    }
    if ((code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) || indev == NULL) return;
    lv_indev_get_point(indev, &point);
    dx = point.x - s_home_pager.start.x;
    dy = point.y - s_home_pager.start.y;
    now = lv_tick_get();
    elapsed = now - s_home_pager.last_tick;
    if (point.x != s_home_pager.last_x && elapsed != 0U) {
        s_home_pager.velocity = (point.x - s_home_pager.last_x) * 1000 / (int32_t)elapsed;
        s_home_pager.last_x = point.x;
        s_home_pager.last_tick = now;
    } else if (elapsed > 150U) {
        s_home_pager.velocity = 0;
    }
    if (!s_home_pager.is_captured && LV_ABS(dx) > PRODUCT_UI_HOME_CAPTURE_PX &&
        LV_ABS(dx) * 2 >= LV_ABS(dy) * 3) {
        s_home_pager.is_captured = 1U;
        s_home_dragged = 1U;
        product_ui_home_cancel_slots();
    }
    if (s_home_pager.is_captured) {
        displacement = product_ui_home_drag_offset(s_home_pager.start_offset + dx);
        if (s_home_pager.motion.is_active || displacement != s_home_pager.motion.current) {
            product_ui_motion_set(&s_home_pager.motion, displacement);
        }
    }
    if (code != LV_EVENT_RELEASED) return;
    s_home_pager.is_tracking = 0U;
    if (!s_home_pager.is_captured && !s_home_dragged) return;
    target = s_home_pager.start_page;
    displacement = s_home_pager.motion.current + target * PRODUCT_UI_SCREEN_W;
    if (LV_ABS(dx) >= PRODUCT_UI_HOME_FLICK_PX &&
        LV_ABS(s_home_pager.velocity) >= PRODUCT_UI_HOME_FLICK_SPEED) {
        target += s_home_pager.velocity < 0 ? 1 : -1;
    } else if (LV_ABS(displacement) >= PRODUCT_UI_HOME_COMMIT_PX) {
        target += displacement < 0 ? 1 : -1;
    }
    product_ui_home_switch_page((uint8_t)LV_CLAMP(0, target, s_home_page_count - 1), s_home_pager.velocity);
}

/**
 * @brief 按逻辑页数创建可点击分页点并同步当前页。
 * @param[in,out] content 首页内容父容器。
 * @return 完整创建为 0，任一对象失败为 -1。
 */
static int product_ui_home_page_dots_create(lv_obj_t *content)
{
    lv_obj_t *dot;
    uint8_t index;

    s_home_page_dots = lv_obj_create(content);
    if (s_home_page_dots == NULL) {
        return -1;
    }
    product_ui_home_clean_obj(s_home_page_dots);
    lv_obj_set_size(s_home_page_dots, 48, 14);
    lv_obj_align(s_home_page_dots, LV_ALIGN_TOP_MID, 0, PRODUCT_UI_HOME_DOTS_Y);
    lv_obj_set_flex_flow(s_home_page_dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_home_page_dots, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_home_page_dots, 8, 0);
    lv_obj_set_style_bg_opa(s_home_page_dots, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_home_page_dots, LV_OBJ_FLAG_EVENT_BUBBLE);
    for (index = 0; index < s_home_page_count; index++) {
        dot = lv_obj_create(s_home_page_dots);
        if (dot == NULL) {
            return -1;
        }
        product_ui_home_clean_obj(dot);
        lv_obj_set_size(dot, PRODUCT_UI_HOME_DOT_SIZE, PRODUCT_UI_HOME_DOT_SIZE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_40, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(dot, product_ui_home_dot_cb, LV_EVENT_CLICKED, NULL);
    }
    product_ui_home_update_page_dots();
    return 0;
}

/**
 * @brief 为一个实际图层创建六个持久应用槽并绑定该逻辑首页页。
 * @param[in,out] content 首页内容父容器。
 * @param[in] page 调用方保证有效的实际图层及初始逻辑页号。
 * @return 完整创建／绑定为 0，任一步失败为 -1，部分层留给上层回退或页面删除。
 */
static int product_ui_home_layer_create(lv_obj_t *content, uint8_t page)
{
    uint8_t index;
    lv_obj_t *layer = lv_obj_create(content);
    if (layer == NULL) return -1;
    s_home_layers[page] = layer;
    product_ui_home_clean_obj(layer);
    lv_obj_set_size(layer, PRODUCT_UI_SCREEN_W, 184);
    lv_obj_set_pos(layer, page * PRODUCT_UI_SCREEN_W, 0);
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(layer, 32, 0);
    lv_obj_set_style_pad_top(layer, 12, 0);
    lv_obj_set_style_pad_column(layer, 8, 0);
    lv_obj_set_style_pad_row(layer, 8, 0);
    lv_obj_set_grid_dsc_array(layer, s_home_columns, s_home_rows);
    lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);

    for (index = 0; index < PRODUCT_UI_HOME_SLOT_COUNT; index++) {
        product_ui_home_hotspot_t *slot = &s_home_slots[page][index];

        if (product_ui_app_slot_create(layer, &slot->view,
                                        product_ui_home_app_cb, slot) != 0) {
            return -1;
        }
        lv_obj_set_grid_cell(slot->view.root,
                              LV_GRID_ALIGN_START, index % 3U, 1,
                              LV_GRID_ALIGN_START, index / 3U, 1);
        if (product_ui_home_bind_slot(page, index, page) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief 按目录数创建有限页层、分页动画和点，第二层失败时保留单层重绑回退。
 * @param[in,out] content 首页内容容器，拥有分页动画对象生命周期。
 * @return 第一层及点完整创建为 0，必要创建失败为 -1。
 */
static int product_ui_home_hotspots_create(lv_obj_t *content)
{
    uint8_t app_count = product_ui_app_registry_home_count();
    const product_ui_motion_binding_t binding = {
        .owner = content, .apply = product_ui_home_apply_offset,
        .complete = product_ui_home_page_settled,
    };
    s_home_page_count = LV_CLAMP(1U,
        (app_count + PRODUCT_UI_HOME_SLOT_COUNT - 1U) / PRODUCT_UI_HOME_SLOT_COUNT,
        PRODUCT_UI_HOME_PAGE_COUNT);
    if (product_ui_home_layer_create(content, 0U) != 0) return -1;
    if (s_home_page_count > 1U) {
        if (product_ui_home_layer_create(content, 1U) == 0) {
            s_home_pager.has_two_layers = 1U;
        } else if (s_home_layers[1] != NULL) {
            lv_obj_delete(s_home_layers[1]);
            s_home_layers[1] = NULL;
        }
    }
    product_ui_motion_bind(&s_home_pager.motion, &binding);
    product_ui_motion_set(&s_home_pager.motion, 0);
    if (product_ui_home_page_dots_create(content) != 0) return -1;
    PRODUCT_UI_PAGER_LOG("create apps=%u pages=%u mode=color_icons\n",
                         app_count, s_home_page_count);
    return 0;
}

/**
 * @brief 页面删除时解除分页及槽动画，清空全部首页引用并解除页面框架状态。
 * @param[in] event 首页 DELETE 事件，当前未读取载荷。
 */
static void product_ui_home_delete_cb(lv_event_t *event)
{
    uint8_t page, index;
    (void)event;
    product_ui_motion_dispose(&s_home_pager.motion);
    for (page = 0U; page < PRODUCT_UI_HOME_PAGE_COUNT; page++) {
        for (index = 0U; index < PRODUCT_UI_HOME_SLOT_COUNT; index++) {
            product_ui_app_slot_dispose(&s_home_slots[page][index].view);
        }
    }
    memset(s_home_slots, 0, sizeof(s_home_slots));
    memset(s_home_notice_text, 0, sizeof(s_home_notice_text));
    memset(s_home_layers, 0, sizeof(s_home_layers));
    memset(&s_home_pager, 0, sizeof(s_home_pager));
    s_home_page_dots = NULL;
    s_home_page_count = 0;
    s_home_current_page = 0;
    s_home_dragged = 0;
    product_ui_page_view_reset(&s_home_page_view);
}

/**
 * @brief 创建首页框架、已确认壁纸及最多两页六格应用，绑定横向分页。
 */
int product_ui_home_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {
        .title = "桌面",
        .show_back = 0,
        .role = PRODUCT_UI_PAGE_ROLE_HOME,
    };
    uint32_t started_at;

    if (parent == NULL) {
        return -1;
    }
    memset(s_home_notice_text, 0, sizeof(s_home_notice_text));
    started_at = lv_tick_get();
    if (product_ui_page_frame_create_ex(parent, &spec,
                                        &s_home_page_view) != 0) {
        return -1;
    }
    lv_obj_set_style_pad_all(s_home_page_view.content, 0, 0);
    lv_obj_add_event_cb(parent, product_ui_home_delete_cb, LV_EVENT_DELETE, NULL);
    if ((product_ui_home_scene_create(parent) != 0) ||
        (product_ui_home_hotspots_create(s_home_page_view.content) != 0)) {
        return -1;
    }
    lv_obj_add_event_cb(s_home_page_view.content, product_ui_home_swipe_cb,
                        LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_home_page_view.content, product_ui_home_swipe_cb,
                        LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_home_page_view.content, product_ui_home_swipe_cb,
                        LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_home_page_view.content, product_ui_home_swipe_cb,
                        LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(s_home_page_view.content, product_ui_home_swipe_cb,
                        LV_EVENT_INDEV_RESET, NULL);
    product_ui_visual_capture_request("HM-00");
    product_ui_perf_page_created("Home", started_at);
    return 0;
}

/**
 * @brief 推进公共 UI 与壁纸，分页静止时更新提醒标记和真实模块可用性。
 */
void product_ui_home_update(uint32_t elapsed_ms)
{
    uint8_t page, index;

    product_ui_process(elapsed_ms);
    product_ui_home_enter();
    if (s_home_pager.is_tracking || s_home_pager.motion.is_active) return;
    for (page = 0U; page < PRODUCT_UI_HOME_PAGE_COUNT; page++) {
        for (index = 0U; index < PRODUCT_UI_HOME_SLOT_COUNT; index++) {
            product_ui_home_hotspot_t *slot = &s_home_slots[page][index];
            if (slot->module != NULL && slot->view.title != NULL && strcmp(slot->module->id, "pomodoro") == 0) {
                const char *title = product_ui_pomodoro_notice_pending() ? "番茄钟 ·" : "番茄钟";
                (void)product_ui_label_sync(slot->view.title, title, &s_home_notice_text[0]);
            }
            if (slot->module != NULL && slot->view.title != NULL && strcmp(slot->module->id, "alarm") == 0) {
                const char *title = product_ui_alarm_notice_pending() ? "闹钟 ·" : "闹钟";
                (void)product_ui_label_sync(slot->view.title, title, &s_home_notice_text[1]);
            }
            if (slot->module != NULL && slot->view.root != NULL &&
                slot->view.is_enabled != product_ui_module_available(slot->module->id)) {
                (void)product_ui_home_bind_slot(page, index,
                    s_home_pager.has_two_layers ? page : s_home_current_page);
            }
        }
    }
}

/**
 * @brief 退出时收敛到起始或已预约页面，撤销所有应用槽的旧按压资格。
 */
void product_ui_home_exit(void)
{
    if (s_home_pager.is_tracking) {
        s_home_pager.is_tracking = 0U;
        s_home_pager.target_page = s_home_pager.start_page;
        product_ui_motion_start(&s_home_pager.motion,
            -(int32_t)s_home_pager.start_page * PRODUCT_UI_SCREEN_W, 0U);
    } else {
        product_ui_motion_cancel(&s_home_pager.motion, 1U);
    }
    product_ui_home_cancel_slots();
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
