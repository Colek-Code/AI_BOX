/** @file product_ui_alarm_tool.h @brief 本地闹钟工具的产品组合端口与显式参数校验接口。 */
#ifndef PRODUCT_UI_ALARM_TOOL_H
#define PRODUCT_UI_ALARM_TOOL_H
#include "modules/integrations/qwen/qwen_local_tool.h"
#include "product_ui_alarm_service.h"
/**
 * @brief 借用 AI 闹钟功能下的静态本地工具端口描述。
 * @return 指向静态只读端口，不由调用方释放。
 * @note 仅由 Qwen worker 串行调用其 start／poll／cancel，不是 UI 缓存或可并发多调用的服务。
 */
const qwen_local_tool_port_t *product_ui_alarm_tool_port(void);
/**
 * @brief 读取参数中的非负十进制 int32_t，拒绝布尔种类、空值及溢出。
 * @param[in] call 借用的已解析工具调用。
 * @param[in] key 待查参数键。
 * @param[out] value 调用方保证非空，成功时接收 0 至 INT32_MAX。
 * @return 合法且写入为 1，缺参数或格式无效为 0。
 * @note 不接受负号、正号、小数或指数；失败不改输出。
 */
int product_ui_alarm_tool_integer(const qwen_tool_call_t *call, const char *key, int32_t *value);
/**
 * @brief 读取 true／false 或 1／0 文本参数。
 * @param[in] call 借用的已解析工具调用。
 * @param[in] key 待查参数键。
 * @param[out] value 调用方保证非空，成功接收归一化 0／1，失败不改。
 * @return 成功解析为 1，缺失或不匹配为 0。
 */
int product_ui_alarm_tool_boolean(const qwen_tool_call_t *call, const char *key, uint8_t *value);
/**
 * @brief 按显式时间、日期模式及周重复参数生成受限闹钟编辑命令。
 * @param[in] call 已解析且参数类型有界的工具调用。
 * @param[in] clock 非空时钟快照，必须 valid。
 * @param[in] previous 更新时借用旧记录，新建为 NULL。
 * @param[out] command 非空命令输出；失败可能已部分填写，不得提交。
 * @return 成功为 D_OK，无可信时钟为 NOT_READY，参数、既有双周规则或可执行日期不支持为 INVALID_ARG。
 * @note 不提交服务或写存储；更新单次未指定日期模式时保留旧绝对日期，不能默认为下一次而改变原语义。
 */
d_err_t product_ui_alarm_tool_rule(const qwen_tool_call_t *call, const product_ui_clock_snapshot_t *clock,
    const product_ui_alarm_record_t *previous, product_ui_alarm_command_t *command);
#endif
