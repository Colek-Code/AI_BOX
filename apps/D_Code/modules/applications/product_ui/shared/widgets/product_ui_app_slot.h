/** @file product_ui_app_slot.h @brief 首页图标、标题及完整按压手势的对象所有权。 */
#ifndef __PRODUCT_UI_APP_SLOT_H__
#define __PRODUCT_UI_APP_SLOT_H__

#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "lvgl.h"
#include "modules/applications/product_ui/shared/motion/product_ui_motion.h"

typedef struct {
    const lv_image_dsc_t *image;
    const char *title;
    uint8_t is_enabled;
} product_ui_app_slot_spec_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *icon;
    lv_obj_t *title;
    product_ui_motion_t press_motion;
    uint8_t is_enabled;
    uint8_t is_armed;
} product_ui_app_slot_t;

/**
 * @brief 按比例缩放借用图像，使其居中放入指定区域。
 * @param[in,out] image 已存在的 LVGL 图像对象。
 * @param[in] source 有效且持续存活的静态图像描述，宽高须非零。
 * @param[in] width 区域宽度，须在屏幕宽度内且大于零。
 * @param[in] height 区域高度，须在屏幕高度内且大于零。
 * @return 成功返回 0；参数、尺寸、颜色格式或缩放比例无效返回 -1。
 * @note 只接受 ARGB8888、RGB565、RGB888、XRGB8888；LVGL 借用源描述与像素，不在此复制。
 */
int product_ui_image_fit(lv_obj_t *image, const lv_image_dsc_t *source,
                         int32_t width, int32_t height);
/**
 * @brief 创建首页应用槽及其图像、标题、按压动画和点击处理。
 * @param[in,out] parent 拥有新根对象的父容器。
 * @param[out] slot 调用方提供的槽状态；必须保持有效至根对象删除或解除回调。
 * @param[in] action 必填点击回调，在手势校验后调用。
 * @param[in] user_data 点击回调借用的上下文，由调用方保持有效。
 * @return 成功返回 0；参数或任一对象创建失败返回 -1，子对象失败时删除已建根对象并清空状态。
 */
int product_ui_app_slot_create(lv_obj_t *parent, product_ui_app_slot_t *slot,
                               lv_event_cb_t action, void *user_data);
/**
 * @brief 验证图像后绑定槽内容，重置手势并更新使能和标题。
 * @param[in,out] slot 已经成功创建的应用槽。
 * @param[in] spec 本次借用的配置；图像保持借用，标题由 LVGL 拷贝。
 * @return 成功返回 0；参数或图像适配失败返回 -1。
 * @note 标题为空时隐藏标题并禁止点击；禁用但有标题时图像减淡。
 */
int product_ui_app_slot_bind(product_ui_app_slot_t *slot,
                             const product_ui_app_slot_spec_t *spec);
/**
 * @brief 撤销本轮手势资格并立即清除按压视觉。
 * @param[in,out] slot 应用槽；NULL 或无根对象时忽略。
 */
void product_ui_app_slot_cancel(product_ui_app_slot_t *slot);
/**
 * @brief 解除按压动画并撤销手势资格，不删除应用槽根对象。
 * @param[in,out] slot 调用方持有的槽，可为 NULL。
 */
void product_ui_app_slot_dispose(product_ui_app_slot_t *slot);

#endif
#endif
