/** @file music_service.h @brief 串行本地播放器公开合同，快照不携带 SDK 指针。 */
#ifndef MUSIC_SERVICE_H
#define MUSIC_SERVICE_H
#include "framework/d_error.h"
#include <stdint.h>
#define MUSIC_PAGE_CAPACITY 10U
#define MUSIC_TITLE_CAPACITY 96U
#define MUSIC_INDEX_NONE 0xffffU
#define MUSIC_FORMAT_MP3 1U
#define MUSIC_FORMAT_WAV 2U
#define MUSIC_HOLD_VOICE 1U
#define MUSIC_HOLD_ALERT 2U
#define MUSIC_HOLD_MAINTENANCE 4U
#define MUSIC_HOLD_STARTUP 8U
#define MUSIC_HOLD_VOLUME 16U
typedef enum { MUSIC_ABSENT, MUSIC_SCANNING, MUSIC_READY, MUSIC_PLAYING,
    MUSIC_PAUSED, MUSIC_STOPPING, MUSIC_ERROR } music_state_e;
typedef enum { MUSIC_SELECT, MUSIC_TOGGLE, MUSIC_NEXT, MUSIC_PREVIOUS, MUSIC_RESCAN } music_command_e;
typedef struct { uint16_t index; char title[MUSIC_TITLE_CAPACITY]; } music_item_t;
typedef struct {
    uint32_t revision, media_generation, request_id, completed_id;
    uint32_t page_request_id, page_completed_id, position_seconds, duration_seconds;
    uint16_t count, discovered_count, current_index, page_first, page_count;
    music_state_e state;
    d_err_t error, command_result, page_result, resume_error;
    uint8_t ready, online, mounted, owned, busy, held, can_resume, limited;
    music_item_t current;
} music_snapshot_t;
typedef struct {
    uint32_t generation, request_id;
    uint16_t first, count;
    music_item_t items[MUSIC_PAGE_CAPACITY];
} music_page_t;
typedef struct {
    uint16_t maximum_tracks;
    uint8_t repeat_all;
    uint8_t formats;
    /** 工作任务在服务锁外调用这些回调；permit 不得等待。 */
    d_err_t (*permit)(void);
    d_err_t (*prepare)(void);
    void (*released)(void);
} music_config_t;
/**
 * @brief 在初始化前绑定本地音乐策略，不创建任务或操作 SDK。
 * @param[in] config 复制的策略描述；回调须在服务运行期间有效。
 * @return 配置有效返回 D_OK；无效配置返回 D_ERR_INVALID_ARG；已尝试初始化返回 D_ERR_BUSY。
 */
d_err_t music_service_configure(const music_config_t *config);
/**
 * @brief 创建持久本地音乐工作任务，扫描不自动启动播放。
 * @return 成功返回 D_OK；未配置或此前初始化失败返回 D_ERR_NOT_READY；创建失败返回 D_ERR_IO。
 * @note 创建只尝试一次，失败后不循环重建任务。
 */
d_err_t music_service_init(void);
/**
 * @brief 受理当前媒体代次的一条播放命令。
 * @param[in] command 选曲、播放切换、上下曲或重新扫描命令。
 * @param[in] generation 快照中的当前媒体代次。
 * @param[in] index 选曲命令使用的零起始索引，其他命令忽略。
 * @param[out] request_id 非空请求编号输出；仅受理成功写入。
 * @return 受理返回 D_OK；无效命令或选曲索引返回 D_ERR_INVALID_ARG；媒体不匹配或未就绪返回 D_ERR_NOT_READY；占用或在途命令返回 D_ERR_BUSY。
 * @note completed_id 与 command_result 才证明执行终态。
 */
d_err_t music_service_request(music_command_e command, uint32_t generation,
                             uint16_t index, uint32_t *request_id);
/**
 * @brief 提交曲目分页值请求，新请求可覆盖旧待处理请求。
 * @param[in] generation 当前媒体代次。
 * @param[in] first 第一页项的零起始曲目索引。
 * @param[out] request_id 非空编号输出，仅受理成功时写入。
 * @return 受理返回 D_OK；空输出返回 D_ERR_INVALID_ARG；代次、索引或就绪条件不符返回 D_ERR_NOT_READY。
 * @note 分页完成通过 page_completed_id 和 page_result 确认。
 */
d_err_t music_service_request_page(uint32_t generation, uint16_t first, uint32_t *request_id);
/**
 * @brief 在锁内复制不含 SDK 指针的音乐状态。
 * @param[out] snapshot 接收状态；为空时不处理。
 */
void music_service_get_snapshot(music_snapshot_t *snapshot);
/**
 * @brief 在锁内复制当前已发布的分页值。
 * @param[out] page 接收页面；为空时不处理。
 */
void music_service_get_page(music_page_t *page);
/**
 * @brief 以单个原因位和非零占用票据禁止播放，并请求停止。
 * @param[in] reason MUSIC_HOLD_* 中的一个原因位。
 * @param[in] ticket 该原因单调更新的非零占用票据。
 * @return 受理或同一活动票据重复申请返回 D_OK；参数无效返回 D_ERR_INVALID_ARG；过期票据返回 D_ERR_NOT_FOUND。
 * @note 受理不代表停止完成，须继续 hold_poll；旧申请不能替换新持有者。
 */
d_err_t music_service_hold_begin(uint8_t reason, uint32_t ticket);
/**
 * @brief 非阻塞检查工作任务是否已完成停止并释放解码资源。
 * @param[out] ready 非空完成确认输出；无忙碌、无资源且停止意图结算后为 1。
 * @return 正常查询返回 D_OK；空输出返回 D_ERR_INVALID_ARG；仍持资源且出错时返回保存的错误。
 */
d_err_t music_service_hold_poll(uint8_t *ready);
/**
 * @brief 解除匹配原因与票据的占用。
 * @param[in] reason 单个占用原因位。
 * @param[in] ticket 申请时返回并由调用者维护的非零票据。
 * @return 已解除或本来已释放返回 D_OK；旧票据返回 D_ERR_NOT_FOUND；尚未停止返回 D_ERR_BUSY 或保留的资源错误。
 * @note 只有真实停止完成才允许解除；不会自动恢复播放。
 */
d_err_t music_service_hold_release(uint8_t reason, uint32_t ticket);
/**
 * @brief 由音量工作任务提交音量，并等待音乐任务串行执行。
 * @param[in] percent 0～100 的目标音量。
 * @param[in] timeout_ms 等待完成确认的毫秒期限。
 * @return 成功返回 D_OK；参数、就绪、忙、SDK 或超时错误返回相应值。
 * @note 不可由音乐工作任务自身调用；超时不撤回已经进入 SDK 的写入，未完成槽继续保留。
 */
d_err_t music_service_apply_volume(uint8_t percent, uint32_t timeout_ms);
#endif
