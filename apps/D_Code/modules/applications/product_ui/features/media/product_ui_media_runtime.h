/** @file product_ui_media_runtime.h @brief 产品语音、闹铃、音乐及音量的所有者协调，不持有 SDK 句柄或页面对象。 */
#ifndef PRODUCT_UI_MEDIA_RUNTIME_H
#define PRODUCT_UI_MEDIA_RUNTIME_H
#include "framework/d_error.h"
#include <stdint.h>
/**
 * @brief 串行绑定音乐配置、提示音交接、语音及电源协作回调。
 * @return 全部绑定成功返回 D_OK；首个绑定失败原样返回。
 * @note 使用静态配置及回调，先取得启动音乐占用以等待初始音量同步；失败可能保留此前已建立的静态绑定。
 */
d_err_t product_ui_media_runtime_bind(void);
/**
 * @brief 串行注册设备音量后端，建立无需页面的释放与恢复推进路径。
 * @return 原样返回音量后端注册结果；必要的备用定时器创建失败返回 NO_MEMORY。
 * @note 音乐 worker 不可用时建立 sys_timer 完成协调；本函数不等待解码，不因页面退出而停止静态协调器。
 */
d_err_t product_ui_media_runtime_start(void);
/**
 * @brief 查询产品媒体协调器已启动且没有保留的音量故障。
 * @return 满足条件返回非零，否则为零。
 * @note 此值不单独代表媒体卡、音乐 worker 或某一播放请求已就绪。
 */
uint8_t product_ui_media_runtime_ready(void);
/**
 * @brief 为 OTA 固定客户端取得维护占用，发起音乐收敛。
 * @note 电源及外部客户端拥有独立占用位；受理占用不表示音乐已经停止。
 */
void product_ui_media_maintenance_begin(void);
/**
 * @brief 只在 OTA 已持有维护占用时等待音乐收敛。
 * @param[in] timeout_ms 最多等待毫秒数，零只检查当前状态。
 * @return OTA 无占用返回 D_OK；有占用时返回音乐完成结果或超时。
 * @note 部分开始失败且未取得 OTA 占用时，不等待无关播放所有者。
 */
d_err_t product_ui_media_maintenance_wait(uint32_t timeout_ms);
/**
 * @brief 持久记录 OTA 释放意图并尝试完成占用解除。
 * @return 已解除返回 D_OK；尚未完成返回 D_ERR_BUSY。
 * @note BUSY 不丢弃释放意图；音乐空闲回调及备用协调定时器继续推进，调用方不重试也可完成。其他客户端的占用不被清除。
 */
d_err_t product_ui_media_maintenance_release(void);
#endif
