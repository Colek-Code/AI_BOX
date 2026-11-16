/** @file product_ui_settings_service.h @brief 设置能力、值投影及有限命令；硬件与外观事务由各所有者执行。 */

#ifndef __PRODUCT_UI_SETTINGS_SERVICE_H__
#define __PRODUCT_UI_SETTINGS_SERVICE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include <stdint.h>
#include "modules/applications/product_ui/runtime/preferences/product_ui_appearance.h"

#define PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH (1U << 0)
#define PRODUCT_UI_SETTING_CAP_BRIGHTNESS       (1U << 1)
#define PRODUCT_UI_SETTING_CAP_VOLUME           (1U << 2)
#define PRODUCT_UI_SETTING_CAP_WALLPAPER        (1U << 3)
#define PRODUCT_UI_SETTING_CAP_TIME             (1U << 4)
#define PRODUCT_UI_SETTING_CAP_WIFI             (1U << 5)
#define PRODUCT_UI_SETTING_CAP_BLUETOOTH        (1U << 6)
#define PRODUCT_UI_SETTING_CAP_REDUCE_MOTION    (1U << 7)

/* 连续亮度须保持可见，物理零亮度留给灭屏所有者；
 * 此最低值须与显示适配器的最低可见亮度一致。 */
#define PRODUCT_UI_BRIGHTNESS_MIN_PERCENT       5U
#define PRODUCT_UI_BRIGHTNESS_MAX_PERCENT       100U

typedef enum {
    PRODUCT_UI_VOLUME_READY,
    PRODUCT_UI_VOLUME_APPLYING,
    PRODUCT_UI_VOLUME_ERROR,
    PRODUCT_UI_VOLUME_TIMEOUT,
    PRODUCT_UI_VOLUME_UNAVAILABLE,
    PRODUCT_UI_VOLUME_QUIESCING,
} product_ui_volume_state_e;

typedef struct {
    uint32_t capability_mask;
    uint32_t valid_mask;
    uint32_t revision;
    uint8_t backlight_enabled;
    uint8_t brightness_percent;
    uint8_t volume_percent;
    uint8_t wallpaper_id;
    uint8_t volume_target_percent;
    product_ui_volume_state_e volume_state;
    int32_t volume_error;
    uint32_t volume_request_id;
    uint32_t volume_completed_id;
    product_ui_appearance_snapshot_t appearance;
} product_ui_settings_snapshot_t;

typedef enum {
    PRODUCT_UI_SETTINGS_SET_BACKLIGHT,
    PRODUCT_UI_SETTINGS_SET_BRIGHTNESS,
    PRODUCT_UI_SETTINGS_SET_VOLUME,
    PRODUCT_UI_SETTINGS_SET_WALLPAPER,
    PRODUCT_UI_SETTINGS_SET_REDUCE_MOTION,
} product_ui_settings_command_e;

/**
 * @brief 在串行启动阶段建立互斥量并重置设置投影。
 * @return 成功返回 0，互斥量创建失败返回 -1。
 * @note 重复调用也清空现有状态，不用于运行时并发拆卸。
 */
int product_ui_settings_service_init(void);
/**
 * @brief 选择音量滑条应显示的当前值或目标值。
 * @param[in] snapshot 借用的设置快照。
 * @return 应用中及超时显示目标音量，其余显示确认音量；空输入返回零。
 */
uint8_t product_ui_settings_volume_display(const product_ui_settings_snapshot_t *snapshot);
/**
 * @brief 根据能力、有效位及音量状态判断当前是否允许继续调节。
 * @param[in] snapshot 借用的设置快照，可为空。
 * @return 有有效音量能力且非超时、静默或不可用时返回非零，否则为零。
 */
uint8_t product_ui_settings_volume_can_adjust(const product_ui_settings_snapshot_t *snapshot);
/**
 * @brief 从音量阶段生成静态中文状态文字。
 * @param[in] snapshot 借用的设置快照，可为空。
 * @return 对应状态文字的借用指针；空输入使用不可用说明。
 */
const char *product_ui_settings_volume_status(const product_ui_settings_snapshot_t *snapshot);
/**
 * @brief 根据外观服务就绪、静默及事务占用筛选保存操作。
 * @param[in] snapshot 借用的设置快照，可为空。
 * @return 服务就绪且无静默、待处理或在途事务时返回非零，否则为零。
 * @note 本辅助值不检查受保护记录版本；最终请求仍可因记录保护或其他准入条件失败。
 */
uint8_t product_ui_settings_appearance_can_save(const product_ui_settings_snapshot_t *snapshot);
/**
 * @brief 按外观就绪、维护、超时、事务及保存结果选择状态文字。
 * @param[in] snapshot 借用的设置快照，可为空。
 * @return 静态中文状态文字的借用指针。
 */
const char *product_ui_settings_appearance_status(const product_ui_settings_snapshot_t *snapshot);
/**
 * @brief 刷新真实外观投影并在设置互斥量内复制当前状态。
 * @param[out] snapshot 接收值快照。
 * @return 成功返回 0，未初始化或空输出返回 -1。
 * @note 读取也可能发现外观变化并推进设置及注册表代次；不执行存储 IO。
 */
int product_ui_settings_service_snapshot(product_ui_settings_snapshot_t *snapshot);
/**
 * @brief 验证硬件发布值，重新取外观投影后按实际变化更新设置代次。
 * @param[in] snapshot 调用期间借用的值投影；外观字段不由硬件发布者拥有。
 * @return 发布或内容相同返回 0，未初始化或输入无效返回 -1。
 * @note 互斥量只保护设置值更新；注册表变化通知在锁外执行。
 */
int product_ui_settings_service_publish(
    const product_ui_settings_snapshot_t *snapshot);
/**
 * @brief 校验命令后转交外观事务或通用设置处理器。
 * @param[in] command 背光、亮度、音量、壁纸或减少动态效果命令。
 * @param[in] value 单字节目标；亮度及音量不得大于 100，其余由对应所有者验证。
 * @return 越界返回 -1；其余原样返回所有者的结果，成功含义依命令区分。
 * @note 壁纸、动态效果和音量成功仅表示受理；显示完成及保存确认仍由对应快照提供。
 */
int product_ui_settings_service_command(product_ui_settings_command_e command,
                                        uint8_t value);

#endif
#endif /* __PRODUCT_UI_SETTINGS_SERVICE_H__ */
