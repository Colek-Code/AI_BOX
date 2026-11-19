/** @file qwen_sdk_audio_io.h @brief 音频任务可用的 SDK 数据接口，避免杰理文件系统与 newlib 的 FILE 声明冲突。 */
#ifndef QWEN_SDK_AUDIO_IO_H
#define QWEN_SDK_AUDIO_IO_H
#include <stdint.h>

/* 服务翻译单元同时包含厂商 c_mmi.h，签名变更会产生编译错误。 */
/** @brief 提交采集数据，返回 SDK 实际接收的字节数。 */
uint32_t c_mmi_put_recorder_data(uint8_t *data, uint32_t size);
/** @brief 读取下行音频，返回 SDK 实际交付的字节数。 */
uint32_t c_mmi_get_player_data(uint8_t *data, uint32_t size);
#endif
