/** @file cipher_panel_wifi_input.h @brief 旧 Cipher WiFi 页的单实例密码层，连接由调用方同步回调受理。 */
#ifndef __CIPHER_PANEL_WIFI_INPUT_H__
#define __CIPHER_PANEL_WIFI_INPUT_H__

#include "lvgl.h"
#include "modules/services/wifi/wifi_service.h"
#include <stdint.h>

/**
 * @brief 同步受理当前输入，组件在非零返回后清密码并延后关闭。
 * @param[in] ssid 本次调用借用的网络身份，返回前须复制，不保留指针。
 * @param[in] pwd 本次调用借用的密码，返回前须复制，不保留指针。
 * @param[in,out] user_data 调用方持有的持续上下文。
 * @return 非零表示已接受输入，零保留面板供用户重试，不表示网络已连接。
 * @note 回调内不删除当前输入层，返回后组件仍需清空密码文本区。
 */
typedef uint8_t (*cipher_wifi_input_connect_cb_t)(const char *ssid,
                                                  const char *pwd,
                                                  void *user_data);

/**
 * @brief 只清空输入层对象、回调及候选网络引用，不删除实际对象。
 * @note 仅在已知旧树正在删除或尚未创建时使用；需要主动关闭时调用 close，不能用 reset 代替对象清理。
 */
void cipher_wifi_input_reset(void);
/**
 * @brief 同步删除当前密码层并清空候选网络及回调借用。
 */
void cipher_wifi_input_close(void);
/**
 * @brief 关闭旧密码层、复制候选网络并创建输入卡与静态布局键盘。
 * @param[in,out] parent 有效页面父容器。
 * @param[in] item 本次复制的扫描结果条目。
 * @param[in] connect_cb 持续有效的受理回调，可为空。
 * @param[in] user_data 回调借用上下文，须覆盖输入层寿命。
 * @note 仅 LVGL 线程；void 接口不保证动作按钮及键盘全部分配成功。创建必要层／输入区失败会关闭局部树。
 */
void cipher_wifi_input_open(lv_obj_t *parent,
                            const wifi_service_scan_result_t *item,
                            cipher_wifi_input_connect_cb_t connect_cb,
                            void *user_data);
/**
 * @brief 输入提示标签存在时复制状态文字及颜色。
 * @param[in] text 本次复制的文字，NULL 时忽略。
 * @param[in] color 二十四位 RGB 颜色值。
 */
void cipher_wifi_input_set_message(const char *text, uint32_t color);
/**
 * @brief 查询静态输入层引用是否存在。
 * @return 存在为 1，否则为 0；延后关闭请求清引用后即为零，不表示旧树删除回调已全部运行。
 */
uint8_t cipher_wifi_input_is_open(void);

#endif /* __CIPHER_PANEL_WIFI_INPUT_H__ */
