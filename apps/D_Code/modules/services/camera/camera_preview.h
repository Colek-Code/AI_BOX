/**
 * @file camera_preview.h
 * @brief 非阻塞相机会话接口，分别确认受理、错误与清理完成。
 * @author D_Code
 * @date 2026-09-11
 */
#ifndef __CAMERA_PREVIEW_H__
#define __CAMERA_PREVIEW_H__

#include "framework/d_error.h"
#include <stdint.h>

#define CAMERA_PREVIEW_WIDTH             208U
#define CAMERA_PREVIEW_HEIGHT            156U
#define CAMERA_PREVIEW_RGB_BYTES         (CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * 2U)

typedef enum {
    CAMERA_PREVIEW_UNAVAILABLE = 0,
    CAMERA_PREVIEW_IDLE,
    CAMERA_PREVIEW_STARTING,
    CAMERA_PREVIEW_RUNNING,
    CAMERA_PREVIEW_STOPPING,
    CAMERA_PREVIEW_ERROR,
    CAMERA_PREVIEW_FAULT,
} camera_preview_phase_e;

typedef enum {
    CAMERA_PREVIEW_ERROR_NONE = 0,
    CAMERA_PREVIEW_ERROR_INIT,
    CAMERA_PREVIEW_ERROR_NO_SENSOR,
    CAMERA_PREVIEW_ERROR_SENSOR_INFO,
    CAMERA_PREVIEW_ERROR_FORMAT,
    CAMERA_PREVIEW_ERROR_OVERLAY,
    CAMERA_PREVIEW_ERROR_FRAME,
    CAMERA_PREVIEW_ERROR_TRANSPORT,
    CAMERA_PREVIEW_ERROR_START_TIMEOUT,
    CAMERA_PREVIEW_ERROR_FRAME_TIMEOUT,
    CAMERA_PREVIEW_ERROR_CLEANUP,
} camera_preview_error_e;

typedef enum {
    CAMERA_PREVIEW_STOP_PAGE = 0,
    CAMERA_PREVIEW_STOP_SCREEN,
    CAMERA_PREVIEW_STOP_SHUTDOWN,
    CAMERA_PREVIEW_STOP_ERROR,
} camera_preview_stop_e;

typedef struct {
    uint32_t session;
    uint32_t revision;
    uint32_t shutdown_epoch;
    uint32_t captured_frames;
    uint32_t published_frames;
    uint32_t copied_frames;
    uint32_t dropped_frames;
    uint32_t stop_elapsed_ms;
    camera_preview_phase_e phase;
    camera_preview_error_e error;
    camera_preview_stop_e stop_reason;
    d_err_t init_result;
    int sdk_result;
    uint8_t is_initialized;
    uint8_t is_requested;
    uint8_t is_sdk_busy;
    uint8_t is_cleanup_complete;
    uint8_t is_stop_overdue;
    uint8_t is_shutdown_blocked;
    uint8_t can_start;
    uint8_t is_init_complete;
} camera_preview_snapshot_t;

/**
 * @brief 启动阶段串行调用一次，创建后台对象但不打开镜头。
 * @return 实际初始化结果；失败保留到重启，不循环创建后台对象。
 * @note 以 is_init_complete 判断是否完成，错误码不能充当完成标志。
 */
d_err_t camera_preview_init(void);
/**
 * @brief 非阻塞受理预览请求，重复有效请求复用原会话。
 * @param[out] session 非空输出，仅 D_OK 时写入非零会话编号。
 * @return D_OK 表示受理；空输出、未初始化、清理或关机阻塞、FAULT 返回对应错误。
 * @note 受理不等于出图；阶段和错误通过快照读取。
 */
d_err_t camera_preview_request_start(uint32_t *session);
/**
 * @brief 非阻塞撤销指定会话。
 * @param[in] session 开始时返回的会话编号，旧会话不会撤销新会话。
 * @param[in] reason 首次停止的业务原因，重复请求不延长期限。
 * @note 函数返回不表示资源已释放，清理由后台完成。
 */
void camera_preview_request_stop(uint32_t session, camera_preview_stop_e reason);
/**
 * @brief 复制有界元数据及当前时刻的期限诊断。
 * @param[out] snapshot 接收快照；为空时不处理。
 * @note 不调用 SDK 或等待；清理完成、错误与超时须分别判断。
 */
void camera_preview_get_snapshot(camera_preview_snapshot_t *snapshot);
/**
 * @brief 由 LVGL 单个显示消费者领取最新帧，在临界区外复制。
 * @param[in] session 当前页面持有的会话编号。
 * @param[out] destination 页面自有的 RGB565 输出缓冲。
 * @param[in] capacity 缓冲字节容量，至少 CAMERA_PREVIEW_RGB_BYTES。
 * @param[in,out] sequence 上次成功帧号，首次置 0；仅成功时更新。
 * @return D_OK 表示复制成功；无新帧、会话过期或复制中取消返回 D_ERR_NOT_READY；参数或容量无效返回 D_ERR_INVALID_ARG。
 * @note 失败时目标可能已写入，调用者必须忽略结果并在绘制前确认会话仍有效；不等待 SDK，不向多个消费者广播每帧。
 */
d_err_t camera_preview_copy_latest(uint32_t session, void *destination,
                                   uint32_t capacity, uint32_t *sequence);
/**
 * @brief 在后台限时等待服务整体清理完成，不自动提交停止。
 * @param[in] timeout_ms 最多等待毫秒数，0 仅检查一次。
 * @return 清理完成返回 D_OK；FAULT 返回 D_ERR_IO；超时返回 D_ERR_TIMEOUT。
 * @note 禁止在 LVGL 回调调用；超时不回收在途对象。防止等待时产生新会话需先调用 shutdown_begin。
 */
d_err_t camera_preview_wait_stopped(uint32_t timeout_ms);
/**
 * @brief 禁止新启动并撤销当前预览，不等待后台收尾。
 */
void camera_preview_shutdown_begin(void);
/**
 * @brief 取消关机禁止，不重新启动预览或清除 STOPPING、FAULT。
 */
void camera_preview_shutdown_abort(void);

#endif
