/**
 * @file    d_display.h
 * @brief   当前屏幕 Profile 提供的显示接口。
 */

#ifndef D_DISPLAY_H
#define D_DISPLAY_H

#include "framework/d_error.h"
#include <stdint.h>

/**
 * @brief 启动所选显示配置的初始化流程。
 * @return GC9307 返回面板与背光准备结果或等待超时；EMI 的 D_OK 只表示初始化任务已创建；任务/基础设施创建失败为 D_ERR_IO。
 * @note 由启动装配串行调用，不是运行期重置。GC9307 同步等待有界，超时不会终止后台任务；EMI 不在此等待面板或触摸完成。
 * @note 显示成功不等于触摸必定就绪：GC9307 保留显示可用而触摸失败的降级路径。
 */
d_err_t d_display_init(void);
/**
 * @brief 读取所选适配已保存的面板宽度。
 * @return 初始化前通常为 0；初始化后为适配报告的宽度，不随运行期产品翻转自动交换。
 */
uint16_t d_display_get_width(void);
/**
 * @brief 读取所选适配已保存的面板高度。
 * @return 初始化前通常为 0；初始化后为适配报告的高度，不代替 LVGL 当前逻辑尺寸。
 */
uint16_t d_display_get_height(void);
/**
 * @brief 同步应用背光亮度请求，不保存用户偏好。
 * @param[in] percent 亮度百分比；超过 100 限为 100，非零低于最小可见值时提升至 5。
 * @return D_OK 为当前配置的应用路径成功；息屏时非零请求为 D_ERR_BUSY；PWM 失败返回底层错误。
 * @note 调用方串行管理屏幕电源状态；非零应用前须先合法唤醒或 clear_off。GC9307 在 PWM 成功后更新连续亮度，关闭失败可降级 GPIO；EMI 仍可能先更新请求缓存并在 PWM 失败后降级开关。
 */
d_err_t d_display_set_brightness(uint8_t percent);
/**
 * @brief 读取当前配置保存的亮度百分比。
 * @return GC9307 返回已接纳亮度缓存；EMI 返回最近请求缓存，不能单凭此值判断 PWM 写入成功。
 * @note 初始化前也可能返回配置默认值，非零不证明面板或背光已就绪。
 */
uint8_t d_display_get_brightness(void);
/**
 * @brief 请求关闭背光或恢复最近的非零亮度。
 * @param[in] on 非零表示恢复，零表示关闭。
 * @return D_OK 为当前路径成功；息屏状态未清除就请求开启时为 D_ERR_BUSY；其他错误沿亮度应用返回。
 * @note 只控制背光，不隐式清除屏幕电源状态，也不持久化亮度。
 */
d_err_t d_display_set_backlight(uint8_t on);

typedef struct {
    uint32_t request_id;    /**< 当前或最近一次受理的请求编号。 */
    uint32_t completed_id;  /**< 已结束事务的完成编号，不等同于最近受理编号。 */
    uint32_t generation;    /**< 能力初始化后非零的方向代次；未支持时为零，用于拒绝旧完成。 */
    uint16_t actual_degrees; /**< 已由完整帧确认的产品相对方向。 */
    uint16_t target_degrees; /**< 最近受理的目标方向，失败时可与实际值不同。 */
    d_err_t result;         /**< 本事务结果；回滚完成仍保留原失败。 */
    uint8_t is_busy;        /**< 事务或资源确认仍在途，故障时可能持续保留。 */
    uint8_t is_available;   /**< 当前配置是否具备已初始化的方向能力。 */
} d_display_rotation_snapshot_t;

/**
 * @brief 非阻塞受理产品相对方向的切换。
 * @param[in] degrees 只接受相对正常方向的 0 或 180。
 * @param[out] request_id 可为 NULL；先清零，受理时取得请求编号，无需变化时可能取得原完成编号。
 * @return D_OK 为已受理或无需变化；非法角度为 D_ERR_INVALID_ARG；显示不可用为 D_ERR_NOT_READY；过渡或方向事务忙为 D_ERR_BUSY。
 * @note 只在 UI 执行者调用，不写偏好。EMI 当前不支持，返回 NOT_READY；完成须看快照的 completed_id、is_busy 和 result，不能只看受理结果。
 */
d_err_t d_display_request_rotation(uint16_t degrees, uint32_t *request_id);
/**
 * @brief 复制实际方向、目标、代次和完成事实的值类型快照。
 * @param[out] snapshot 接收结构体；NULL 时不处理。
 * @note GC9307 在短临界区内读取，可供其他任务查询；未提供能力的配置返回不可用快照，不触发 LVGL 或存储操作。
 */
void d_display_get_rotation_snapshot(d_display_rotation_snapshot_t *snapshot);
/**
 * @brief 在公共 UI 周期接纳显示完成并推进方向、输入复位及完整重绘。
 * @note 仅 UI 执行者调用；必要时沿原显示等待接口确认旧作业，不在刷新工作任务调用 LVGL 或保存偏好。
 * @note 完成不确定时保留故障和忙状态并关闭新刷新，不把超时当作资源已释放。
 */
void d_display_process_rotation(void);
/**
 * @brief 在首次显示提交前设置启动方向。
 * @param[in] degrees 产品相对方向，0 或 180。
 * @return D_OK 为方向已装配；非法角度、显示不可用或已有提交/在途事务时返回相应错误；EMI 不支持。
 * @note 仅 UI 执行者在首帧前调用，不显示新帧、不写偏好。
 */
d_err_t d_display_set_initial_rotation(uint16_t degrees);

#endif /* D_DISPLAY_H */
