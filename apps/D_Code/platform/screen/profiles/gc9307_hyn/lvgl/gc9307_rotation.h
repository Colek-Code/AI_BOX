/**
 * @file gc9307_rotation.h
 * @brief 定义 GC9307 私有方向事务与坐标变换，不包含 LVGL、IO 或偏好保存策略。
 */
#ifndef GC9307_ROTATION_H
#define GC9307_ROTATION_H
#include "platform/screen/d_display.h"

typedef enum {
    GC9307_ROTATION_IDLE = 0,
    GC9307_ROTATION_DRAIN,
    GC9307_ROTATION_FRAME,
    GC9307_ROTATION_ROLLBACK,
    GC9307_ROTATION_FAULT
} gc9307_rotation_phase_e;

typedef struct {
    uint32_t generation; /**< 本作业冻结的方向代次。 */
    uint32_t request_id; /**< 参与方向事务时的请求编号，普通作业可为零。 */
    uint16_t angle; /**< 有效安装角为 90°/270°，不是产品相对 0°/180°。 */
} gc9307_frame_orientation_t;

typedef struct {
    d_display_rotation_snapshot_t snapshot; /**< 对外公布的值类型事实。 */
    gc9307_rotation_phase_e phase; /**< 内部排空、目标帧、回滚或故障阶段。 */
    uint16_t render_degrees; /**< 正在渲染的产品相对方向，可能尚未成为实际可见方向。 */
} gc9307_rotation_t;

/**
 * @brief 清空私有方向事务，并建立启动方向与非零初始代次。
 * @param[out] state 显示配置持有的有效状态对象。
 * @param[in] degrees 调用方已校验的产品相对方向，0 或 180。
 * @note 由显示配置串行调用；不操作 LVGL、硬件或偏好存储，调用方负责指针及初始方向有效。
 */
void gc9307_rotation_init(gc9307_rotation_t *state, uint16_t degrees);
/**
 * @brief 在空闲事务中登记相对方向切换请求。
 * @param[in,out] state 显示配置持有并串行保护的有效状态对象。
 * @param[in] degrees 目标相对方向，只接受 0 或 180。
 * @param[out] id 可为 NULL；先置零，受理时取得新请求编号，无需变化时取得已有完成编号。
 * @return D_OK 表示受理或方向无需变化；角度无效为 D_ERR_INVALID_ARG，事务非空闲为 D_ERR_BUSY。
 * @note 受理不等于显示完成；新请求进入 DRAIN，需等旧方向作业实际排空后推进。
 */
d_err_t gc9307_rotation_request(gc9307_rotation_t *state, uint16_t degrees, uint32_t *id);
/**
 * @brief 旧方向作业确认排空后，推进方向代次并等待目标完整帧。
 * @param[in,out] state 有效且串行保护的方向事务。
 * @note 仅在 DRAIN 阶段生效；调用方必须先确认旧显示作业已完成，本纯状态函数不自行等待。
 */
void gc9307_rotation_drained(gc9307_rotation_t *state);
/**
 * @brief 仅接纳当前请求及方向代次的完整帧完成，按结果提交、回滚或保留故障。
 * @param[in,out] state 有效且串行保护的方向事务。
 * @param[in] id 该帧冻结的请求编号。
 * @param[in] generation 该帧冻结的方向代次。
 * @param[in] result D_OK 表示 DMA 已确认；D_ERR_INVALID_ARG 表示 DMA 前合帧失败；其他错误表示完成不确定。
 * @note 旧请求、旧代次或阶段不符时不改状态。DMA 前失败可进入旧方向重绘；其他失败保留 FAULT 和忙状态，不提前复用资源。
 * @note 回滚帧成功会结束忙状态，但保留原 D_ERR_IO 结果；只有目标帧成功才更新 actual_degrees。
 */
void gc9307_rotation_complete(gc9307_rotation_t *state, uint32_t id,
                             uint32_t generation, d_err_t result);

/**
 * @brief 把产品相对方向映射为当前屏幕安装角度。
 * @param[in] degrees 产品相对方向；调用方通常已限定为 0 或 180。
 * @return 180 对应 270°，其他值对应 90°。
 */
static inline uint16_t gc9307_rotation_angle(uint16_t degrees)
{
    return degrees == 180U ? 270U : 90U;
}

/**
 * @brief 将触摸原生坐标变换为 320×240 逻辑坐标，供自定义手势使用。
 * @param[in] angle 当前帧安装角度，90° 或 270°。
 * @param[in] x 240×320 原生域的横坐标。
 * @param[in] y 240×320 原生域的纵坐标。
 * @param[out] out_x 有效的逻辑横坐标输出指针。
 * @param[out] out_y 有效的逻辑纵坐标输出指针。
 * @note 不检查指针或裁剪范围。真实 LVGL 输入由 LVGL 完成旋转，不能再叠加本变换。
 */
static inline void gc9307_native_to_logical(uint16_t angle, int32_t x, int32_t y,
                                            int32_t *out_x, int32_t *out_y)
{
    *out_x = angle == 90U ? 319 - y : y;
    *out_y = angle == 90U ? x : 239 - x;
}

/**
 * @brief 将 320×240 逻辑坐标逆变换为触摸原生坐标。
 * @param[in] angle 当前帧安装角度，90° 或 270°。
 * @param[in] x 逻辑横坐标。
 * @param[in] y 逻辑纵坐标。
 * @param[out] out_x 有效的原生横坐标输出指针。
 * @param[out] out_y 有效的原生纵坐标输出指针。
 * @note 不裁剪范围；调用方负责有效指针和坐标域。
 */
static inline void gc9307_logical_to_native(uint16_t angle, int32_t x, int32_t y,
                                            int32_t *out_x, int32_t *out_y)
{
    *out_x = angle == 90U ? y : 239 - y;
    *out_y = angle == 90U ? 319 - x : x;
}

/**
 * @brief 将逻辑坐标映射到 LCD 传屏扫描坐标。
 * @param[in] angle 该作业冻结的安装角度，90° 或 270°。
 * @param[in] x 逻辑横坐标。
 * @param[in] y 逻辑纵坐标。
 * @param[out] out_x 有效的扫描横坐标输出指针。
 * @param[out] out_y 有效的扫描纵坐标输出指针。
 * @note LCD 线序坐标不同于触摸原生坐标；不能用触摸逆变换替代。函数不检查指针或裁剪范围。
 */
static inline void gc9307_logical_to_physical(uint16_t angle, int32_t x, int32_t y,
                                              int32_t *out_x, int32_t *out_y)
{
    *out_x = angle == 90U ? 239 - y : y;
    *out_y = angle == 90U ? x : 319 - x;
}
#endif
