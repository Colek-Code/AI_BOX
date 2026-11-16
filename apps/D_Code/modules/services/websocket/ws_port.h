/** @file ws_port.h @brief SDK 与 RTOS 的私有边界，由 WebSocket 工作任务负责资源销毁。 */
#ifndef D_WS_PORT_H
#define D_WS_PORT_H
#include "ws_service.h"
#include "ws_http.h"
/**
 * @brief 取得 WebSocket 服务元数据自旋锁。
 * @note 仅保护短元数据操作，不在锁内执行 I/O 或外部回调。
 */
void ws_port_lock(void);
/**
 * @brief 释放 WebSocket 服务元数据自旋锁。
 */
void ws_port_unlock(void);
/**
 * @brief 在产品启动阶段一次性创建 I/O 互斥量。
 * @return 成功或已经创建返回 D_OK；创建失败返回 D_ERR_NO_MEMORY。
 * @note 在向调用者暴露服务前串行初始化。
 */
d_err_t ws_port_init(void);
/**
 * @brief 读取当前 SDK 编译支持的客户端与服务端能力。
 * @return WS_CAP_* 能力位掩码；服务端 WSS 取决于 MBEDTLS_SSL_SRV_C。
 */
uint32_t ws_port_capabilities(void);
/**
 * @brief 等待旧工作任务结束后创建新 WebSocket 工作任务。
 * @param[in] worker 新工作任务入口。
 * @return 创建成功返回 D_OK；失败返回 D_ERR_NO_MEMORY。
 * @note 仅在前一个工作任务已发布最终 inactive 状态后调用。
 */
d_err_t ws_port_start(void (*worker)(void *));
/**
 * @brief 有界等待 I/O 互斥量。
 * @param[in] timeout_ms 等待毫秒数，向上换算为 10 ms 节拍；0 沿用 SDK 的零超时语义。
 * @return 取得锁返回非零，否则为 0。
 */
int ws_port_io_lock(uint32_t timeout_ms);
/**
 * @brief 释放当前持有的 I/O 互斥量。
 */
void ws_port_io_unlock(void);
/**
 * @brief 按系统节拍让出执行。
 * @param[in] milliseconds 等待毫秒数，向上取整到 10 ms。
 */
void ws_port_wait(uint32_t milliseconds);
/**
 * @brief 读取用于期限比较的单调毫秒时钟。
 * @return uint32_t 环形毫秒值。
 */
uint32_t ws_port_now(void);
/**
 * @brief 检查 WiFi 已开启且 STA 已取得 DHCP 地址。
 * @return 两项满足返回非零，否则为 0。
 */
int ws_port_network_ready(void);
/**
 * @brief 初始化随机源、TLS 对象及可选监听套接字。
 * @param[in] config 非空配置，借用至 close_all。
 * @param[in] url 客户端解析后的 URL，借用至 close_all。
 * @return 成功返回 D_OK；已有对象、可信时间不足、配置或网络错误返回对应错误。
 * @note 失败也可能保留已初始化对象，工作任务必须调用 close_all 收尾；此处不完成连接或 TLS 握手。
 */
d_err_t ws_port_prepare(const ws_service_config_t *config, const ws_url_t *url);
/**
 * @brief 连接配置中的客户端地址并配置非阻塞对端。
 * @return 成功返回 D_OK；时间未就绪、网络或对端配置错误返回相应值。
 * @note 由工作任务持有 I/O 所有权；失败连接的 SDK 句柄按当前 SDK 释放约定置空。
 */
d_err_t ws_port_connect(void);
/**
 * @brief 非阻塞接收一个监听连接并配置对端。
 * @return 接入成功返回 D_OK；无待接对端返回 D_ERR_NOT_READY；网络或配置失败返回相应错误。
 * @note 无对端时监听器仍由工作任务持有。
 */
d_err_t ws_port_accept(void);
/**
 * @brief 执行有界 TLS 握手；会话复用失败时至多重新连接一次。
 * @param[in] cancelled 非空取消探针，在握手循环中调用。
 * @return 成功返回 D_OK；取消返回 D_ERR_NOT_READY；期限、验证或网络失败返回对应错误。
 * @note 不降低证书验证要求；失败后仍由所有者关闭对端。明文连接只检查取消。
 */
d_err_t ws_port_tls_handshake(int (*cancelled)(void));
/**
 * @brief 从当前明文或 TLS 对端非阻塞读取。
 * @param[out] bytes 接收字节的缓冲，仅本次调用借用。
 * @param[in] capacity 缓冲字节容量。
 * @return 正数为读取字节数，0 为暂未进展，负数为 EOF 或失败。
 */
int ws_port_read(uint8_t *bytes, unsigned capacity);
/**
 * @brief 向当前明文或 TLS 对端非阻塞写入。
 * @param[in] bytes 仅本次调用借用的字节。
 * @param[in] length 待写字节数。
 * @return 正数为写出字节数，0 为暂未进展，负数为失败。
 */
int ws_port_write(const uint8_t *bytes, unsigned length);
/**
 * @brief 释放当前对端及其 TLS 连接，保留监听与会话配置。
 * @return 当前实现返回 D_OK。
 */
d_err_t ws_port_close_peer(void);
/**
 * @brief 释放对端、监听与全部 TLS 配置并清空端口状态。
 * @return 当前实现返回 D_OK。
 */
d_err_t ws_port_close_all(void);
/**
 * @brief 从本端已初始化的 DRBG 取得随机字节。
 * @param[out] bytes 随机字节输出。
 * @param[in] length 要求的字节数。
 * @return Mbed TLS 随机源原始结果，0 成功，非零失败。
 */
int ws_port_random(uint8_t *bytes, unsigned length);
/**
 * @brief 生成 16 字节随机量的 Base64 客户端握手 key。
 * @param[out] key 至少 25 字节的零结尾 key 输出。
 * @return 成功返回 0，随机或编码失败返回 -1。
 */
int ws_port_key(char key[25]);
/**
 * @brief 按 WebSocket GUID 计算握手 accept。
 * @param[in] key 长度为 24 的零结尾客户端 key。
 * @param[out] accept 至少 29 字节的零结尾输出。
 * @return 成功返回 0；key 长度、摘要或编码失败返回 -1。
 */
int ws_port_accept_key(const char *key, char accept[29]);
#endif
