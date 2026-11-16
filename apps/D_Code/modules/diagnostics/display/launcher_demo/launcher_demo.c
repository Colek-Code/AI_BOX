/**
 * @file launcher_demo.c
 * @brief 全屏横向启动器演示页实现。
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_LAUNCHER_DEMO)

#include "modules/diagnostics/display/launcher_demo/launcher_demo.h"
#include "framework/d_page_manager.h"
#include "framework/d_ui_font.h"
#include "lvgl.h"
#include <stdint.h>

#define LOG_TAG "[MAIN_PAGE] "

#define LAUNCHER_PAGE_COUNT     4
#define LAUNCHER_ICON_SIZE      200
#define LAUNCHER_LABEL_H        36
#define DETAIL_CLOSE_SIZE       48

typedef struct {
    const char *name;
    uint32_t color;
} launcher_item_t;

static const launcher_item_t s_launcher_items[LAUNCHER_PAGE_COUNT] = {
    {"应用1", 0x38BDF8},
    {"应用2", 0x34D399},
    {"应用3", 0xF59E0B},
    {"应用4", 0xFB7185},
};

static lv_obj_t *s_tileview = NULL;
static lv_obj_t *s_detail_page = NULL;
static lv_obj_t *s_detail_title = NULL;
static lv_obj_t *s_launcher_tiles[LAUNCHER_PAGE_COUNT];
static int s_launcher_index = 0;

/**
 * @brief 清除边框、内边距及圆角，并禁用对象滚动。
 * @param[in,out] obj 待调整的 LVGL 对象；为空时不处理。
 */
static void launcher_demo_clean_obj(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 清理对象并铺满逻辑显示尺寸，设置不透明背景。
 * @param[in,out] obj 待调整的 LVGL 对象；为空时不处理。
 * @param[in] color 背景颜色。
 */
static void launcher_demo_set_fullscreen(lv_obj_t *obj, lv_color_t color)
{
    if (obj == NULL) {
        return;
    }

    launcher_demo_clean_obj(obj);
    lv_obj_set_size(obj, MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

/**
 * @brief 将磁贴设置为全屏、不透明无渐变的演示背景。
 * @param[in,out] tile 磁贴对象；为空时不处理。
 */
static void launcher_demo_style_tile(lv_obj_t *tile)
{
    if (tile == NULL) {
        return;
    }

    launcher_demo_clean_obj(tile);
    lv_obj_set_size(tile, MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_NONE, 0);
}

/**
 * @brief 依据页索引只允许存在相邻页的水平滑动。
 * @param[in] index 启动器页索引。
 * @return 第一页仅向右，末页仅向左，中间页允许双向。
 */
static lv_dir_t launcher_demo_tile_dir(int index)
{
    if (index == 0) {
        return LV_DIR_RIGHT;
    }
    if (index == LAUNCHER_PAGE_COUNT - 1) {
        return LV_DIR_LEFT;
    }
    return LV_DIR_LEFT | LV_DIR_RIGHT;
}

/**
 * @brief 按有效应用索引更新详情标题并将详情层置前。
 * @param[in] index 0～LAUNCHER_PAGE_COUNT-1 的应用索引。
 */
static void launcher_demo_show_detail(int index)
{
    if (s_detail_page == NULL || s_detail_title == NULL) {
        return;
    }
    if (index < 0 || index >= LAUNCHER_PAGE_COUNT) {
        return;
    }

    lv_label_set_text(s_detail_title, s_launcher_items[index].name);
    lv_obj_remove_flag(s_detail_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_detail_page);
}

/**
 * @brief 将事件用户数据中的应用索引传给详情层。
 * @param[in] e 带应用索引的点击事件。
 */
static void on_launcher_icon_click(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    launcher_demo_show_detail(index);
}

/**
 * @brief 隐藏已有详情层。
 * @param[in] e 点击事件，本实现不使用其内容。
 */
static void on_detail_close_click(lv_event_t *e)
{
    (void)e;
    if (s_detail_page != NULL) {
        lv_obj_add_flag(s_detail_page, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 由当前激活磁贴更新缓存页索引。
 * @param[in] e 页面值变化事件，本实现不使用其内容。
 */
static void on_launcher_tile_changed(lv_event_t *e)
{
    lv_obj_t *active_tile;

    (void)e;
    if (s_tileview == NULL) {
        return;
    }

    active_tile = lv_tileview_get_tile_active(s_tileview);
    for (int i = 0; i < LAUNCHER_PAGE_COUNT; i++) {
        if (active_tile == s_launcher_tiles[i]) {
            s_launcher_index = i;
            return;
        }
    }
}

/**
 * @brief 切换到不同且有效的启动器页。
 * @param[in] index 目标页索引。
 * @param[in] anim_en LVGL 动画开关。
 * @return 完成切换请求返回 1；未创建、越界或已经当前页返回 0。
 */
static int launcher_demo_set_index(int index, lv_anim_enable_t anim_en)
{
    if (s_tileview == NULL) {
        return 0;
    }
    if (index < 0 || index >= LAUNCHER_PAGE_COUNT) {
        return 0;
    }
    if (index == s_launcher_index) {
        return 0;
    }

    s_launcher_index = index;
    lv_tileview_set_tile_by_index(s_tileview, (uint32_t)s_launcher_index, 0, anim_en);
    printf(LOG_TAG "Launcher page %d\n", s_launcher_index);
    return 1;
}

/**
 * @brief 创建一页应用图标及标题，并绑定详情点击。
 * @param[in,out] tile 承载图标的磁贴。
 * @param[in] index 应用索引。
 * @return 创建成功返回 0；无效父对象、索引或分配失败返回 -1。
 * @note 中途失败不自行删除已经创建的子对象，由页面容器生命周期收尾。
 */
static int launcher_demo_create_tile(lv_obj_t *tile, int index)
{
    lv_obj_t *icon;
    lv_obj_t *label;
    lv_color_t base_color;

    if (tile == NULL || index < 0 || index >= LAUNCHER_PAGE_COUNT) {
        return -1;
    }

    launcher_demo_style_tile(tile);
    base_color = lv_color_hex(s_launcher_items[index].color);

    icon = lv_btn_create(tile);
    if (icon == NULL) {
        return -1;
    }
    lv_obj_set_size(icon, LAUNCHER_ICON_SIZE, LAUNCHER_ICON_SIZE);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -24);
    launcher_demo_clean_obj(icon);
    lv_obj_set_style_radius(icon, 28, 0);
    lv_obj_set_style_bg_color(icon, base_color, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(icon, lv_color_darken(base_color, LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(icon, 0, 0);
    lv_obj_set_style_shadow_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(icon, on_launcher_icon_click, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    label = lv_label_create(tile);
    if (label == NULL) {
        return -1;
    }
    lv_label_set_text(label, s_launcher_items[index].name);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF9FAFB), 0);
    lv_obj_set_style_text_font(label, d_ui_font_get_default(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(label, MY_DISP_HOR_RES, LAUNCHER_LABEL_H);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, LAUNCHER_ICON_SIZE / 2 + 12);
    return 0;
}

/**
 * @brief 创建横向 tileview 与四个应用磁贴。
 * @param[in,out] parent 承载新建子对象的父容器。
 * @return 成功返回 0，任一创建失败返回 -1。
 */
static int launcher_demo_create_launcher(lv_obj_t *parent)
{
    s_tileview = lv_tileview_create(parent);
    if (s_tileview == NULL) {
        return -1;
    }
    launcher_demo_set_fullscreen(s_tileview, lv_color_black());
    lv_obj_update_layout(s_tileview);
    lv_obj_add_flag(s_tileview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_tileview, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tileview, on_launcher_tile_changed, LV_EVENT_VALUE_CHANGED, NULL);

    for (int i = 0; i < LAUNCHER_PAGE_COUNT; i++) {
        lv_obj_t *tile = lv_tileview_add_tile(s_tileview, i, 0, launcher_demo_tile_dir(i));
        if (tile == NULL) {
            return -1;
        }
        s_launcher_tiles[i] = tile;
        if (launcher_demo_create_tile(tile, i) != 0) {
            return -1;
        }
    }
    lv_tileview_set_tile_by_index(s_tileview, 0, 0, LV_ANIM_OFF);
    return 0;
}

/**
 * @brief 创建初始隐藏的全屏详情层、关闭按钮与标题。
 * @param[in,out] parent 承载新建子对象的父容器。
 * @return 成功返回 0，任一创建失败返回 -1。
 */
static int launcher_demo_create_detail_page(lv_obj_t *parent)
{
    lv_obj_t *close_btn;
    lv_obj_t *close_label;

    s_detail_page = lv_obj_create(parent);
    if (s_detail_page == NULL) {
        return -1;
    }
    launcher_demo_set_fullscreen(s_detail_page, lv_color_hex(0x020617));
    lv_obj_add_flag(s_detail_page, LV_OBJ_FLAG_HIDDEN);

    close_btn = lv_btn_create(s_detail_page);
    if (close_btn == NULL) {
        return -1;
    }
    lv_obj_set_size(close_btn, DETAIL_CLOSE_SIZE, DETAIL_CLOSE_SIZE);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    launcher_demo_clean_obj(close_btn);
    lv_obj_set_style_radius(close_btn, DETAIL_CLOSE_SIZE / 2, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x374151), LV_STATE_PRESSED);
    lv_obj_add_event_cb(close_btn, on_detail_close_click, LV_EVENT_CLICKED, NULL);

    close_label = lv_label_create(close_btn);
    if (close_label == NULL) {
        return -1;
    }
    lv_label_set_text(close_label, "X");
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_24, 0);
    lv_obj_center(close_label);

    s_detail_title = lv_label_create(s_detail_page);
    if (s_detail_title == NULL) {
        return -1;
    }
    lv_label_set_text(s_detail_title, s_launcher_items[0].name);
    lv_obj_set_style_text_color(s_detail_title, lv_color_hex(0xF9FAFB), 0);
    lv_obj_set_style_text_font(s_detail_title, d_ui_font_get_default(), 0);
    lv_obj_set_style_text_align(s_detail_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_detail_title, MY_DISP_HOR_RES);
    lv_obj_center(s_detail_title);
    return 0;
}

/**
 * @brief 重置静态引用，并依次创建启动器与详情层。
 * @param[in,out] parent 承载新建子对象的父容器。
 * @return 成功返回 0；父对象为空或子阶段失败返回 -1。
 */
static int launcher_demo_create(lv_obj_t *parent)
{
    lv_obj_t *scr;

    if (parent == NULL) {
        return -1;
    }

    s_tileview = NULL;
    s_detail_page = NULL;
    s_detail_title = NULL;
    s_launcher_index = 0;
    for (int i = 0; i < LAUNCHER_PAGE_COUNT; i++) {
        s_launcher_tiles[i] = NULL;
    }

    scr = lv_obj_get_parent(parent);
    if (scr != NULL) {
        launcher_demo_clean_obj(scr);
    }
    launcher_demo_set_fullscreen(parent, lv_color_black());

    if (launcher_demo_create_launcher(parent) != 0) {
        return -1;
    }
    if (launcher_demo_create_detail_page(parent) != 0) {
        return -1;
    }

    printf(LOG_TAG "Horizontal launcher demo created\n");
    return 0;
}

/**
 * @brief 记录启动器页面进入事件。
 */
static void launcher_demo_enter(void)
{
    printf(LOG_TAG "Launcher demo enter\n");
}

/**
 * @brief 记录启动器页面退出事件。
 */
static void launcher_demo_exit(void)
{
    printf(LOG_TAG "Launcher demo exit\n");
}

/**
 * @brief 在详情层隐藏时把左右手势映射到页索引。
 * @param[in] gesture_type 页面管理器手势编号，3 下一页、4 上一页。
 * @return 已切换返回 1；详情层可见、边界或无关手势返回 0。
 */
static int launcher_demo_gesture_handler(int gesture_type)
{
    if (s_detail_page != NULL && !lv_obj_has_flag(s_detail_page, LV_OBJ_FLAG_HIDDEN)) {
        return 0;
    }

    switch (gesture_type) {
    case 3:
        return launcher_demo_set_index(s_launcher_index + 1, LV_ANIM_ON);
    case 4:
        return launcher_demo_set_index(s_launcher_index - 1, LV_ANIM_ON);
    default:
        break;
    }

    return 0;
}

static const page_ops_t s_launcher_demo_ops = {
    .create  = launcher_demo_create,
    .enter   = launcher_demo_enter,
    .exit    = launcher_demo_exit,
    .destroy = NULL,
    .update  = NULL,
    .gesture = launcher_demo_gesture_handler,
};

static const page_desc_t s_launcher_demo_desc = {
    .page_type  = PAGE_TYPE_MAIN,
    .name       = "LauncherDemo",
    .ops        = &s_launcher_demo_ops,
    .container  = NULL,
    .is_created = 0,
    .is_active  = 0,
};

/**
 * @brief 注册静态页面描述；完整合同见 launcher_demo.h。
 */
int launcher_demo_register(void)
{
    return page_manager_register(&s_launcher_demo_desc);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_LAUNCHER_DEMO) */
