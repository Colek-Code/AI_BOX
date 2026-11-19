/*
* Copyright 2025 Alibaba Group Holding Ltd.
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*     http://www.apache.org/licenses/LICENSE-2.0
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef __C_VISUAL_H__
#define __C_VISUAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_utils.h"
#include "c_mmi_cmd.h"

/**
 * @brief VISUAL 模块支持的图像分辨率枚举
 *
 * 用于在 c_visual_config_t 中指定摄像头采集或图片上传的分辨率规格。
 */
enum {
    C_VISUAL_FRAMESIZE_96x96,
    C_VISUAL_FRAMESIZE_160x120,
    C_VISUAL_FRAMESIZE_128x128,
    C_VISUAL_FRAMESIZE_176x144,
    C_VISUAL_FRAMESIZE_240x176,
    C_VISUAL_FRAMESIZE_240x240,
    C_VISUAL_FRAMESIZE_320x240,
    C_VISUAL_FRAMESIZE_320x320,
    C_VISUAL_FRAMESIZE_400x296,
    C_VISUAL_FRAMESIZE_480x320,
    C_VISUAL_FRAMESIZE_640x480,
    C_VISUAL_FRAMESIZE_800x600,
    C_VISUAL_FRAMESIZE_1024x768,
    C_VISUAL_FRAMESIZE_1280x720,
    C_VISUAL_FRAMESIZE_1280x1024,
    C_VISUAL_FRAMESIZE_1600x1200,
    C_VISUAL_FRAMESIZE_1920x1080,
    C_VISUAL_FRAMESIZE_720x1280,
    C_VISUAL_FRAMESIZE_864x1536,
    C_VISUAL_FRAMESIZE_2560x1440,
    C_VISUAL_FRAMESIZE_2560x1600,
    C_VISUAL_FRAMESIZE_1080x1920,
    C_VISUAL_FRAMESIZE_2560x1920,
    C_VISUAL_FRAMESIZE_2592x1944,
    C_VISUAL_FRAMESIZE_INVALID,
};

/**
 * @brief VISUAL 模块支持的图片格式枚举
 *
 * 用于在 c_visual_config_t 中指定图片的编码格式。
 * JPG / JPE 为 JPEG 的别名，TIFF 为 TIF 的别名。
 */
enum {
    C_VISUAL_PIC_FORMAT_JPEG,
    C_VISUAL_PIC_FORMAT_BMP,
    C_VISUAL_PIC_FORMAT_PNG,
    C_VISUAL_PIC_FORMAT_TIF,
    C_VISUAL_PIC_FORMAT_WEBP,
    C_VISUAL_PIC_FORMAT_HEIC,
    C_VISUAL_PIC_FORMAT_JPG = C_VISUAL_PIC_FORMAT_JPEG,
    C_VISUAL_PIC_FORMAT_JPE = C_VISUAL_PIC_FORMAT_JPEG,
    C_VISUAL_PIC_FORMAT_TIFF = C_VISUAL_PIC_FORMAT_TIF
};

/**
 * @brief VISUAL 模块工作模式枚举（位掩码）
 *
 * 各模式可通过按位或（|）组合使用，在 c_visual_config_t 中指定。
 * - C_VISUAL_MODE_NONE:      无视觉功能
 * - C_VISUAL_MODE_VQA:       拍照问答模式
 * - C_VISUAL_MODE_LIVE_AI:   视频通话模式
 * - C_VISUAL_MODE_OMNI:      全双工视觉对话模式（需 DUPLEX 工作模式）
 * - C_VISUAL_MODE_TRANSLATE: 图片翻译模式（复用 VQA 通道）
 * - C_VISUAL_MODE_PROACTOR:  主动交互模式
 * - C_VISUAL_MODE_REMINDER:  提醒模式（复用 VQA 通道）
 * - C_VISUAL_MODE_GUIDE:     导览模式（复用 LiveAI 通道）
 */
enum {
    C_VISUAL_MODE_NONE = 1 << 0,
    C_VISUAL_MODE_VQA = 1 << 1,
    C_VISUAL_MODE_LIVE_AI = 1 << 2,
    C_VISUAL_MODE_OMNI = 1 << 3,
    C_VISUAL_MODE_TRANSLATE = 1 << 4,
    C_VISUAL_MODE_PROACTOR = 1 << 5,
    C_VISUAL_MODE_REMINDER = 1 << 6,
    C_VISUAL_MODE_GUIDE = 1 << 7,
};

/**
 * @brief VISUAL 模块传入图像的数据类型枚举
 *
 * 用于在 c_visual_config_t 中指定图片数据的传输方式。
 * - C_VISUAL_DATA_BASE64: 直接传入 JPEG 图片数据，SDK 内部进行 Base64 编码
 * - C_VISUAL_DATA_URL:    传入图片的 HTTP/HTTPS URL 字符串
 */
enum {
    C_VISUAL_DATA_BASE64,
    C_VISUAL_DATA_URL,
};

/**
 * @brief VISUAL 模块回调事件枚举
 *
 * 在 c_visual_config_t 注册的 event_callback 中接收，用于驱动摄像头开关及图像采集时机。
 * - C_VISUAL_EVENT_MALLOC_PREPARE: PSRAM 资源分配前腾挪时机，返回非 0 中止分配；
 *                                  回调在模块锁内执行，禁止调用本模块锁相关接口
 * - C_VISUAL_EVENT_VQA_START:      拍照问答开始，建议在此事件中激活摄像头并采集一帧图片
 * - C_VISUAL_EVENT_VQA_END:        拍照问答结束，建议在此事件中关闭摄像头
 * - C_VISUAL_EVENT_LIVEAI_START:   视频通话开始（含极速/导览模式），建议在此事件中开启摄像头
 * - C_VISUAL_EVENT_LIVEAI_ACTION:  视频通话抽帧时机，需在此事件回调中采集并写入图像数据
 * - C_VISUAL_EVENT_LIVEAI_STOP:    视频通话结束，建议在此事件中关闭摄像头
 * - C_VISUAL_EVENT_PROACTOR_START: 主动交互开始，建议在此事件中开启摄像头
 * - C_VISUAL_EVENT_PROACTOR_STOP:  主动交互结束，建议在此事件中关闭摄像头
 *
 * @note 事件数值随枚举排列顺序隐含固定：新增事件只允许追加到枚举末尾，禁止重排既有项
 */
enum {
    C_VISUAL_EVENT_MALLOC_PREPARE,  // 此事件在分配 PSRAM 前触发，可在回调中腾挪资源，返回非 0 中止本次分配
    C_VISUAL_EVENT_VQA_START,        // 此事件在开启拍照问答时触发，可以在该事件回调中激活摄像头并开始拍照
    C_VISUAL_EVENT_VQA_END,          // 此事件在结束拍照问答时触发，建议在该事件回调关闭摄像头
    C_VISUAL_EVENT_LIVEAI_START,     // 此事件在开启视频通话时触发（含极速），建议在该事件回调触开启摄像头
    C_VISUAL_EVENT_LIVEAI_ACTION,    // 此事件在开始视频通话抽帧时触发，可以在该事件回调中进行图片采集
    C_VISUAL_EVENT_LIVEAI_STOP,      // 此事件在关闭视频通话时触发，建议在该事件回调关闭摄像头
    C_VISUAL_EVENT_PROACTOR_START,   // 此事件在开始主动交互时触发，建议在该事件回调触开启摄像头
    C_VISUAL_EVENT_PROACTOR_STOP,    // 此事件在退出主动交互时触发，建议在该事件回调关闭摄像头
};

typedef struct _c_visual_config_t {
    uint8_t frame_size;
    uint8_t image_format;
    uint8_t visual_mode;
    uint8_t data_type;
    uint32_t image_size;
    uint32_t fps;
    c_mmi_cmd_event_callback event_callback;
} c_visual_config_t;

/**
 * @brief 配置并初始化 VISUAL 模块
 *
 * 该函数用于在 c_mmi 初始化之前配置视觉模块参数，包括工作模式、图像格式、
 * 分辨率、帧率及事件回调等。配置以最后一次调用为准，且只有在新一轮对话
 * 开始时生效。
 *
 * @param[in] config  视觉模块配置参数指针，字段说明：
 *                    - frame_size:      图像分辨率，取值见 C_VISUAL_FRAMESIZE_* 枚举
 *                    - image_format:    图片格式，取值见 C_VISUAL_PIC_FORMAT_* 枚举
 *                    - visual_mode:     工作模式位掩码，取值见 C_VISUAL_MODE_* 枚举
 *                    - data_type:       数据传输方式，取值见 C_VISUAL_DATA_* 枚举
 *                    - image_size:      单帧图像最大字节数，超出上限将自动截断为最大值
 *                    - fps:             视频模式下的目标帧率（1~2 fps），超出范围默认为 2
 *                    - event_callback:  事件回调函数，不可为 NULL
 *
 * @return int32_t  执行结果：
 *                  - 0：成功
 *                  - 非 0：失败，常见原因：参数非法、回调为 NULL、在 c_mmi 初始化后调用
 *
 * @attention
 * - 必须在 c_mmi 初始化（c_mmi_init）之前调用，否则返回错误
 * - c_visual_deinit() 后的重新配置同样适用此约束：需先完成 c_mmi 的
 *   反初始化与重新初始化，不支持 c_mmi 运行期间的热重配
 * - visual_mode 不可为 C_VISUAL_MODE_NONE，event_callback 不可为 NULL
 * - 重复调用时若模块已初始化，仅会重置 image_count，不会更新配置
 * - 本接口仅保存配置参数并注册指令回调，不分配内存资源；图像缓冲区
 *   等 PSRAM 资源由 SDK 在收到云端视觉启动 cmd 时自动分配（亦可在
 *   会话前调用 c_visual_malloc() 显式预分配），分配前
 *   c_visual_task_handle() / c_visual_image_get_buffer() 等接口返回
 *   UTIL_ERR_NO_INIT / NULL
 */
int32_t c_visual_config(c_visual_config_t* config);

/**
 * @brief 反初始化 VISUAL 模块并释放动态申请的内存
 *
 * 释放 c_visual_malloc() 动态申请的全部资源（堆内存，ESP32 HAL 下
 * 图像缓冲区位于 PSRAM），包括：
 * - 图像编码/URL 缓冲区（buffer）
 * - 缓冲区互斥锁（buffer_mutex）
 * - 双缓冲区及其互斥锁（dbuffer）
 *
 * 调用后模块恢复为未初始化状态（含 config 清零）。如需重新配置，必须在
 * c_mmi 也回到未初始化状态后进行（即完整的 deinit + init 周期），
 * 不支持 c_mmi 运行期间的热重配。
 * 该函数为幂等操作，未初始化时调用直接返回成功。
 *
 * @return int32_t  执行结果：
 *                  - 0：成功
 *                  - 非 0：释放过程中发生异常（当前实现不会返回错误码）
 *
 * @note 典型用法（随 c_mmi 完整关闭与重启）：
 * @code
 * // 关闭：先释放视觉资源，再反初始化 c_mmi
 * c_visual_deinit();
 * c_mmi_deinit();
 *
 * // 重启：必须在 c_mmi 初始化之前重新配置，并可显式预分配资源
 * c_visual_config(&config);
 * c_visual_malloc();
 * c_mmi_init(...);
 * @endcode
 *
 * @attention
 * - 调用前建议确保 c_visual_task_handle() 已停止，避免正在访问的缓冲区被释放
 * - 本接口仅释放 c_visual 模块自身管理的内存，不会注销已注册的多模态指令回调
 * - 与 c_visual_free() 的区别：本接口清除全部状态（config 清零），
 *   free 释放资源并结束会话，但保留 config 和 inited 状态
 */
int32_t c_visual_deinit(void);

/**
 * @brief 基于已保存的 config 分配 VISUAL 资源
 *
 * c_visual_config() 仅保存配置，不分配内存。SDK 收到云端视觉启动
 * cmd 时会在回复云端前自动调用本接口分配 PSRAM 缓冲区
 * （dbuffer + buffer + buffer_mutex）；调用方亦可在会话前显式调用
 * 预分配（如 URL 模式），已分配时幂等返回成功。前提：模块已通过
 * c_visual_config() 完成配置（inited == 1），且 config 参数未被
 * c_visual_deinit() 清零。
 *
 * @return int32_t  执行结果：
 *                  - 0：成功（含已分配时的幂等返回）
 *                  - UTIL_ERR_NO_INIT：模块未配置
 *                  - UTIL_ERR_NO_MEMORY：PSRAM 分配失败
 *
 * @attention
 * - 手动调用（会话外预分配）时应确保 camera 任务已停止采集
 *   （camera_work == 0）；SDK 在 START 指令处理中的自动分配由
 *   c_visual_image_get_buffer 的未分配守护保证不会被并发写入
 * - 视觉会话启动时，malloc 会确保在 SDK 回复云端前完成：云端下
 *   发视觉启动 cmd 触发 SDK 指令处理函数后，SDK 先分配资源，
 *   成功才通过 req2rsp 回复云端并触发事件回调；分配失败则本次
 *   会话不启动（无事件回调），调用方应注意该时序
 * - 分配成功后资源常驻，SDK 不会在会话结束时自动释放；调用方可
 *   通过 c_visual_free() 手动释放，释放后下次视觉启动 cmd 会再次
 *   自动分配
 * - 分配成功后 get_buffer 恢复可用、task_handle 恢复正常驱动
 */
int32_t c_visual_malloc(void);

/**
 * @brief 释放 VISUAL 模块动态资源，保留 config 和 inited 状态
 *
 * 释放 dbuffer / buffer / buffer_mutex 三块 PSRAM 资源，但保留 config 配置、
 * inited 标志和已注册的 cmd 回调，使模块可随时通过 c_visual_malloc() 重新分配。
 * 该函数为幂等操作，资源已释放时调用直接返回成功。
 *
 * @return int32_t  执行结果：
 *                  - 0：成功
 *                  - UTIL_ERR_NO_INIT：模块未配置
 *
 * @attention
 * - 调用前必须先停止 camera 任务（camera_work = 0），并等待至少一个 camera
 *   循环周期（≥20ms），确保在途的 get_buffer / capture / image_action 调用已返回
 * - 释放后 get_buffer 返回 NULL、task_handle 返回 UTIL_ERR_NO_INIT，
 *   调用方需据此做降级处理（如 camera 任务循环中 sleep 后 continue）
 * - 释放后若收到云端视觉启动 cmd，SDK 会自动重新分配资源并启动
 *   会话，调用方应预期 free 后仍可能出现视觉会话
 * - 本接口会同时结束当前视觉会话（解除视觉接管、停止采集驱动），
 *   应在会话已结束后调用，否则进行中的会话会被终止
 * - 注意：本接口终止会话不会触发 STOP 事件回调（STOP 事件仅由
 *   云端 stop cmd 的指令处理函数触发）
 * - 与 c_visual_deinit() 的区别：本接口保留 config / inited / cmd 回调，
 *   deinit 则全部清零
 */
int32_t c_visual_free(void);

/**
 * @brief 更新视觉模块使用的图片 URL
 *
 * 当 c_visual_config_t.data_type 设置为 C_VISUAL_DATA_URL 时，
 * 通过该函数更新待发送的图片地址。URL 将在下一次 c_visual_task_handle
 * 处理时被消费并发送。
 *
 * @param[in] url      图片 URL 字符串指针，需为完整的 HTTP/HTTPS 地址
 * @param[in] url_len  URL 字符串长度（不含结束符 \0），不得超过内部缓冲区大小
 *
 * @return int32_t    执行结果：
 *                    - 0：成功
 *                    - 非 0：参数为空或 url_len 超出缓冲区限制
 */
int32_t c_visual_update_url(char *url, uint32_t url_len);
/**
 * @brief VISUAL 模块任务驱动函数
 *
 * 该函数需在外部创建的任务（Task）中循环调用，用于驱动视觉模块
 * 完成图像数据的编码与上传。函数内部会根据当前工作模式和帧率
 * 自动进行休眠调度，无需外部额外延时。
 *
 * @return int32_t  执行结果：
 *                  - 0：成功（含模块就绪但暂无图像帧需要处理的情况）
 *                  - 非 0：模块未初始化或数据发送失败
 *
 * @note 典型用法：
 * @code
 * void visual_task(void *arg) {
 *     while (1) {
 *         c_visual_task_handle();
 *     }
 * }
 * @endcode
 *
 * @attention 该函数为阻塞式驱动，必须在独立任务中运行，不可在主循环中调用
 */
int32_t c_visual_task_handle(void);

// uint32_t c_visual_update_image(uint8_t *img, uint32_t size);

/**
 * @brief 重置视觉图像双缓冲区
 *
 * 清空内部双缓冲区的数据并解除锁定状态，通常在停止采集或
 * 发生异常时调用，确保缓冲区状态一致。
 *
 * @return int32_t  执行结果：
 *                  - 0：成功
 *                  - 非 0：模块未初始化
 */
int32_t c_visual_data_reset(void);

/**
 * @brief 获取视觉图像写入缓冲区
 *
 * 返回内部图像缓冲区的指针，用于外部将图片数据写入该缓冲区。
 * 通过输出参数 @p size 返回缓冲区的可用空间大小，
 * 填充的图片数据大小不能超过该值，否则会发生内存越界。
 *
 * @param[out] size   缓冲区可用空间大小（字节数），调用者填入的图片数据不得超过此值
 *
 * @return uint8_t*  缓冲区首地址指针：
 *                   - 非 NULL：有效缓冲区地址，可直接写入图片数据
 *                   - NULL：模块未初始化，*size 同时被置为 0
 *
 * @note 使用示例：
 * @code
 * uint32_t buf_size = 0;
 * uint8_t *buf = c_visual_image_get_buffer(&buf_size);
 * if (buf && image_data_size <= buf_size) {
 *     memcpy(buf, image_data, image_data_size);
 *     c_visual_image_action(image_data_size);
 * }
 * @endcode
 *
 * @attention
 * - 调用前必须确保视觉模块已完成初始化，否则返回 NULL
 * - 写入数据大小严禁超过 *size，超出将导致内存越界
 * - 写入完成后需调用 c_visual_image_action() 通知模块处理图片
 */
uint8_t *c_visual_image_get_buffer(uint32_t *size);

/**
 * @brief 通知 VISUAL 模块图像数据已写入缓冲区
 *
 * 在调用 c_visual_image_get_buffer() 获取缓冲区并完成图像数据写入后，
 * 调用本函数通知模块实际写入的数据大小，触发内部双缓冲区交换，
 * 使数据对发送侧可见。
 *
 * @param[in] image_size  本次写入的图像数据大小（字节数），
 *                        不得超过 c_visual_image_get_buffer() 返回的缓冲区大小
 *
 * @return int32_t        执行结果：
 *                        - 0：成功，双缓冲区已完成交换
 *                        - 非 0：模块未初始化，或缓冲区当前被发送侧占用（丢帧）
 *
 * @attention
 * - 仅在 c_visual_image_get_buffer() 返回非 NULL 后调用
 * - 若返回 UTIL_ERR_BUSY，表示发送侧正在读取上一帧，本帧将被丢弃，可重试或跳过
 */
int32_t c_visual_image_action(uint32_t image_size);

#ifdef __cplusplus
}
#endif
#endif