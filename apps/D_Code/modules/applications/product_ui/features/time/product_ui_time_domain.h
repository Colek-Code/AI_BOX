/** @file product_ui_time_domain.h @brief 无 LVGL 依赖的有限时间、闹钟桥接及日程 RAM 域；调用方负责串行。 */

#ifndef PRODUCT_UI_TIME_DOMAIN_H
#define PRODUCT_UI_TIME_DOMAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRODUCT_UI_TIME_DOMAIN_MAX_ALARMS             16U
#define PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS              64U
#define PRODUCT_UI_TIME_DOMAIN_ALARM_LABEL_SIZE        24U
#define PRODUCT_UI_TIME_DOMAIN_EVENT_TITLE_SIZE        40U
#define PRODUCT_UI_TIME_DOMAIN_EVENT_LOCATION_SIZE     24U
#define PRODUCT_UI_TIME_DOMAIN_MAX_YEAR               9999U

#define PRODUCT_UI_TIME_WEEKDAY_SUNDAY    (1U << 0)
#define PRODUCT_UI_TIME_WEEKDAY_MONDAY    (1U << 1)
#define PRODUCT_UI_TIME_WEEKDAY_TUESDAY   (1U << 2)
#define PRODUCT_UI_TIME_WEEKDAY_WEDNESDAY (1U << 3)
#define PRODUCT_UI_TIME_WEEKDAY_THURSDAY  (1U << 4)
#define PRODUCT_UI_TIME_WEEKDAY_FRIDAY    (1U << 5)
#define PRODUCT_UI_TIME_WEEKDAY_SATURDAY  (1U << 6)
#define PRODUCT_UI_TIME_WEEKDAY_ALL       0x7FU

typedef enum {
    PRODUCT_UI_TIME_OK = 0,
    PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT = -1,
    PRODUCT_UI_TIME_ERR_UNAVAILABLE = -2,
    PRODUCT_UI_TIME_ERR_NOT_FOUND = -3,
    PRODUCT_UI_TIME_ERR_CAPACITY = -4,
    PRODUCT_UI_TIME_ERR_CONFLICT = -5,
} product_ui_time_result_e;

typedef enum {
    PRODUCT_UI_TIME_CAPABILITY_UNKNOWN = 0,
    PRODUCT_UI_TIME_CAPABILITY_UNAVAILABLE,
    PRODUCT_UI_TIME_CAPABILITY_AVAILABLE,
} product_ui_time_capability_e;

typedef enum {
    PRODUCT_UI_TIME_UNAVAILABLE_NONE = 0,
    PRODUCT_UI_TIME_UNAVAILABLE_NOT_PROBED,
    PRODUCT_UI_TIME_UNAVAILABLE_NOT_SUPPORTED,
    PRODUCT_UI_TIME_UNAVAILABLE_NOT_READY,
    PRODUCT_UI_TIME_UNAVAILABLE_SOURCE_INVALID,
} product_ui_time_unavailable_reason_e;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
} product_ui_date_t;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} product_ui_time_of_day_t;

typedef struct {
    product_ui_date_t date;
    product_ui_time_of_day_t time;
} product_ui_datetime_t;

typedef enum {
    PRODUCT_UI_ALARM_SCHEDULE_ONCE = 0,
    PRODUCT_UI_ALARM_SCHEDULE_WEEKLY,
    PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT,
} product_ui_alarm_schedule_e;

/** 持久化的作息模板身份，不以视图菜单下标作为存储值。 */
typedef enum {
    PRODUCT_UI_ALARM_ROUTINE_BASIC = 0,
    PRODUCT_UI_ALARM_ROUTINE_DOUBLE_REST = 1,
    PRODUCT_UI_ALARM_ROUTINE_SINGLE_REST = 2,
    PRODUCT_UI_ALARM_ROUTINE_ALTERNATING = 3,
} product_ui_alarm_routine_e;

typedef struct {
    uint32_t id;
    uint8_t enabled;
    uint8_t alternate_mask;
    uint8_t routine_template;
    product_ui_alarm_schedule_e schedule;
    /**< 单次为执行日期，双周为第一周周一，每周计划忽略此字段。 */
    product_ui_date_t date;
    uint8_t hour;
    uint8_t minute;
    uint8_t repeat_mask;
    char label[PRODUCT_UI_TIME_DOMAIN_ALARM_LABEL_SIZE];
} product_ui_alarm_t;

typedef struct {
    uint32_t id;
    product_ui_datetime_t start;
    product_ui_datetime_t end;
    uint8_t all_day;
    uint8_t completed;
    char title[PRODUCT_UI_TIME_DOMAIN_EVENT_TITLE_SIZE];
    char location[PRODUCT_UI_TIME_DOMAIN_EVENT_LOCATION_SIZE];
} product_ui_calendar_event_t;

/** 值快照始终可按结构读取。墙钟有效要求 capability 为 AVAILABLE；
 * 闹钟运行时还要求 wall_time_valid。无墙钟时闹钟及日程记录仍可读取；
 * alarm_runtime_available 为零时不可使用 next_alarm 字段。 */
typedef struct {
    uint32_t generation;
    product_ui_time_capability_e capability;
    product_ui_time_unavailable_reason_e unavailable_reason;
    uint8_t wall_time_valid;
    uint8_t alarm_runtime_available;
    product_ui_datetime_t wall_time;
    uint8_t next_alarm_valid;
    uint32_t next_alarm_id;
    product_ui_datetime_t next_alarm_at;
    uint8_t alarm_count;
    uint8_t event_count;
    product_ui_alarm_t alarms[PRODUCT_UI_TIME_DOMAIN_MAX_ALARMS];
    product_ui_calendar_event_t events[PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS];
} product_ui_time_domain_snapshot_t;

typedef struct {
    uint32_t generation;
    product_ui_time_capability_e capability;
    product_ui_time_unavailable_reason_e unavailable_reason;
    uint8_t wall_time_valid;
    uint8_t alarm_runtime_available;
    product_ui_datetime_t wall_time;
    uint8_t next_alarm_valid;
    uint32_t next_alarm_id;
    product_ui_datetime_t next_alarm_at;
    uint8_t alarm_count;
    uint8_t event_count;
} product_ui_time_domain_summary_t;

typedef enum {
    PRODUCT_UI_TIME_COMMAND_RESET = 0,
    PRODUCT_UI_TIME_COMMAND_SET_CAPABILITY,
    PRODUCT_UI_TIME_COMMAND_SET_WALL_TIME,
    PRODUCT_UI_TIME_COMMAND_INVALIDATE_WALL_TIME,
    PRODUCT_UI_TIME_COMMAND_UPSERT_ALARM,
    PRODUCT_UI_TIME_COMMAND_REMOVE_ALARM,
    PRODUCT_UI_TIME_COMMAND_SET_ALARM_ENABLED,
    PRODUCT_UI_TIME_COMMAND_UPSERT_EVENT,
    PRODUCT_UI_TIME_COMMAND_REMOVE_EVENT,
    PRODUCT_UI_TIME_COMMAND_SET_EVENT_COMPLETED,
} product_ui_time_command_type_e;

typedef struct {
    product_ui_time_capability_e capability;
    product_ui_time_unavailable_reason_e unavailable_reason;
} product_ui_time_capability_command_t;

typedef struct {
    uint32_t id;
} product_ui_time_id_command_t;

typedef struct {
    uint32_t id;
    uint8_t enabled;
} product_ui_alarm_enabled_command_t;

typedef struct {
    uint32_t id;
    uint8_t completed;
} product_ui_event_completed_command_t;

typedef struct {
    product_ui_time_command_type_e type;
    union {
        product_ui_time_capability_command_t capability;
        product_ui_datetime_t wall_time;
        product_ui_alarm_t alarm;
        product_ui_time_id_command_t id;
        product_ui_alarm_enabled_command_t alarm_enabled;
        product_ui_calendar_event_t event;
        product_ui_event_completed_command_t event_completed;
    } data;
} product_ui_time_domain_command_t;

typedef struct {
    product_ui_time_result_e result;
    uint32_t generation;
    uint32_t affected_id;
} product_ui_time_domain_command_result_t;

/** 闹钟记录由运行时持有；纯 C 桥接只复制摘要，不拥有影子记录。 */
typedef struct {
    uint32_t generation;
    uint32_t next_id;
    product_ui_datetime_t next_at;
    uint8_t count;
    uint8_t runtime_available;
    uint8_t next_valid;
} product_ui_time_alarm_summary_t;

typedef struct {
    product_ui_time_result_e (*copy)(product_ui_alarm_t *alarms, product_ui_time_alarm_summary_t *summary);
    product_ui_time_result_e (*get)(uint8_t index, product_ui_alarm_t *alarm);
    product_ui_time_result_e (*execute)(const product_ui_time_domain_command_t *command, uint32_t *affected_id);
} product_ui_time_alarm_backend_t;

/**
 * @brief 绑定静态闹钟后端并重置已观察的闹钟代次。
 * @param[in] backend 借用的静态后端描述；NULL 清除绑定。
 * @note 仅在 UI／启动串行上下文使用；后端及回调所有者须覆盖所有页面生命周期，不在本域维护影子闹钟。
 */
void product_ui_time_domain_bind_alarms(const product_ui_time_alarm_backend_t *backend);

/**
 * @brief 重置 UI 拥有的墙钟能力及日程 RAM 缓存，保留已绑定的持久闹钟。
 * @note 无内部锁，调用方须串行组织命令及快照；init 和 RESET 均推进独立日程代次，避免活页面旧缓存命中。
 */
void product_ui_time_domain_init(void);

/**
 * @brief 同步执行一条有限状态变更并输出结果、当前代次及受影响编号。
 * @param[in] command 借用的命令值；不保留输入指针。
 * @param[out] result 可为空，非空时接收返回结果、代次与受影响编号。
 * @return 成功返回 TIME_OK；参数、能力、记录、容量或后端冲突返回对应时间域错误。
 * @note 无墙钟时仍可操作闹钟／日程；设置墙钟要求 AVAILABLE。成功日程操作即使同值也推进日程代次，失败不发布新代次。
 */
product_ui_time_result_e product_ui_time_domain_execute(
    const product_ui_time_domain_command_t *command,
    product_ui_time_domain_command_result_t *result);

/**
 * @brief 复制固定容量墙钟、闹钟与日程状态。
 * @param[out] snapshot 接收调用方拥有的完整值副本。
 * @return 成功返回 TIME_OK；空输出返回 INVALID_ARGUMENT。
 * @note 闹钟由后端拷贝；来源缺失时仍可读取本域日程。读取可能因观察到新的后端代次而推进总内容代次，不保证多次后端拷贝是同一原子快照。
 */
product_ui_time_result_e product_ui_time_domain_get_snapshot(
    product_ui_time_domain_snapshot_t *snapshot);
/**
 * @brief 读取不含记录数组的域摘要，并观察闹钟后端代次。
 * @param[out] summary 接收能力、有效时间、计数及可用的下次闹钟。
 * @return 成功返回 TIME_OK；空输出返回 INVALID_ARGUMENT。
 * @note 仍扫描固定容量日程槽统计数量；无锁且后端回调同步调用，所有调用须由 owner 串行。
 */
product_ui_time_result_e product_ui_time_domain_get_summary(
    product_ui_time_domain_summary_t *summary);
/**
 * @brief 从绑定闹钟后端复制指定逻辑下标的记录。
 * @param[in] index 闹钟后端定义的逻辑下标，不是持久编号。
 * @param[out] alarm 接收闹钟值副本，失败时不得使用。
 * @return 空输出返回 INVALID_ARGUMENT；无绑定返回 UNAVAILABLE；其余原样返回后端结果。
 */
product_ui_time_result_e product_ui_time_domain_get_alarm(
    uint8_t index, product_ui_alarm_t *alarm);
/**
 * @brief 按有效槽顺序复制指定逻辑下标的日程。
 * @param[in] index 从零开始的逻辑下标，不是物理槽或记录编号。
 * @param[out] event 接收日程值副本，失败时不写入。
 * @return 成功返回 TIME_OK；空输出返回 INVALID_ARGUMENT；没有对应条目返回 NOT_FOUND。
 */
product_ui_time_result_e product_ui_time_domain_get_event(
    uint8_t index, product_ui_calendar_event_t *event);

/**
 * @brief 读取仅由初始化／RESET 及成功日程变更推进的非零缓存代次。
 * @return 当前日程代次；只允许相等比较，不将大小或差值解释为事件数。
 * @note 内存读取不扫描记录、不执行 IO；墙钟及闹钟变化不影响它，沿用 UI 调用方串行合同。
 */
uint32_t product_ui_time_domain_get_event_revision(void);

/**
 * @brief 校验完整公历日期及二十四小时制时分秒。
 * @param[in] value 借用的日期时间值，年份为 1 至 MAX_YEAR。
 * @return 有效返回 TIME_OK；空指针、日期或时分秒越界返回 INVALID_ARGUMENT。
 */
product_ui_time_result_e product_ui_time_domain_validate_datetime(
    const product_ui_datetime_t *value);
/**
 * @brief 校验闹钟启用位、时分、周期掩码及相应计划约束。
 * @param[in] alarm 借用的闹钟值。
 * @return 符合计划合同返回 TIME_OK，否则返回 INVALID_ARGUMENT。
 * @note 单次必须有合法日期且无重复位；周计划兼容未使用的旧日期；双周要求交替模板、非空两周掩码及受支持范围内的首周周一。此处不校验标签文字。
 */
product_ui_time_result_e product_ui_time_domain_validate_alarm(
    const product_ui_alarm_t *alarm);
/**
 * @brief 校验日程布尔位、开始结束日期时间及先后关系。
 * @param[in] event 借用的日程值；本函数不校验或修改标题和地点。
 * @return 合法且开始不晚于结束返回 TIME_OK，否则返回 INVALID_ARGUMENT。
 */
product_ui_time_result_e product_ui_time_domain_validate_event(
    const product_ui_calendar_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* PRODUCT_UI_TIME_DOMAIN_H */
