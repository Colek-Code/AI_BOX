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

#ifndef __C_AGENT_VISUAL_H__
#define __C_AGENT_VISUAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 视觉 Agent 数据类型定义
 * 
 * 用于指定传入视觉问答功能的图片数据格式：
 * - C_VISUAL_DATA_BASE64: 使用 Base64 编码图片数据，适用于直接传输二进制数据
 * - C_VISUAL_DATA_URL: 图片的 URL 地址，适用于云端图片引用
 */
enum {
    C_VISUAL_DATA_BASE64,   /**< Base64 编码的图片数据 */
    C_VISUAL_DATA_URL,      /**< 图片 URL 地址 */
};

/**
 * @brief 视觉问答（VQA - Visual Question Answering）功能接口
 * 
 * 该函数用于向视觉 Agent 提交图片数据和查询问题，实现图片理解与问答功能。
 * 支持两种数据传入方式：JPEG 图片数据（SDK内部会进行base64编码）或图片 URL。
 * 
 * @param query          查询问题字符串，用于描述用户想要了解的内容
 *                       例如："这张图片里有什么？"、"这是什么动物？"
 * 
 * @param data_type      数据类型标识，可选值：
 *                       - C_VISUAL_DATA_BASE64: 图片数据使用 Base64 编码进行传输
 *                       - C_VISUAL_DATA_URL: 使用图片 URL 地址
 * 
 * @param data           图片数据指针，根据 data_type 不同含义不同：
 *                       - 当 data_type=C_VISUAL_DATA_BASE64 时：指向 JPEG 图片数据
 *                       - 当 data_type=C_VISUAL_DATA_URL 时：指向包含 URL 的字符串
 * 
 * @param data_size      数据大小（字节数），对于 Base64 数据为 JPEG 图片数据的长度，
 *                       对于 URL 数据为字符串长度（包含结束符\0）
 * 
 * @return int32_t       执行结果：
 *                       - 0 或正数：表示成功，返回对话 ID 或状态码
 *                       - 负数：表示失败，具体错误码需参考错误定义
 * 
 * @note 使用示例：
 * @code
 * // 示例 1：使用 Base64 编码
 * char *image = load_image("photo.jpg");
 * uint32_t image_size = get_image_size(image);
 * c_agent_visual_vqa("这张图片里有什么？", C_VISUAL_DATA_BASE64, 
 *                    (uint8_t*)image, image_size);
 * 
 * // 示例 2：使用图片 URL
 * char *image_url = "https://example.com/image.jpg";
 * c_agent_visual_vqa("这是什么地方？", C_VISUAL_DATA_URL, 
 *                    (uint8_t*)image_url, strlen(image_url) + 1);
 * @endcode
 * 
 * @attention 
 * - 该函数会异步处理请求，返回后不代表问答已完成
 * - 需要配合事件回调系统接收问答结果
 * - URL 数据必须是完整的 HTTP/HTTPS 地址
 */
int32_t c_agent_visual_vqa(char *query, uint8_t data_type, uint8_t *data, uint32_t data_size);

#ifdef __cplusplus
}
#endif

#endif /* __C_AGENT_VISUAL_H__ */