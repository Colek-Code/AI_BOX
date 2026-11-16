/**
 * @file d_page_manager.c
 * @brief LVGL 页面管理框架 - 实现
 */

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "d_page_manager.h"
#include "d_ui_module.h"
#include <string.h>

#define LOG_TAG "[PAGE_MGR] "

//*********************************************************************************//
//                              内部状态                                            //
//*********************************************************************************//

#define PAGE_MAX_COUNT (D_MODULE_MAX_COUNT + 1)

/* 页面注册表 */
static page_desc_t s_pages[PAGE_MAX_COUNT];
static int s_page_count = 0;

/* 当前页面 */
static int g_current_page = PAGE_TYPE_MAIN;

static unsigned int s_transition_id = 0;
static uint8_t s_is_switching;

//*********************************************************************************//
//                              内部函数                                            //
//*********************************************************************************//

/**
 * @brief 在静态注册表查找页面描述符。
 * @param[in] page_type 要查找的页面编号。
 * @return 指向表内元素的借用指针；未登记时为 NULL。
 */
static page_desc_t *find_page(int page_type)
{
    for (int i = 0; i < s_page_count; i++) {
        if (s_pages[i].page_type == page_type) {
            return &s_pages[i];
        }
    }
    return NULL;
}

/**
 * @brief 显示已有容器、标记激活，再调用进入回调。
 * @param[in,out] page 页面描述符；NULL 或没有容器时直接返回。
 */
static void show_page(page_desc_t *page)
{
    if (!page || !page->container) return;

    lv_obj_remove_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(page->container);
    page->is_active = 1;

    if (page->ops && page->ops->enter) {
        page->ops->enter();
    }
}

/**
 * @brief 先调用退出回调，再隐藏容器并清除激活标志。
 * @param[in,out] page 页面描述符；NULL 或没有容器时不处理。
 */
static void hide_page(page_desc_t *page)
{
    if (!page || !page->container) return;

    if (page->ops && page->ops->exit) {
        page->ops->exit();
    }

    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    page->is_active = 0;
}

/**
 * @brief 通知页面释放自有对象，再销毁容器并清除创建状态。
 * @param[in,out] page 页面描述符；未创建时不重复销毁。
 * @note 页面 destroy 回调负责自身业务清理，容器由本函数删除；不负责停止独立后台服务。
 */
static void destroy_page(page_desc_t *page)
{
    if (!page || !page->is_created) return;

    if (page->ops && page->ops->destroy) {
        page->ops->destroy();
    }
    if (page->container) {
        lv_obj_delete(page->container);
    }
    page->container = NULL;
    page->is_created = 0;
    page->is_active = 0;

    printf(LOG_TAG "Page '%s' destroyed (type=%d)\n",
           page->name, page->page_type);
}

/**
 * @brief 退出并隐藏页面；未声明常驻时继续销毁。
 * @param[in,out] page 待停用的注册表元素，可为 NULL。
 */
static void deactivate_page(page_desc_t *page)
{
    hide_page(page);
    if (page && (!page->ops ||
                 !(page->ops->flags & PAGE_FLAG_KEEP_ALIVE))) {
        destroy_page(page);
    }
}

/**
 * @brief 创建隐藏的页面容器，并调用页面创建回调。
 * @param[in,out] page 保存创建结果的页面描述符。
 * @param[in] parent UI 线程中的有效父容器。
 * @return 0 表示已存在或创建成功；-1 表示参数、分配或创建回调失败。
 * @note 创建回调失败时调用可选 destroy，并删除容器、清除页面状态。
 */
static int create_page(page_desc_t *page, lv_obj_t *parent)
{
    int ret;

    if (!page || !parent) return -1;
    if (page->is_created) return 0;

    /* 创建页面容器 */
    page->container = lv_obj_create(parent);
    if (!page->container) {
        return -1;
    }
    lv_obj_set_size(page->container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page->container, 0, 0);
    lv_obj_set_style_pad_all(page->container, 0, 0);
    lv_obj_set_style_border_width(page->container, 0, 0);
    lv_obj_set_style_radius(page->container, 0, 0);
    lv_obj_set_style_bg_opa(page->container, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(page->container, lv_color_black(), 0);
    lv_obj_remove_flag(page->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);

    /* 调用页面的创建回调 */
    if (page->ops && page->ops->create) {
        ret = page->ops->create(page->container);
        if (ret != 0) {
            if (page->ops->destroy) {
                page->ops->destroy();
            }
            lv_obj_delete(page->container);
            page->container = NULL;
            page->is_created = 0;
            page->is_active = 0;
            return -1;
        }
    }

    page->is_created = 1;
    printf(LOG_TAG "Page '%s' created (type=%d)\n", page->name, page->page_type);
    return 0;
}

//*********************************************************************************//
//                              公共接口                                            //
//*********************************************************************************//

/**
 * @brief 初始化静态页面注册表及导航状态。 完整合同见 d_page_manager.h。
 */
void page_manager_init(void)
{
    memset(s_pages, 0, sizeof(s_pages));
    s_page_count = 0;
    g_current_page = PAGE_TYPE_MAIN;
    s_transition_id = 0;
    s_is_switching = 0U;

    printf(LOG_TAG "Page manager initialized\n");
}

/**
 * @brief 复制页面描述符并登记一个尚未创建的页面。 完整合同见 d_page_manager.h。
 */
int page_manager_register(const page_desc_t *desc)
{
    if (!desc || !desc->name || !desc->ops ||
        desc->page_type < 0 || desc->page_type >= PAGE_MAX_COUNT ||
        s_page_count >= PAGE_MAX_COUNT) {
        printf(LOG_TAG "Register failed: invalid or full\n");
        return -1;
    }

    for (int i = 0; i < s_page_count; i++) {
        if (s_pages[i].page_type == desc->page_type) {
            printf(LOG_TAG "Register failed: duplicate page_type=%d\n", desc->page_type);
            return -1;
        }
    }

    memcpy(&s_pages[s_page_count], desc, sizeof(page_desc_t));
    s_pages[s_page_count].container = NULL;
    s_pages[s_page_count].is_created = 0;
    s_pages[s_page_count].is_active = 0;
    s_page_count++;

    printf(LOG_TAG "Page '%s' registered (type=%d, total=%d)\n",
           desc->name, desc->page_type, s_page_count);
    return 0;
}

/**
 * @brief 查找并按需创建目标页，确认容器可用于切换。
 * @param[in] page_type 目标页面编号。
 * @return 0 表示容器可用；-1 表示页面不存在或创建失败。
 */
static int page_manager_prepare(int page_type)
{
    int create_ret;
    page_desc_t *page = find_page(page_type);

    if (page == NULL) {
        printf(LOG_TAG "Prepare failed: page %d not found\n", page_type);
        return -1;
    }
    if (!page->is_created) {
        create_ret = create_page(page, lv_scr_act());
        if ((create_ret != 0) || !page->is_created ||
            (page->container == NULL)) {
            printf(LOG_TAG "Prepare failed: page %d create error\n", page_type);
            return -1;
        }
    }
    if (page->container == NULL) {
        printf(LOG_TAG "Prepare failed: page %d container null\n", page_type);
        return -1;
    }
    printf(LOG_TAG "Page '%s' prepared (type=%d)\n", page->name, page_type);
    return 0;
}

/**
 * @brief 在重入保护已取得后准备目标页，再退出旧页并进入新页。
 * @param[in] page_type 目标页面编号。
 * @param[in] policy 本次页面过渡策略。
 * @return 0 表示切换成功或目标已激活；-1 表示目标准备失败。
 * @note 创建目标失败时不先销毁当前页；TE 过渡只向平台登记请求，不在此等待 DMA。
 */
static int page_manager_switch_to_impl(int page_type, page_transition_policy_e policy)
{
    page_desc_t *from = find_page(g_current_page);
    page_desc_t *to = find_page(page_type);
    unsigned int transition_id = 0;

    if (to == NULL) {
        printf(LOG_TAG "Switch failed: page %d not found\n", page_type);
        return -1;
    }
    if (page_manager_prepare(page_type) != 0) {
        return -1;
    }
    if (to->container == NULL) {
        printf(LOG_TAG "Switch failed: page %d container null\n", page_type);
        return -1;
    }
    if ((from == to) && to->is_active) {
        printf(LOG_TAG "Switch ignored: page '%s' already active\n", to->name);
        return 0;
    }

    if ((from != NULL) && (from != to) &&
        (policy == PAGE_TRANSITION_TE_FRAME)) {
        transition_id = ++s_transition_id;
        d_page_transition_begin(policy, transition_id);
        printf(LOG_TAG "Transition id=%u from='%s' to='%s' policy=te_frame\n",
               transition_id, from->name, to->name);
    }

    /* 隐藏当前页面 */
    if (from && from != to) {
        deactivate_page(from);
    }

    /* 显示目标页面 */
    show_page(to);

    g_current_page = page_type;

    printf(LOG_TAG "Switched to page '%s'\n", to->name);
    return 0;
}

/**
 * @brief 在 UI 线程同步完成目标页面的生命周期切换。 完整合同见 d_page_manager.h。
 */
int page_manager_switch_to(int page_type, page_transition_policy_e policy)
{
    int result;

    if (s_is_switching) {
        return -1;
    }
    s_is_switching = 1U;
    result = page_manager_switch_to_impl(page_type, policy);
    s_is_switching = 0U;
    return result;
}

/**
 * @brief 请求立即返回主页面。 完整合同见 d_page_manager.h。
 */
void page_manager_back_to_main(void)
{
    page_manager_switch_to(PAGE_TYPE_MAIN, PAGE_TRANSITION_IMMEDIATE);
}

/**
 * @brief 读取当前逻辑页面编号。 完整合同见 d_page_manager.h。
 */
int page_manager_get_current(void)
{
    return g_current_page;
}

/**
 * @brief 借用当前页面容器。 完整合同见 d_page_manager.h。
 */
lv_obj_t *page_manager_get_current_container(void)
{
    page_desc_t *page = find_page(g_current_page);
    return page != NULL ? page->container : NULL;
}

/**
 * @brief 调用当前页面的可选周期回调。 完整合同见 d_page_manager.h。
 */
void page_manager_update(uint32_t elapsed_ms)
{
    page_desc_t *current = find_page(g_current_page);
    if (current && current->ops && current->ops->update) {
        current->ops->update(elapsed_ms);
    }
}

/**
 * @brief 按页面标志处理通用返回手势或转交页面手势回调。 完整合同见 d_page_manager.h。
 */
void page_manager_handle_gesture(int gesture_type)
{
    page_desc_t *current = find_page(g_current_page);

    if (current && current->ops &&
        (current->ops->flags & PAGE_FLAG_NO_GESTURE)) {
        printf(LOG_TAG "Gesture suppressed page='%s' type=%d flags=0x%x\n",
               current->name, gesture_type, current->ops->flags);
        return;
    }
    if (current && current->ops) {
        printf(LOG_TAG "Gesture accepted page='%s' type=%d flags=0x%x\n",
               current->name, gesture_type, current->ops->flags);
    }

    /* 上滑手势：从子页面返回主页面 */
    if (gesture_type == 1 && g_current_page != PAGE_TYPE_MAIN) {
        printf(LOG_TAG "Swipe up detected, back to main\n");
        page_manager_back_to_main();
        return;
    }

    /* 传递给当前页面处理 */
    if (current && current->ops && current->ops->gesture) {
        current->ops->gesture(gesture_type);
    }
}

#endif /* D_LVGL_ENABLE */
