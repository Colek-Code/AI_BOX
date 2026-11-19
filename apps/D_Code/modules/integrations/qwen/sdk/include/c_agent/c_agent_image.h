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

#ifndef __C_AGENT_IMAGE_H__
#define __C_AGENT_IMAGE_H__

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 图像生成模式定义
 *
 * 指定图像生成 Agent 的工作模式：
 * - C_AGENT_IMAGE_MODE_TEXT : 文生图，根据文字描述生成图片（textToImage）
 * - C_AGENT_IMAGE_MODE_IMAGE: 图生图，以输入图片为参考辅助生成（imageAssistant）
 * - C_AGENT_IMAGE_MODE_DRAW : 草图生图，将手绘草图转换为完整图片（sketchToImage）
 */
enum {
    C_AGENT_IMAGE_MODE_TEXT,    /**< 文生图模式 */
    C_AGENT_IMAGE_MODE_IMAGE,   /**< 图生图模式 */
    C_AGENT_IMAGE_MODE_DRAW,    /**< 草图生图模式 */
};

/**
 * @brief 设置生成图片的输出尺寸
 *
 * 配置图像生成 Agent 输出图片的宽高像素值，须在调用 c_agent_image_start() 前设置。
 * 默认尺寸为 768×768。
 *
 * @param image_width   输出图片宽度（像素），有效范围 [200, 2048]
 * @param image_height  输出图片高度（像素），有效范围 [200, 2048]
 *
 * @note 宽高比限制：长边不得超过短边的 4 倍
 *
 * @return int32_t  UTIL_SUCCESS=成功，UTIL_ERR_INVALID_PARAM=参数超出范围
 */
int32_t c_agent_image_set_size(uint32_t image_width, uint32_t image_height);

/**
 * @brief 设置生成图片的输出格式
 *
 * 配置图像生成 Agent 输出图片的文件格式，须在调用 c_agent_image_start() 前设置。
 * 默认格式为 PNG。
 *
 * @param jpg_enable  格式选择：1=JPG，0=PNG
 *
 * @return int32_t  UTIL_SUCCESS=成功
 */
int32_t c_agent_image_set_format(uint8_t jpg_enable);

/**
 * @brief 启动图像生成 Agent
 *
 * 按指定模式构造 biz 参数并通过 c_mmi_update_biz_params() 下发，触发图像生成流程。
 * 调用前可通过 c_agent_image_set_size() 和 c_agent_image_set_format() 配置输出参数。
 *
 * @param mode  生成模式，取值为 C_AGENT_IMAGE_MODE_TEXT / IMAGE / DRAW
 *
 * @return int32_t  UTIL_SUCCESS=成功，失败返回相应错误码
 *
 * @note 该函数为异步触发，返回后图像生成仍在进行，需配合事件回调系统接收结果
 */
int32_t c_agent_image_start(uint8_t mode);

/**
 * @brief 启动图像生成 Agent（v2，支持自定义提示词变量）
 *
 * 功能与 c_agent_image_start() 相同，区别在于 user_prompt_params 由调用方传入，
 * 可携带生图 Agent 提示词中自定义的变量（参见百炼文档《语音请求直通图像生成Agent》）https://help.aliyun.com/zh/model-studio/audio-to-generateimgagent。
 * 调用前可通过 c_agent_image_set_size() 和 c_agent_image_set_format() 配置输出参数。
 *
 * 传入的 user_prompt_params 示例：
 * {
 *     "positive": {
 *         "query": ""        // 语音直通场景下 query 必须为空字符串，
 *                            // 系统会将 ASR 结果自动注入到该变量
 *     },
 *     "negative": {          // 可选，反向提示词变量
 *     }
 * }
 *
 * @param mode                生成模式，取值为 C_AGENT_IMAGE_MODE_TEXT / IMAGE / DRAW
 * @param user_prompt_params  提示词自定义变量（cJSON 对象），不能为 NULL。
 *                            调用成功后所有权转移给 SDK，随 biz 参数统一管理，
 *                            调用方不得再释放或修改；调用失败时仍由调用方持有
 *
 * @return int32_t  UTIL_SUCCESS=成功；
 *                  UTIL_ERR_INVALID_PARAM=模式参数非法或 user_prompt_params 非法；
 *                  UTIL_ERR_NO_MEMORY=内存不足；
 *                  UTIL_ERR_UNEXPECT=biz 参数已存在（上一轮请求未下发完成），可稍后重试
 *
 * @note 该函数为异步触发，返回后图像生成仍在进行，需配合事件回调系统接收结果
 */
int32_t c_agent_image_start_v2(uint8_t mode, cJSON *user_prompt_params);

/**
 * @brief 结束图像生成 Agent
 *
 * 清空 biz 参数（user_defined_params 和 commands），通知 Agent 退出图像生成模式，
 * 使会话恢复到默认状态。
 *
 * @return int32_t  UTIL_SUCCESS=成功，失败返回相应错误码
 */
int32_t c_agent_image_end(void);

#ifdef __cplusplus
}
#endif

#endif /* __C_AGENT_IMAGE_H__ */
