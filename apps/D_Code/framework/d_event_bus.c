/**
 * @file    d_event_bus.c
 * @brief   D_Code 发布-订阅事件总线 — 实现
 *
 * 核心机制：
 *   - 静态订阅表 s_sub_table[]，按实际事件 ID 建槽
 *   - d_event_publish()：同步发布，先拷贝订阅者快照再回调
 *   - d_event_post()：浅拷贝指定字节到环形缓冲区，信号量通知分发任务
 *   - d_event_dispatcher_task()：异步取队列，先拷贝快照再派发
 *
 * 适配杰理 SDK API：
 *   - os_mutex_create/pend/post  → 互斥锁
 *   - os_sem_create/pend/post    → 信号量（队列通知）
 *   - thread_fork                → 创建任务
 */

#include "d_event_bus.h"
#include "d_log.h"
#include "system/includes.h"
#include "os/os_api.h"
#include <string.h>

#define TAG "EVT_BUS"

//*********************************************************************************//
//                              内部数据结构                                        //
//*********************************************************************************//

/* 单个订阅者 */
typedef struct {
    d_event_handler_t handler;
    void             *user_data;
} subscriber_t;

/* 每个事件 ID 的订阅者列表 */
typedef struct {
    subscriber_t subs[D_EVENT_MAX_SUBSCRIBERS];
    uint8_t      count;
} event_sub_list_t;

/* 事件 ID 订阅表项 */
typedef struct {
    d_event_id_t      id;
    uint8_t           in_use;
    event_sub_list_t  list;
} event_sub_entry_t;

/* 异步事件队列项：只保留指定字节，不管理载荷内指针引用的对象。 */
typedef struct {
    d_event_id_t id;
    uint16_t     data_len;
    uint8_t      data[D_EVENT_DATA_MAX];
} event_queue_item_t;

//*********************************************************************************//
//                              静态变量                                            //
//*********************************************************************************//

/* 订阅表：按实际事件 ID 建槽 */
#define SUB_TABLE_SIZE  64

static event_sub_entry_t s_sub_table[SUB_TABLE_SIZE];
static OS_MUTEX         s_mutex;
static int              s_initialized = 0;

/* 异步事件环形缓冲 + 信号量 */
static event_queue_item_t s_ring_buf[D_EVENT_QUEUE_SIZE];
static volatile int       s_ring_head = 0;  /* 写入位置 */
static volatile int       s_ring_tail = 0;  /* 读取位置 */
static OS_SEM             s_ring_sem;

//*********************************************************************************//
//                              内部函数                                            //
//*********************************************************************************//

/* 检查环形缓冲是否满 */
static inline int ring_is_full(void)
{
    return ((s_ring_head + 1) % D_EVENT_QUEUE_SIZE) == s_ring_tail;
}

/* 检查环形缓冲是否空 */
static inline int ring_is_empty(void)
{
    return s_ring_head == s_ring_tail;
}

/* 查找事件 ID 对应的订阅表项 */
static event_sub_entry_t *find_event_entry(d_event_id_t id, int create)
{
    event_sub_entry_t *free_entry = NULL;
    int i;

    for (i = 0; i < SUB_TABLE_SIZE; i++) {
        if (s_sub_table[i].in_use) {
            if (s_sub_table[i].id == id) {
                return &s_sub_table[i];
            }
        } else if (!free_entry) {
            free_entry = &s_sub_table[i];
        }
    }

    if (!create || !free_entry) {
        return NULL;
    }

    memset(free_entry, 0, sizeof(*free_entry));
    free_entry->id = id;
    free_entry->in_use = 1;
    return free_entry;
}

/* 锁内取得订阅者快照，锁外回调；取消订阅不能撤销已取得的快照。 */
static void dispatch_event(const event_queue_item_t *item)
{
    d_event_t evt;
    evt.id       = item->id;
    evt.data     = (item->data_len > 0) ? item->data : NULL;
    evt.data_len = item->data_len;
    subscriber_t notify_list[D_EVENT_MAX_SUBSCRIBERS];
    uint8_t notify_count = 0;

    os_mutex_pend(&s_mutex, 0);

    event_sub_entry_t *entry = find_event_entry(item->id, 0);
    if (!entry) {
        os_mutex_post(&s_mutex);
        return;
    }

    event_sub_list_t *list = &entry->list;

    notify_count = list->count;
    if (notify_count > D_EVENT_MAX_SUBSCRIBERS) {
        notify_count = D_EVENT_MAX_SUBSCRIBERS;
    }
    memcpy(notify_list, list->subs, notify_count * sizeof(subscriber_t));

    os_mutex_post(&s_mutex);

    int i;
    for (i = 0; i < notify_count; i++) {
        if (notify_list[i].handler) {
            notify_list[i].handler(&evt, notify_list[i].user_data);
        }
    }
}

/* 事件分发任务（处理异步投递的事件） */
static void d_event_dispatcher_task(void *priv)
{
    event_queue_item_t item;
    int has_event;

    LOGI(TAG, "事件分发任务启动");

    while (1) {
        /* 阻塞等待信号量通知 */
        os_sem_pend(&s_ring_sem, 0);

        /* 取出所有待处理事件 */
        while (1) {
            has_event = 0;
            OS_ENTER_CRITICAL();
            if (!ring_is_empty()) {
                memcpy(&item, &s_ring_buf[s_ring_tail], sizeof(item));
                s_ring_tail = (s_ring_tail + 1) % D_EVENT_QUEUE_SIZE;
                has_event = 1;
            }
            OS_EXIT_CRITICAL();

            if (!has_event) {
                break;
            }
            dispatch_event(&item);
        }
    }
}

//*********************************************************************************//
//                              公共 API                                            //
//*********************************************************************************//

d_err_t d_event_bus_init(void)
{
    int ret;

    if (s_initialized) {
        return D_OK;
    }

    memset(s_sub_table, 0, sizeof(s_sub_table));

    if (os_mutex_create(&s_mutex) != OS_NO_ERR) {
        LOGE(TAG, "事件总线互斥锁创建失败");
        return D_ERR_IO;
    }
    if (os_sem_create(&s_ring_sem, 0) != OS_NO_ERR) {
        LOGE(TAG, "事件总线信号量创建失败");
        os_mutex_del(&s_mutex, OS_DEL_ALWAYS);
        return D_ERR_IO;
    }

    s_ring_head = 0;
    s_ring_tail = 0;

    /* 创建事件分发任务 */
    ret = thread_fork("d_evt_disp", 10, 1024, 0, NULL, d_event_dispatcher_task, NULL);
    if (ret != OS_NO_ERR) {
        LOGE(TAG, "事件分发任务创建失败: %d", ret);
        os_sem_del(&s_ring_sem, OS_DEL_ALWAYS);
        os_mutex_del(&s_mutex, OS_DEL_ALWAYS);
        return D_ERR_IO;
    }

    s_initialized = 1;
    LOGI(TAG, "事件总线初始化完成 (max_subs=%d, queue=%d)", D_EVENT_MAX_SUBSCRIBERS, D_EVENT_QUEUE_SIZE);
    return D_OK;
}

int d_event_subscribe(d_event_id_t id, d_event_handler_t handler, void *user_data)
{
    if (!s_initialized || !handler) return -1;

    os_mutex_pend(&s_mutex, 0);

    event_sub_entry_t *entry = find_event_entry(id, 1);
    if (!entry) {
        os_mutex_post(&s_mutex);
        LOGE(TAG, "订阅失败: 事件表已满 (id=0x%04x)", id);
        return -1;
    }

    event_sub_list_t *list = &entry->list;

    /* 检查是否已满 */
    if (list->count >= D_EVENT_MAX_SUBSCRIBERS) {
        os_mutex_post(&s_mutex);
        LOGE(TAG, "订阅失败: 事件 0x%04x 订阅者已满(%d)", id, D_EVENT_MAX_SUBSCRIBERS);
        return -1;
    }

    /* 检查重复订阅 */
    int i;
    for (i = 0; i < list->count; i++) {
        if (list->subs[i].handler == handler) {
            os_mutex_post(&s_mutex);
            LOGW(TAG, "重复订阅: 事件 0x%04x", id);
            return 0;
        }
    }

    /* 添加订阅者 */
    list->subs[list->count].handler   = handler;
    list->subs[list->count].user_data = user_data;
    list->count++;

    os_mutex_post(&s_mutex);
    LOGD(TAG, "订阅成功: 事件 0x%04x (%d/%d)", id, list->count, D_EVENT_MAX_SUBSCRIBERS);
    return 0;
}

int d_event_unsubscribe(d_event_id_t id, d_event_handler_t handler)
{
    if (!s_initialized || !handler) return -1;

    os_mutex_pend(&s_mutex, 0);

    event_sub_entry_t *entry = find_event_entry(id, 0);
    if (!entry) {
        os_mutex_post(&s_mutex);
        return -1;
    }

    event_sub_list_t *list = &entry->list;

    int i;
    for (i = 0; i < list->count; i++) {
        if (list->subs[i].handler == handler) {
            /* 用最后一个填充空位 */
            list->count--;
            if (i < list->count) {
                list->subs[i] = list->subs[list->count];
            }
            memset(&list->subs[list->count], 0, sizeof(subscriber_t));

            if (list->count == 0) {
                memset(entry, 0, sizeof(*entry));
            }

            os_mutex_post(&s_mutex);
            LOGD(TAG, "取消订阅: 事件 0x%04x", id);
            return 0;
        }
    }

    os_mutex_post(&s_mutex);
    return -1;
}

void d_event_publish(d_event_id_t id, const void *data, uint16_t data_len)
{
    d_event_t evt;
    evt.id       = id;
    evt.data     = data;
    evt.data_len = data_len;
    subscriber_t notify_list[D_EVENT_MAX_SUBSCRIBERS];
    uint8_t notify_count = 0;

    if (!s_initialized) {
        return;
    }

    os_mutex_pend(&s_mutex, 0);

    event_sub_entry_t *entry = find_event_entry(id, 0);
    if (!entry) {
        os_mutex_post(&s_mutex);
        return;
    }

    event_sub_list_t *list = &entry->list;

    notify_count = list->count;
    if (notify_count > D_EVENT_MAX_SUBSCRIBERS) {
        notify_count = D_EVENT_MAX_SUBSCRIBERS;
    }

    memcpy(notify_list, list->subs, notify_count * sizeof(subscriber_t));

    os_mutex_post(&s_mutex);

    int i;
    for (i = 0; i < notify_count; i++) {
        if (notify_list[i].handler) {
            notify_list[i].handler(&evt, notify_list[i].user_data);
        }
    }
}

void d_event_post(d_event_id_t id, const void *data, uint16_t data_len)
{
    event_queue_item_t *item;

    if (!s_initialized) {
        return;
    }

    if (data_len > D_EVENT_DATA_MAX) {
        LOGE(TAG, "d_event_post: 数据过大 (%d > %d)", data_len, D_EVENT_DATA_MAX);
        return;
    }

    OS_ENTER_CRITICAL();
    if (ring_is_full()) {
        OS_EXIT_CRITICAL();
        LOGE(TAG, "d_event_post: 队列已满, 事件 0x%04x 丢弃", id);
        return;
    }

    item = &s_ring_buf[s_ring_head];
    item->id       = id;
    item->data_len = data_len;

    if (data && data_len > 0) {
        memcpy(item->data, data, data_len);
    } else {
        item->data_len = 0;
    }

    s_ring_head = (s_ring_head + 1) % D_EVENT_QUEUE_SIZE;
    OS_EXIT_CRITICAL();

    /* 通知分发任务 */
    os_sem_post(&s_ring_sem);
}
