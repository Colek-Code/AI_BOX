/**
 * @file d_page_types.h
 * @brief 定义共用的 LVGL 页面生命周期与描述符，不依赖模块注册实现。
 */

#ifndef D_PAGE_TYPES_H
#define D_PAGE_TYPES_H

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "lvgl.h"

typedef struct {
    int (*create)(lv_obj_t *parent); /**< 在所给父容器中同步创建内容；0 为成功，失败后仍会调用 destroy。 */
    void (*enter)(void);             /**< 容器显示后进入页面，不能嵌套导航。 */
    void (*exit)(void);              /**< 隐藏容器前退出页面，不能嵌套导航。 */
    void (*destroy)(void);           /**< 释放页面自有资源；框架随后删除页面容器。 */
    void (*update)(uint32_t elapsed_ms); /**< UI 周期更新；参数为经过的毫秒数。 */
    int (*gesture)(int gesture_type); /**< 处理输入层手势；框架当前不消费其返回值。 */
    uint32_t flags;                  /**< PAGE_FLAG_* 生命周期及手势策略位。 */
} page_ops_t;

typedef struct {
    int page_type;            /**< 注册表内唯一的页面编号。 */
    const char *name;          /**< 持续有效的借用诊断名称。 */
    const page_ops_t *ops;     /**< 持续有效的生命周期回调表，在 UI 线程执行。 */
    lv_obj_t *container;       /**< 框架拥有的页面容器；注册时强制置空。 */
    int is_created;            /**< 框架记录的创建完成事实。 */
    int is_active;             /**< 框架记录的当前激活事实。 */
} page_desc_t;

#define PAGE_TYPE_MAIN          0
#define PAGE_TYPE_INVALID       (-1)
#define PAGE_FLAG_KEEP_ALIVE    (1U << 0)
#define PAGE_FLAG_NO_GESTURE    (1U << 1)

#endif /* D_LVGL_ENABLE */
#endif /* D_PAGE_TYPES_H */
