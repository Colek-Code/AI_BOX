/**
 * @file d_screen_power.h
 * @brief 统一管理屏幕空闲、背光和触摸唤醒门禁，持久化策略由产品层管理。
 */
#ifndef D_SCREEN_POWER_H
#define D_SCREEN_POWER_H

#include <stdint.h>
#include "framework/d_error.h"

/**
 * @brief 按默认值初始化内存中的屏幕电源状态。
 * @note 仅由 UI 线程调用；已初始化时不重复清状态，不读取或写入 VM。
 */
void d_screen_power_init(void);
/**
 * @brief 推进空闲及唤醒保护计时，达到配置阈值时关闭背光。
 * @param[in] elapsed_ms 本轮经过的毫秒数。
 * @note 仅供 UI 线程周期调用；持有自动熄屏占用或设为永不熄屏时清空闲计时。
 */
void d_screen_power_update(uint32_t elapsed_ms);
/**
 * @brief 亮屏时清空闲计时；息屏时不改变息屏事实。
 */
void d_screen_power_reset_idle(void);
/**
 * @brief 在 UI 线程取得一份自动熄屏占用并清空闲计时。
 * @return D_OK 为取得成功；计数达到 UINT16_MAX 时为 D_ERR_FULL。
 * @note 每次成功须配对一次 release；只抑制自动熄屏，手动关屏仍然生效。
 */
d_err_t d_screen_power_hold_auto_off(void);
/**
 * @brief 释放一份自动熄屏占用，最后一份释放后重置空闲计时。
 * @note 仅 UI 线程调用；计数为零时不减为负数，不能替其他所有者额外释放。
 */
void d_screen_power_release_auto_off(void);
/**
 * @brief 过滤息屏唤醒手势，只有后续可交给界面的按压才透传。
 * @param[in] pressed 原始按压电平，非零视为按下。
 * @return 1 表示允许作为界面按压；0 表示松开、息屏或唤醒过滤中的输入。
 * @note UI 输入线程调用；息屏后仅在保护期结束且出现新按下沿时尝试唤醒，该次唤醒触摸仍被吞掉直到松手。
 */
uint8_t d_screen_power_filter_touch(uint8_t pressed);
/**
 * @brief 设置自动熄屏选项并重置可重置的空闲计时。
 * @param[in] index 0 至 4 对应 30秒、60秒、5分钟、15分钟、永不；越界回退到默认索引 4。
 * @note 仅修改内存，不保存用户偏好；不主动唤醒息屏界面。
 */
void d_screen_power_set_timeout_index(uint8_t index);
/**
 * @brief 读取当前内存中的自动熄屏选项。
 * @return 当前合法索引，未初始化时先使用默认值。
 */
uint8_t d_screen_power_get_timeout_index(void);
/**
 * @brief 读取当前自动熄屏阈值。
 * @return 阈值毫秒数；0 表示永不自动熄屏。
 */
uint32_t d_screen_power_get_timeout_ms(void);
/**
 * @brief 设置触摸唤醒许可并重置可重置的空闲计时。
 * @param[in] enabled 非零启用，零禁用。
 * @note 仅修改内存，不写 VM。
 */
void d_screen_power_set_touch_wake_enabled(uint8_t enabled);
/**
 * @brief 查询内存中的触摸唤醒许可。
 * @return 启用为 1，禁用为 0。
 */
uint8_t d_screen_power_get_touch_wake_enabled(void);
/**
 * @brief 查询软件记录的息屏事实。
 * @return 息屏为 1，亮屏为 0；不直接读取背光引脚。
 */
uint8_t d_screen_power_is_off(void);
/**
 * @brief 清息屏状态并尝试恢复背光；失败时重新进入息屏。
 * @note UI 线程调用；不负责持久化亮度，恢复失败不伪装为已亮屏。
 */
void d_screen_power_wake(void);
/**
 * @brief 清除息屏及计时状态，但不驱动背光。
 * @note 用于调用方随后一次性应用目标亮度，避免先恢复旧亮度再改亮度产生闪变；背光应用失败时调用方须 force_off 回滚。
 */
void d_screen_power_clear_off(void);
/**
 * @brief 立即进入息屏状态并请求背光关闭。
 * @note 可用于亮度应用失败回滚；不受自动熄屏占用限制，不检查底层关闭返回值。
 */
void d_screen_power_force_off(void);

#endif /* D_SCREEN_POWER_H */
