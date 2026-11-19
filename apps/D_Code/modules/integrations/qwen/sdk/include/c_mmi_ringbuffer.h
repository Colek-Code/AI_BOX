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

#ifndef __C_MMI_RINGBUFFER_H__
#define __C_MMI_RINGBUFFER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_utils.h"

/** @brief 缓冲区实例角色（录音上行 / 播放下行） */
enum {
    C_MMI_RINGBUFFER_RECORDER,       /**< 录音上行缓冲 */
    C_MMI_RINGBUFFER_PLAYER,         /**< 播放下行缓冲 */
};

/** @brief 写满时的行为模式 */
enum {
    C_MMI_RINGBUFFER_WAIT_FOR_WRITE, /**< 写满就等读方腾出空间（最多等 MMI_RB_WAIT_TIME_OUT_MS） */
    C_MMI_RINGBUFFER_OVERWRITE,      /**< 写满就丢掉最旧的一帧腾地方 */
    C_MMI_RINGBUFFER_DROP,           /**< 保留值，当前 put 不支持，传入会被拒绝 */
};

/** @brief c_mmi 环形缓冲区实例：字节环 + 可选分帧链表 + 一把外层锁 */
typedef struct _c_mmi_ringbuffer_t {
    util_ringbuffer_t *rb;      /**< 底层字节环，存实际音频字节 */
    util_list_t* list;          /**< 分帧链表，list_flag=1 时使用，每节点记录一帧大小与是否完整 */
    util_mutex_t *mutex;        /**< 外层锁，保护本实例所有会变的数据；put/get/reset 全程持有 */
    uint8_t write_mode;         /**< 写满行为：WAIT_FOR_WRITE 或 OVERWRITE */
    uint8_t list_flag;          /**< 1=分帧（用链表），0=直通（纯字节环） */
    volatile uint8_t stopping;  /**< 置 1 后 WAIT_FOR_WRITE 等空间的循环立即退出，供 deinit 前收走在途 put */
    uint8_t reserved;           // no use, for align
} c_mmi_ringbuffer_t;

/**
 * @brief 创建一个环形缓冲区实例
 *
 * @param size 字节环总容量（字节），为 0 时返回 NULL
 * @param write_mode 写满行为，取 C_MMI_RINGBUFFER_WAIT_FOR_WRITE 或 C_MMI_RINGBUFFER_OVERWRITE
 * @param list_flag 1=分帧模式（额外建链表按帧存取），0=直通模式（纯字节环按字节存取）
 * @return 成功返回实例指针；任一步失败会走完整 deinit 清理并返回 NULL
 */
c_mmi_ringbuffer_t *c_mmi_ringbuffer_init(uint32_t size, uint8_t write_mode, uint8_t list_flag);

/**
 * @brief 销毁一个环形缓冲区实例，对 NULL 安全
 *
 * @param rb 实例指针，允许为 NULL（直接返回成功）
 * @return 恒返回 UTIL_SUCCESS
 * @note 本函数不加 rb 锁（正在删除的就是这把锁）。销毁与 put/get/reset 的排他由调用方保证：
 *       须在这些调用全部退出后才 deinit（c_mmi 层用 ref_count 引用计数保证）。
 */
int32_t c_mmi_ringbuffer_deinit(c_mmi_ringbuffer_t *rb);

/**
 * @brief 请求缓冲区停止接收写入：让卡在 WAIT_FOR_WRITE 等空间循环里的在途 put 立刻退出
 *
 * @param rb 实例指针，允许为 NULL（直接忽略）
 * @note 供 c_mmi 层 deinit 前调用。置停止标志后，在途 put 会在下一次循环判断时返回 0、退出对引用计数的占用，
 *       deinit 的引用计数等待得以尽快归零后再释放本实例。标志置位后不复位：本实例进入拆除流程即不再接受写入。
 */
void c_mmi_ringbuffer_request_stop(c_mmi_ringbuffer_t *rb);

/**
 * @brief 写入一段数据
 *
 * @param rb 实例指针
 * @param data 待写入数据
 * @param size 写入字节数，为 0 或超过字节环总容量时返回 0
 * @param finish_flag 分帧模式下标记这一帧是否已完整（1=完整，0=后面还有分片要续写）
 * @return 实际写入字节数；参数非法、写满等不到空间超时、或申请节点失败时返回 0
 * @note 整个写入过程持外层锁 rb->mutex。C_MMI_RINGBUFFER_WAIT_FOR_WRITE 模式在锁外直接读
 *       “剩余可写字节够不够”，强依赖“只有一个写方”：多写方可能同时判定够写再先后写入，
 *       被字节环内层的覆盖开关静默覆盖而丢数据。多写方仅 C_MMI_RINGBUFFER_OVERWRITE 模式安全。
 */
uint32_t c_mmi_ringbuffer_put(c_mmi_ringbuffer_t *rb, uint8_t *data, uint32_t size, uint8_t finish_flag);

/**
 * @brief 读出一段数据
 *
 * @param rb 实例指针
 * @param data 接收缓冲
 * @param size 接收缓冲容量（字节）
 * @return 实际读出字节数；参数非法、缓冲区空、最旧一帧未完整、或接收缓冲小于该帧时返回 0
 * @note 整个过程持外层锁 rb->mutex。分帧模式下读字节和摘链表节点在同一段持锁代码里完成。
 */
uint32_t c_mmi_ringbuffer_get(c_mmi_ringbuffer_t *rb, uint8_t *data, uint32_t size);

/**
 * @brief 清空缓冲区（字节环与分帧链表一起清）
 *
 * @param rb 实例指针
 * @return 成功返回 UTIL_SUCCESS；rb 或字节环为空返回 UTIL_ERR_INVALID_PARAM；拿不到锁返回 UTIL_ERR_BUSY
 * @note 整个清空过程持外层锁 rb->mutex，清字节环与清链表一次做完，不会出现清一半的中间态被 put/get 看到。
 */
int32_t c_mmi_ringbuffer_reset(c_mmi_ringbuffer_t *rb);

/**
 * @brief 自测入口：跑一遍功能/边界/并发/泄漏用例，结果打到日志（ALL PASS 或 FAIL 计数）
 */
void c_mmi_ringbuffer_test(void);

#ifdef __cplusplus
}
#endif

#endif
