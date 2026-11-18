/** @file product_ui_host_alarm.h @brief 闹钟双槽文件、保存故障与独立后台时钟的主机控制合同。 */
#ifndef PRODUCT_UI_HOST_ALARM_H
#define PRODUCT_UI_HOST_ALARM_H
#include <stdint.h>
/**
 * @brief 在 VM 互斥内复制双槽文件前缀，写入已进入时拒绝改路径。
 * @param[in] prefix 双槽文件名前缀，UTF-8 编码且小于 740 字节。
 * @return D_OK 已设置；空或过长为 D_ERR_INVALID_ARG；在途写入为 D_ERR_BUSY。
 */
int product_ui_host_alarm_path(const char *prefix);
/**
 * @brief 以网络互斥保存 UTC 样本及对应设备毫秒锚点。
 * @param[in] utc_seconds 网络 UTC 秒样本，负数表示当前不可用。
 */
void product_ui_host_alarm_network(int64_t utc_seconds);
/**
 * @brief 持 tick 互斥推进原子设备时间并同步调用时钟与闹钟回调。
 * @param[in] milliseconds 本次推进的设备毫秒数或注册周期；注册接口须为 100。
 * @note 回调在本函数持有 tick 互斥期间执行，调用方不能把它等同目标 RTOS 调度。
 */
void product_ui_host_alarm_advance(uint32_t milliseconds);
/**
 * @brief 仅在独立实时模式关闭时随 UI 推进闹钟设备时钟。
 * @param[in] milliseconds 本次推进的设备毫秒数或注册周期；注册接口须为 100。
 */
void product_ui_host_alarm_ui_tick(uint32_t milliseconds);
/**
 * @brief 在 VM 互斥内设置故障和写阻断，并广播唤醒等待者。
 * @param[in] fault 0 正常，1 打开写失败，2 短写，3 读取损坏，4 短读，5 存储未就绪。
 * @param[in] blocked 是否阻断写入；解除后条件变量唤醒。
 */
void product_ui_host_alarm_fault(int fault, int blocked);
/**
 * @brief 为单槽指定读取结果；非法槽忽略，零恢复真实文件路径。
 * @param[in] slot 双槽记录下标，仅允许 0 或 1。
 * @param[in] result 需要强制返回的存储读取结果，零为使用真实主机文件结果。
 */
void product_ui_host_alarm_read_fault(uint8_t slot, int result);
/**
 * @brief 在 VM 互斥内读取写入是否已经进入。
 * @return 非零表示写入在途。
 */
int product_ui_host_alarm_entered(void);
/**
 * @brief 读取双槽主机写入尝试累计次数。
 * @return 包括失败及阻断中的写入尝试。
 */
unsigned product_ui_host_alarm_writes(void);
/**
 * @brief 启停独立 pthread 时钟；关闭时 join 等待退出。
 * @param[in] enabled 是否启用独立实时后台时钟。
 * @return D_OK 模式已切换；重复启用为 D_ERR_BUSY；创建线程失败为 D_ERR_IO。
 */
int product_ui_host_alarm_realtime(int enabled);
/**
 * @brief 在音频互斥内设置发声或取消错误分区。
 * @param[in] fault 1 请求发声失败，2 取消已占用音频失败，0 正常。
 */
void product_ui_host_alarm_audio_fault(int fault);
/**
 * @brief 设置定时器失败：1 闹钟服务定时器，2 产品时钟定时器。
 * @param[in] fault 主机故障分区编号，具体取值由当前接口说明。
 */
void product_ui_host_alarm_timer_fault(int fault);
#endif
