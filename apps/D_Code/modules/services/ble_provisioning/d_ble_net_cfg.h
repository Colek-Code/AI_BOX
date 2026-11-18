/**
 * @file d_ble_net_cfg.h
 * @brief BLE 配网对外入口，SDK 事件、广播意图与明文扩展边界。
 */

#ifndef D_BLE_NET_CFG_H
#define D_BLE_NET_CFG_H

#include <stdint.h>

struct sys_event;

/**
 * @brief 声明 SDK 配网连接入口，实现在 SDK 配网模块。
 * @note 此处仅保留兼容声明，不代表本文件提供实现。
 */
void config_network_connect(void);
/**
 * @brief 分发来自蓝牙连接源的系统事件。
 * @param[in] event 非空 SDK 系统事件，payload 按 bt_event 解释。
 * @return 当前分支处理后返回 0；其他来源同样返回 0。
 */
int ble_demo_bt_event_handler(struct sys_event *event);
/**
 * @brief 关闭 sniff，设置 BLE 地址并启动蓝牙协议栈。
 * @note 协议栈后续回调 ble_profile_init；此入口不证明 BT_STATUS_INIT_OK 已到达。
 */
void bt_ble_module_init(void);
/**
 * @brief 用 WiFi MAC 尾部生成设备名，建立互斥量并启用配网模块。
 * @note 在蓝牙协议栈就绪后调用；广播最终生效由协议栈执行。
 */
void bt_ble_init(void);
/**
 * @brief 请求停用配网模块，活动连接可能需要异步断开。
 */
void bt_ble_exit(void);
/**
 * @brief 保存配网广播意图，并在有连接时等待断连后重开。
 * @param[in] enable 非零要求广播，0 禁止断连后自动重开。
 * @note 有 GATT 连接时先提交断连，不表示函数返回即已广播。
 */
void d_ble_net_cfg_set_adv(uint8_t enable);
/**
 * @brief 请求断开当前 BLE 连接，忽略底层立即返回值。
 */
void ble_app_disconnect(void);
/**
 * @brief 仅在配网命令已经受理时转发 WiFi 连接结果。
 * @param[in] event SSID 未找到、关联失败或已连接的网络事件。
 */
void ble_cfg_net_result_notify(int event);
/**
 * @brief 格式化并尝试发送含 MAC、可选 UUID 与批次的成功响应。
 * @return 格式化成功返回0，不代表手机已收到；格式化失败或截断返回-1且不发送部分JSON。
 */
int ble_connected_net_notify(void);
/**
 * @brief 弱实现按明文复制接收内容，可由产品覆盖。
 * @param[in] in 非空输入缓冲。
 * @param[in] in_len 输入字节数。
 * @param[out] out 非空输出缓冲。
 * @param[in,out] out_len 非空；输入为容量，成功后改为写入字节数。
 * @return 复制成功返回 0；指针为空或容量不足返回 -1。
 */
int d_ble_decrypt(const uint8_t *in, uint16_t in_len,
                  uint8_t *out, uint16_t *out_len);
/**
 * @brief 弱实现原样复制发送内容，可由产品覆盖。
 * @param[in] in 至少 in_len 字节的有效输入。
 * @param[in] in_len 待复制字节数。
 * @param[out] out 至少入参out_len所指容量的有效输出缓冲。
 * @param[in,out] out_len 输入为容量，成功后为写入长度。
 * @return 默认实现复制成功为0，空指针或容量不足为-1且不写输出。
 * @note 覆盖实现也必须遵守容量合同，失败不能作为可发送的数据发布。
 */
int d_ble_encrypt(const uint8_t *in, uint16_t in_len,
                  uint8_t *out, uint16_t *out_len);

/**
 * @brief 有界加密并尝试发送JSON；本接口不提供排队或对端送达确认。
 * @param[in] json_str 调用期间稳定的零结尾输入，超过192字节或加密失败时不发送。
 */
void d_ble_send_json_response(const char *json_str);

#endif /* D_BLE_NET_CFG_H */
