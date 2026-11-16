/** @file music_backend.h @brief 仅由 local_music 工作任务使用的扫描与解码 SDK 边界。 */
#ifndef MUSIC_BACKEND_H
#define MUSIC_BACKEND_H
#include "music_service.h"
/**
 * @brief 扫描 TF 卡目录并保留工作任务拥有的扫描对象。
 * @param[in] formats MP3、WAV 或两者组合。
 * @param[in] cancel 可选非阻塞取消探针；SDK 可能在后续 fselect 时继续调用。
 * @param[out] count 扫描发现的文件数输出。
 * @return 成功返回 D_OK；输出为空、解码资源占用或取消返回 D_ERR_BUSY；扫描失败返回 D_ERR_IO。
 * @note 仅由 local_music 工作任务调用；扫描不启动播放。
 */
d_err_t music_backend_scan(uint8_t formats, int (*cancel)(void), uint16_t *count);
/**
 * @brief 释放当前扫描对象并清空指针。
 */
void music_backend_scan_release(void);
/**
 * @brief 按零起始索引读取曲目标题并关闭临时文件。
 * @param[in] index 扫描结果中的零起始文件索引。
 * @param[out] item 接收曲目索引与 UTF-8 标题。
 * @return 成功返回 D_OK；参数或索引无效返回 D_ERR_INVALID_ARG；选文件失败返回 D_ERR_IO。
 */
d_err_t music_backend_item(uint16_t index, music_item_t *item);
/**
 * @brief 打开指定曲目的解码器，在各阶段重新检查播放准入。
 * @param[in] index 扫描目录的零起始曲目索引。
 * @param[in] volume 向 SDK 提交的音量。
 * @param[in] resume 非零使用当前有效断点。
 * @param[in] permit 可选非阻塞准入回调。
 * @return 成功返回 D_OK；返回占用、断点不可用、资源或准入错误。
 * @note 仅由工作任务调用；失败会尝试 stop，但 stop 失败仍保留可能在用的对象，调用者须查询 owned。
 */
d_err_t music_backend_start(uint16_t index, uint8_t volume, uint8_t resume, d_err_t (*permit)(void));
/**
 * @brief 可选保存断点，确认 STOP 成功后关闭解码与文件。
 * @param[in] save_position 非零尝试保存续播断点。
 * @param[out] resume_error 可选断点结果输出；停止成功不代表断点可用。
 * @return STOP 成功或无在途解码返回 D_OK；STOP 失败返回 D_ERR_IO 并保留对象。
 * @note 扫描对象不在此释放；STOP 失败时调用者不能释放仍可能在用的资源。
 */
d_err_t music_backend_stop(uint8_t save_position, d_err_t *resume_error);
/**
 * @brief 释放 SDK 分配的断点载荷并清空断点。
 */
void music_backend_forget_position(void);
/**
 * @brief 检查是否仍持有解码器或播放文件。
 * @return 任一对象存在返回非零，否则为 0。
 */
uint8_t music_backend_owned(void);
/**
 * @brief 检查续播断点载荷是否存在且长度有效。
 * @return 可使用断点时返回非零。
 */
uint8_t music_backend_can_resume(void);
/**
 * @brief 向现有解码器提交音量。
 * @param[in] volume 向 SDK 提交的音量，本层不校验范围。
 * @return 无解码器或设置成功返回 D_OK；SDK 请求失败返回 D_ERR_IO。
 */
d_err_t music_backend_volume(uint8_t volume);
/**
 * @brief 复制解码事件维护的播放进度。
 * @param[out] position 非空当前位置秒数输出。
 * @param[out] duration 非空总时长秒数输出。
 */
void music_backend_progress(uint32_t *position, uint32_t *duration);
/**
 * @brief 消费一次解码终止标记并读取终止结果。
 * @param[out] result 非空终止结果输出。
 * @return 此前收到终止事件返回非零；否则返回 0。
 * @note 读取后清除 ended 标记；不会执行 STOP 或关闭资源。
 */
uint8_t music_backend_ended(d_err_t *result);
#endif
