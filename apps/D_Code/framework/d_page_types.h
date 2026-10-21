/**
 * @file d_page_types.h
 * @brief Shared LVGL page descriptors without module-system dependencies.
 */

#ifndef D_PAGE_TYPES_H
#define D_PAGE_TYPES_H

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "lvgl.h"

typedef struct {
    int (*create)(lv_obj_t *parent);
    void (*enter)(void);
    void (*exit)(void);
    void (*destroy)(void);
    void (*update)(uint32_t elapsed_ms);
    int (*gesture)(int gesture_type);
    uint32_t flags;
} page_ops_t;

typedef struct {
    int page_type;
    const char *name;
    const page_ops_t *ops;
    lv_obj_t *container;
    int is_created;
    int is_active;
} page_desc_t;

#define PAGE_TYPE_MAIN          0
#define PAGE_TYPE_INVALID       (-1)
#define PAGE_FLAG_KEEP_ALIVE    (1U << 0)
#define PAGE_FLAG_NO_GESTURE    (1U << 1)

#endif /* D_LVGL_ENABLE */
#endif /* D_PAGE_TYPES_H */
