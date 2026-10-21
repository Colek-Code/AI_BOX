/**
 * @file camera_preview.h
 * @brief Nonblocking camera requests; completion is independent of errors.
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
 * @return 实际初始化结果；失败结果保留到重启，不循环创建线程或监督对象。
 * 读取 is_init_complete 判断初始化是否完成，不能用错误码充当完成标志。
 */
d_err_t camera_preview_init(void);
/**
 * @brief 非阻塞提交预览请求；同一有效请求重复调用返回原会话号。
 * @param session 非空输出；仅 D_OK 时写入。旧清理完成前不接受新会话。
 * @return D_OK 为已接受，不代表已经出图；BUSY 为清理/关机禁止，IO 为需重启的FAULT。
 * 未初始化返回 NOT_READY；实际阶段、错误及操作可用性通过快照读取。
 */
d_err_t camera_preview_request_start(uint32_t *session);
/**
 * @brief 非阻塞撤销指定会话；过期会话号及重复停止不会取消后续新会话。
 * @param session 开始接口返回的会话号。
 * @param reason 首次停止的业务原因；重复请求不延长停止期限。
 * 返回只表示请求处理完毕，资源释放仍由后台完成。
 */
void camera_preview_request_stop(uint32_t session, camera_preview_stop_e reason);
/**
 * @brief 读取有界元数据及当前时刻的超时诊断，不调用SDK或等待。
 * @param snapshot 输出快照；NULL无操作。完成事实与错误/超时分别判断。
 */
void camera_preview_get_snapshot(camera_preview_snapshot_t *snapshot);
/**
 * @brief 由LVGL线程的单个显示消费者取走最新帧，在临界区外复制。
 * @param session 当前页面持有的会话号。
 * @param destination 页面自有固定RGB565缓冲，不向后台交付LVGL对象。
 * @param capacity 至少 CAMERA_PREVIEW_RGB_BYTES 字节。
 * @param sequence 输入上次成功帧号，首次置0；仅D_OK时更新为本次帧号。
 * @return D_OK复制成功；NOT_READY表示无新帧、过期或复制途中取消；INVALID_ARG为参数/容量错误。
 * 失败时目标缓冲也可能已写入，不保证原内容；调用者必须忽略失败结果，并在绘制前
 * 再确认会话仍有效。函数不等待SDK，也不向多个消费者广播每一帧。
 */
d_err_t camera_preview_copy_latest(uint32_t session, void *destination,
                                   uint32_t capacity, uint32_t *sequence);
/**
 * @brief 后台等待服务整体停止；不自动提交停止，也不限定某个历史会话号。
 * @param timeout_ms 最多等待时间，0只检查一次。禁止在LVGL回调中调用。
 * @return D_OK为清理完成，TIMEOUT仍未完成，IO为FAULT。超时不回收在途对象。
 * 需要防止等待期间产生新会话时，先使用shutdown_begin设置启动禁止。
 */
d_err_t camera_preview_wait_stopped(uint32_t timeout_ms);
/** @brief 关机编排入口：禁止新启动并撤销当前预览；本函数不等待。 */
void camera_preview_shutdown_begin(void);
/** @brief 关机取消时仅解除禁止；不重启预览，也不清除STOPPING/FAULT事实。 */
void camera_preview_shutdown_abort(void);

#endif
