/**
 * @file    music_service.h
 * @brief   本地音乐服务接口（事件驱动）
 */

#ifndef MUSIC_SERVICE_H
#define MUSIC_SERVICE_H

#include "framework/d_error.h"

/**
 * @brief 音乐服务初始化
 */
d_err_t music_service_init(void);

/**
 * @brief 音乐服务销毁
 */
void music_service_deinit(void);

/**
 * @brief 播放/暂停
 */
void music_service_play_pause(void);

/**
 * @brief 下一首
 */
void music_service_next(void);

/**
 * @brief 上一首
 */
void music_service_prev(void);

/**
 * @brief 设置音量
 * @param step +5=增大, -5=减小
 */
void music_service_volume(int step);

#endif /* MUSIC_SERVICE_H */
