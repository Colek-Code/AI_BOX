/** @file product_ui_appearance.h @brief Product UI 外观事务接口；快照区分请求受理、显示完成与持久化确认。 */
#ifndef PRODUCT_UI_APPEARANCE_H
#define PRODUCT_UI_APPEARANCE_H

#include "config/app_config.h"
#include "framework/d_error.h"
#include <stdint.h>

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#define PRODUCT_UI_APPEARANCE_VM_ID       26U
#define PRODUCT_UI_APPEARANCE_RECORD_SIZE 8U
#define PRODUCT_UI_APPEARANCE_TIMEOUT_MS  2000U
#define PRODUCT_UI_WALLPAPER_COUNT        3U

typedef struct {
    uint32_t revision; /**< 状态变化代次，零不作为有效发布代次。 */
    uint32_t request_id; /**< 最近受理的非零请求编号。 */
    uint32_t completed_id; /**< 最近完成确认的请求编号；与请求编号匹配后再读取 result。 */
    d_err_t result; /**< 最近事务结果；受理时为 D_OK，不能单独据此判断完成。 */
    d_err_t init_error; /**< 服务初始化结果。 */
    d_err_t record_error; /**< 启动记录读取校验结果或最近成功保存状态。 */
    uint16_t rotation_degrees; /**< 已由显示端确认应用的逻辑方向。 */
    uint16_t persisted_rotation_degrees; /**< 最近确认存储的方向，可能滞后于显示方向。 */
    uint16_t target_rotation_degrees; /**< 当前事务的目标方向。 */
    uint8_t has_rotation_override; /**< 当前方向是否来自用户覆盖。 */
    uint8_t target_rotation_override; /**< 本次记录是否保存用户方向覆盖。 */
    uint8_t is_rotation_waiting; /**< 已占用队列且尚待显示完成确认。 */
    uint8_t is_record_protected; /**< 遇到较新未知记录版本，禁止覆盖。 */
    uint8_t wallpaper_id; /**< 最近确认保存的壁纸编号。 */
    uint8_t reduce_motion; /**< 最近确认保存的减少动态效果开关。 */
    uint8_t target_wallpaper_id; /**< 本次事务的壁纸目标。 */
    uint8_t target_reduce_motion; /**< 本次事务的动态效果目标。 */
    uint8_t is_initialized; /**< 首次初始化已经结束，包括失败。 */
    uint8_t is_initializing; /**< 快照中派生的初始化在途标志。 */
    uint8_t is_service_ready; /**< 后台任务已启动且尚未退出。 */
    uint8_t has_confirmed_record; /**< 存在已校验的启动记录或已读回确认的保存记录。 */
    uint8_t is_pending; /**< 单槽中存在已受理、尚未交给存储的工作。 */
    uint8_t is_in_progress; /**< worker 正在执行存储调用。 */
    uint8_t is_quiescing; /**< 禁止受理新请求，已受理工作仍继续。 */
    uint8_t is_timed_out; /**< 待处理或在途时长超过观察阈值；不撤销请求。 */
} product_ui_appearance_snapshot_t;

/**
 * @brief 读取已有外观记录并启动后台保存任务；启动过程不写 VM。
 * @return 服务就绪返回 D_OK；初始化在途返回 D_ERR_BUSY；任务未就绪返回初始化错误或 D_ERR_NOT_READY。
 * @note 未知较新记录版本受到保护，后续写入请求返回 D_ERR_NOT_READY；重复调用不会重新初始化。
 */
d_err_t product_ui_appearance_init(void);
/**
 * @brief 复制受临界区保护的外观状态，并按受理时间计算超时观察值。
 * @param[out] snapshot 接收状态副本；为 NULL 时不操作。
 * @note 超时仅为观察结果，不撤销后台写入，不等于完成确认。
 */
void product_ui_appearance_get_snapshot(product_ui_appearance_snapshot_t *snapshot);
/**
 * @brief 受理壁纸偏好保存请求。
 * @param[in] id 壁纸编号，须小于 PRODUCT_UI_WALLPAPER_COUNT。
 * @param[out] request_id 可为 NULL；失败置零，受理返回请求编号，已确认值返回既有完成编号。
 * @return D_OK 表示受理或与已确认值一致；非法编号、未就绪、保护记录或已有事务分别返回相应错误。
 * @note 调用方按快照的 completed_id 与 result 观察持久化完成；D_OK 不保证本次已写入。
 */
d_err_t product_ui_appearance_request_wallpaper(uint8_t id, uint32_t *request_id);
/**
 * @brief 受理减少动态效果偏好的保存请求。
 * @param[in] enabled 0 关闭、1 开启；其他值无效。
 * @param[out] request_id 可为 NULL；失败置零，成功返回请求或既有完成编号。
 * @return D_OK 表示受理或与已确认值一致；参数越界、未就绪或事务占用返回相应错误。
 * @note 后台完整写入并读回后才发布完成确认。
 */
d_err_t product_ui_appearance_request_reduce_motion(uint8_t enabled, uint32_t *request_id);
/**
 * @brief 预留外观事务队列，等待匹配的显示翻转结果后再决定保存。
 * @param[in] degrees 目标逻辑方向，仅支持 0 或 180 度。
 * @param[out] request_id 可为 NULL；受理时返回非零请求编号，失败置零。
 * @return D_OK 仅表示已占用事务；非法角度、服务不可用或事务占用返回相应错误。
 * @note 必须调用 rotation_applied 提交匹配请求的显示完成结果；等待期间 worker 不写入，维护等待也不能提前视为空闲。
 */
d_err_t product_ui_appearance_request_rotation(uint16_t degrees, uint32_t *request_id);
/**
 * @brief 接收显示翻转完成确认，成功释放后台保存，失败结束本次事务。
 * @param[in] request_id 待确认的外观请求编号；零、过期或非等待状态均忽略。
 * @param[in] result 显示应用结果；D_OK 才推进存储。
 * @note 显示成功但保存失败时保留已经应用的方向，快照分别报告实际方向与已持久化方向。
 */
void product_ui_appearance_rotation_applied(uint32_t request_id, d_err_t result);
/**
 * @brief 关闭新外观请求入口，保留已受理事务继续完成。
 */
void product_ui_appearance_quiesce_begin(void);
/**
 * @brief 未进入服务停止时解除维护静默，重新允许外观请求。
 */
void product_ui_appearance_quiesce_cancel(void);
/**
 * @brief 阻塞等待初始化及已受理外观事务退出。
 * @param[in] timeout_ms 最多等待的单调时钟毫秒数。
 * @return 无初始化、待处理或在途事务时返回 D_OK；超时返回 D_ERR_TIMEOUT。
 * @note 每轮延迟一个 OS tick；超时保留事务及其静态上下文，调用方不能据此销毁资源。
 */
d_err_t product_ui_appearance_wait_idle(uint32_t timeout_ms);
/**
 * @brief 先禁止新请求并排空事务，再要求后台任务退出。
 * @param[in] timeout_ms 排空及停止观察共用的总等待上限，单位毫秒。
 * @return 任务已停止或未启动返回 D_OK；排空或退出等待超时返回 D_ERR_TIMEOUT。
 * @note 超时后仍保留静态状态和同步对象，迟到的存储调用仍可完成；本函数不删除这些对象。
 */
d_err_t product_ui_appearance_service_stop(uint32_t timeout_ms);
#endif
#endif
