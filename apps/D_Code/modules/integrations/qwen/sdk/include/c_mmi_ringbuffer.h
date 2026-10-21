/*
 * Copyright 2025 Alibaba Group Holding Ltd.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 *     http: *www.apache.org/licenses/LICENSE-2.0

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

enum {
    C_MMI_RINGBUFFER_RECORDER,
    C_MMI_RINGBUFFER_PLAYER,
};
enum {
    C_MMI_RINGBUFFER_WAIT_FOR_WRITE,
    C_MMI_RINGBUFFER_OVERWRITE,
    C_MMI_RINGBUFFER_DROP,
};

typedef struct _c_mmi_ringbuffer_t {
    util_ringbuffer_t *rb;
    util_list_t* list;
    util_mutex_t *mutex;
    uint8_t write_mode;
    uint8_t list_flag;
    uint16_t reserved;       // no use,for align 
} c_mmi_ringbuffer_t;

c_mmi_ringbuffer_t *c_mmi_ringbuffer_init(uint32_t size, uint8_t write_mode, uint8_t list_flag);
int32_t c_mmi_ringbuffer_deinit(c_mmi_ringbuffer_t *rb);
uint32_t c_mmi_ringbuffer_put(c_mmi_ringbuffer_t *rb, uint8_t *data, uint32_t size, uint8_t finish_flag);
uint32_t c_mmi_ringbuffer_get(c_mmi_ringbuffer_t *rb, uint8_t *data, uint32_t size);
int32_t c_mmi_ringbuffer_reset(c_mmi_ringbuffer_t *rb);
void c_mmi_ringbuffer_test(void);

#ifdef __cplusplus
}
#endif

#endif
