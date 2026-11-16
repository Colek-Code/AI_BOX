/** @file ota_port.h @brief OTA 可替换的设备私有边界，不依赖 LVGL。 */
#ifndef D_OTA_PORT_H
#define D_OTA_PORT_H
#include "ota_service.h"
/**
 * @brief 取得 OTA 短元数据自旋锁。
 * @note 不得在锁内等待 SDK 或执行网络 I/O。
 */
void ota_port_lock(void);
/**
 * @brief 释放 OTA 元数据自旋锁。
 */
void ota_port_unlock(void);
/**
 * @brief 读取 OTA 期限使用的单调毫秒时钟。
 * @return uint32_t 环形毫秒值。
 */
uint32_t ota_port_now(void);
/**
 * @brief 读取当前编译配置的板型编号。
 * @return D_BOARD_TARGET 的值。
 */
uint32_t ota_port_board(void);
/**
 * @brief 借用编译配置中的默认发布地址。
 * @return 静态生命周期的 D_OTA_RELEASE_URL 字符串。
 */
const char *ota_port_default_url(void);
/**
 * @brief 核对 WiFi DHCP 与曾经 NTP 确认的 SDK UTC。
 * @return 网络未就绪返回 D_ERR_NETWORK；可信时间不足返回 D_ERR_NOT_READY；满足返回 D_OK。
 * @note 产品手动时钟不能替代 SDK 软件 UTC 的可信时间边界。
 */
d_err_t ota_port_ready(void);
/**
 * @brief 建立持续期限观察定时器并创建一次 OTA 工作任务。
 * @param[in] worker OTA 任务入口。
 * @param[in] context 传入任务的借用上下文，生命周期由调用者保证。
 * @return 成功返回 D_OK；观察器或线程创建失败返回 D_ERR_NO_MEMORY。
 * @note 定时器保留到重启；任务创建失败不释放已经创建的观察器。
 */
d_err_t ota_port_start(void (*worker)(void *), void *context);
/**
 * @brief 记录当前请求快照并发布相应 OTA 唤醒事件。
 * @param[in] state 本次通知的状态。
 * @note 事件仅为提示，消费者须读取含请求编号及代次的快照核对真实状态。
 */
void ota_port_notify(ota_state_e state);
/**
 * @brief 调用 SDK 系统复位入口。
 */
void ota_port_reboot(void);
/**
 * @brief 按 10 ms 系统节拍向上取整并延时。
 * @param[in] milliseconds 等待毫秒数。
 */
void ota_port_wait(uint32_t milliseconds);

/**
 * @brief 建立严格 TLS 连接，发送 HEAD 或 GET 并解析响应头。
 * @param[in] url 零结尾 HTTPS 地址。
 * @param[in] head 非零发送 HEAD，0 发送 GET。
 * @param[in] etag 可空或空串；非空时作为已验证的 If-Match 值发送。
 * @param[out] response 非空 HTTP 响应摘要输出。
 * @return 成功返回 D_OK；URL、连接、握手、响应格式或期限错误返回对应值。
 * @note 先关闭此前传输；失败可能保留 TLS 对象，由当前 OTA 工作任务调用 close。
 */
d_err_t ota_transport_open(const char *url, uint8_t head, const char *etag,
                           ota_http_response_t *response);
/**
 * @brief 有界等待并读取当前 TLS 数据。
 * @param[out] bytes 非空接收缓冲。
 * @param[in] capacity 非零缓冲容量。
 * @return 正数为字节数，0 为连接结束，负数为参数、I/O 或等待失败。
 */
int ota_transport_read(uint8_t *bytes, uint16_t capacity);
/**
 * @brief 释放哈希与网络 TLS 对象，不等待无界 close-notify。
 * @note 只由拥有传输的 OTA 工作任务执行。
 */
void ota_transport_close(void);
/**
 * @brief 开始新一次 SHA256 流式摘要，释放旧哈希上下文。
 */
void ota_transport_hash_begin(void);
/**
 * @brief 在无已锁存错误时继续累计 SHA256。
 * @param[in] bytes 输入字节，调用期间借用。
 * @param[in] length 输入字节数。
 * @note 调用前需 hash_begin；错误锁存至 finish。
 */
void ota_transport_hash_update(const uint8_t *bytes, uint32_t length);
/**
 * @brief 结束 SHA256 并释放哈希上下文。
 * @param[out] digest 至少 32 字节的摘要输出，错误时不可使用。
 * @return 成功返回 D_OK；任一步摘要错误返回 D_ERR_IO。
 */
d_err_t ota_transport_hash_finish(uint8_t digest[32]);

/**
 * @brief 独占初始化被动升级上下文，并计算首次写入对齐长度。
 * @return 成功返回 D_OK；已有 SDK 上下文返回 D_ERR_BUSY；初始化或允许检查失败返回 D_ERR_IO。
 * @note 失败可能保留 SDK 所有权或 pending，调用者须受检收尾，不能假定失败无副作用。
 */
d_err_t ota_sdk_begin(void);
/**
 * @brief 读取目标更新地址对齐后的首包长度。
 * @return 成功 begin 后的首包字节数，不超过 OTA_PACKET_SIZE。
 */
uint16_t ota_sdk_first_size(void);
/**
 * @brief 由 OTA 工作任务串行提交一个 SDK 数据包。
 * @param[in] bytes 本次调用借用的非空包缓冲。
 * @param[in] length 1～OTA_PACKET_SIZE 的字节数。
 * @return SDK 接收成功且无锁存写错误返回 D_OK；参数或在途冲突返回 D_ERR_INVALID_ARG；写失败返回 D_ERR_IO。
 * @note 当前 SDK 在返回前复制或完成相应写等待，输入缓冲不逃逸；缓冲包可能没有写回调。
 */
d_err_t ota_sdk_write(const uint8_t *bytes, uint16_t length);
/**
 * @brief 提交被动升级校验并等待同次结果回调。
 * @return 成功返回 D_OK；未持上下文或有在途调用返回 D_ERR_NOT_READY；提交、校验或锁存写错误返回 D_ERR_IO。
 * @note SDK 无取消或队列受理保证，缺失回调时保留工作任务与静态缓冲，独立观察器仅暴露超时事实。
 */
d_err_t ota_sdk_verify(void);
/**
 * @brief 提交升级启动信息并等待完成回调。
 * @return 成功返回 D_OK；未持上下文或存在在途调用返回 D_ERR_NOT_READY；提交或完成失败返回 D_ERR_IO。
 * @note 此接口具有真实升级提交副作用；回调未到时不提前释放上下文。
 */
d_err_t ota_sdk_commit(void);
/**
 * @brief 仅在 SDK 调用及回调均不在途时释放被动升级上下文。
 * @return 已释放或原本未持有返回 D_OK；仍在途或退出未确认返回 D_ERR_BUSY。
 * @note 只有 OTA 工作任务可调用，不能在 dw_update 回调任务中销毁。
 */
d_err_t ota_sdk_close(void);
/**
 * @brief 读取 SDK 命令等待回调或同步调用在途状态。
 * @return 任一在途标记存在返回非零。
 */
uint8_t ota_sdk_pending(void);

#endif
