/**
 * @file d_touch_hyn.h
 * @brief 声明 GC9307 配置的 HYN/CST8xxT 触摸能力及值类型质量快照。
 */

#ifndef D_TOUCH_HYN_H
#define D_TOUCH_HYN_H

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && defined(D_TOUCH_HYN_DRIVER_ENABLE)

#include "framework/d_error.h"
#include "platform/screen/lcd/d_lcd_compat.h"
#include <stdint.h>

typedef struct {
    u8 id; /**< 驱动接受的主接触编号，当前只选 0 或 1。 */
    u8 z;  /**< 驱动构造的兼容强度字段，不是实际压感测量。 */
    u16 x; /**< 安装变换后的原生横坐标，不含产品运行方向。 */
    u16 y; /**< 安装变换后的原生纵坐标。 */
} d_touch_hyn_point_t;

/**
 * @brief 不执行 IO 的触摸质量快照。
 * @note 每次连续读取失败进入取消期时推进一次 cancel_epoch，即使原来空闲也推进；
 * 消费者按变化判断而非比较大小，执行取消，不能伪造正常 UP。
 * needs_release 会抑制恢复后的持续接触，直到读取成功且无认可主接触的帧到来。
 */
typedef struct {
    uint16_t x; /**< 最近有效原生横坐标；取消时仍保留。 */
    uint16_t y; /**< 最近有效原生纵坐标。 */
    uint8_t pressed; /**< 当前有效接触电平，不把未知丢失解释为正常松手。 */
    uint8_t needs_release; /**< 取消恢复门禁，须等待合法中性帧。 */
    uint32_t cancel_epoch; /**< 自然回绕的取消代次，只比较是否改变。 */
} d_touch_hyn_snapshot_t;

/**
 * @brief 按板级参数准备 HYN 总线、中断及常驻读取任务。
 * @param[in] pdata 有效板级描述符，包含触摸中断及复位引脚。
 * @return D_OK 为已就绪或任务创建成功；参数无效为 D_ERR_INVALID_ARG；没有有效设备应答为 D_ERR_NOT_FOUND；其他准备失败为 D_ERR_IO。
 * @note 启动装配串行调用，不提供运行期重复启动或停止接口。中断注册失败时保留轮询降级；成功不代表每次后续读取都有效。
 */
d_err_t d_touch_hyn_init(const struct lcd_platform_data *pdata);
/**
 * @brief 读取兼容的原生坐标及 0/1 接触电平。
 * @param[out] x 可为 NULL，接收原生横坐标。
 * @param[out] y 可为 NULL，接收原生纵坐标。
 * @param[out] status 可为 NULL，非零表示有效按下。
 * @note 仅复制快照，不执行 IIC。动作消费者应使用含 cancel_epoch 的完整快照，不能把这里的零电平直接当作正常 UP。原生域为 240×320，LVGL 自行应用显示方向。
 */
void d_touch_hyn_get_state(s16 *x, s16 *y, unsigned char *status);
/**
 * @brief 在短临界区内复制触摸坐标、电平及取消质量事实。
 * @param[out] snapshot 调用方持有的快照；NULL 时不操作。
 * @note 不访问设备或调用 LVGL。消费者用取消代次变化复位自身输入序列，并等待 needs_release 清除，不能由 getter 触发恢复。
 */
void d_touch_hyn_get_snapshot(d_touch_hyn_snapshot_t *snapshot);

#endif /* D_LCD_ENABLE && D_TOUCH_HYN_DRIVER_ENABLE */
#endif /* D_TOUCH_HYN_H */
