/** @file qwen_auth_port.h @brief 公开的网络时间适配接口，凭据保持在私有接入库内。 */
/** 公开时间适配，凭据仍保留在私有接入库中。 */
#ifndef QWEN_AUTH_TIME_PORT_H
#define QWEN_AUTH_TIME_PORT_H
/**
 * @brief 由认证准备路径同步已确认的网络时间到加密时间源。
 * @return NTP 已确认且 SDK UTC 位于允许范围时返回 1；否则返回 0。
 * @note 实现位于 qwen_auth_port.c；该头不暴露凭据读写接口。
 */
int qwen_auth_sync_network_time(void);
#endif
