/** @file product_ui_alarm_port.h @brief 闹钟完整记录存储、独立调度及共享提示音端口。 */
#ifndef PRODUCT_UI_ALARM_PORT_H
#define PRODUCT_UI_ALARM_PORT_H
#include "framework/d_error.h"
#include <stdint.h>
#define PRODUCT_UI_ALARM_VM_A 29U
#define PRODUCT_UI_ALARM_VM_B 30U

typedef struct {
    uint32_t event_id;
    uint32_t attempt;
    d_err_t error;
    uint8_t ready;
    uint8_t pending;
    uint8_t playing;
    uint8_t owned;
} product_ui_alarm_audio_snapshot_t;

/**
 * @brief 按 A/B 槽读取完整闹钟记录并分类缺失及版本保护。
 * @param[in] slot 0 为 A 槽、1 为 B 槽。
 * @param[out] record 容量至少为 size 的调用方缓冲，失败时内容不可使用。
 * @param[in] size 必须为 PRODUCT_UI_ALARM_RECORD_SIZE。
 * @return 实际读长或规范化错误；NOT_FOUND 仅表示已证实缺失，长度版本不符返回 ALARM_ERR_VERSION。
 * @note 启动与单一保存所有者串行访问端口，不能把负读取值概括为允许覆盖默认记录。
 */
int product_ui_alarm_storage_read(uint8_t slot, uint8_t *record, uint16_t size);
/**
 * @brief 写入一个完整闹钟 A/B 记录。
 * @param[in] slot 0 为 A 槽、1 为 B 槽。
 * @param[in] record 调用期间借用的完整记录。
 * @param[in] size 必须为 PRODUCT_UI_ALARM_RECORD_SIZE。
 * @return 实际写入长度或底层负错误；参数无效返回 INVALID_ARG。
 * @note 成功长度仍须由服务读回校验后确认。
 */
int product_ui_alarm_storage_write(uint8_t slot, const uint8_t *record, uint16_t size);
/**
 * @brief 在独立 sys_timer 任务建立 100 毫秒调度周期。
 * @param[in] callback 借用的持久回调，收到 NULL 上下文。
 * @return 定时器编号，零表示创建失败。
 */
uint16_t product_ui_alarm_timer_start(void (*callback)(void *));
/**
 * @brief 停止非零闹钟定时器。
 * @param[in] timer 此前创建的定时器编号。
 */
void product_ui_alarm_timer_stop(uint16_t timer);
/**
 * @brief 读取共享提示音中闹钟来源的值快照。
 * @param[out] snapshot 接收快照；NULL 时忽略。
 * @note playing 只表示 START 已确认；owned 也覆盖打开、启动在途及失败后保留的资源。
 */
void product_ui_alarm_audio_snapshot(product_ui_alarm_audio_snapshot_t *snapshot);
/**
 * @brief 按事件与尝试代次受理共享闹铃提示音。
 * @param[in] event_id 非零闹铃事件编号。
 * @param[in] attempt 同来源递增的非零尝试代次。
 * @param[in] volume 1 至 100 的播放音量。
 * @return 原样返回共享提示音受理结果；D_OK 不表示已开始播放。
 * @note 请求值被复制，时间戳取共用单调时钟；真实完成与资源占用通过快照观察。
 */
d_err_t product_ui_alarm_audio_request(uint32_t event_id, uint32_t attempt, uint8_t volume);
/**
 * @brief 取消不新于指定尝试代次的闹铃，并拒绝该上界内的迟到提交。
 * @param[in] through_attempt 非零取消上界；零表示尚未发出请求，此适配不操作。
 * @note 非阻塞提交停止意图，资源释放须继续观察来源 owned／playing。
 */
void product_ui_alarm_audio_cancel(uint32_t through_attempt);
/**
 * @brief 分配受锁保护的非零试听代次并提交共享试听来源。
 * @param[in] volume 1 至 100 的试听音量。
 * @return 原样返回提示音受理结果；成功只表示受理。
 */
d_err_t product_ui_alarm_audio_preview(uint8_t volume);
/**
 * @brief 取消此前已提交的当前试听代次，未发出过试听时不操作。
 * @note 只请求停止，不等待资源释放。
 */
void product_ui_alarm_audio_preview_cancel(void);
/**
 * @brief 读取试听来源是否仍待处理、播放或占有资源。
 * @param[out] active 非零表示 pending、playing 或 owned 任一成立。
 * @return 服务就绪时返回其来源错误，否则返回 NOT_READY；空输出返回 INVALID_ARG。
 */
d_err_t product_ui_alarm_audio_preview_status(uint8_t *active);
#endif
