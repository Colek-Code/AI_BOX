/** @file qwen_full_credential.h @brief 全托管 License 首次注册与断电恢复的设备存储边界。 */
#ifndef QWEN_FULL_CREDENTIAL_H
#define QWEN_FULL_CREDENTIAL_H
#include "framework/d_error.h"
#include <stdint.h>

#define QWEN_FULL_VM_INTENT 34U
#define QWEN_FULL_VM_CERT   35U
#define QWEN_FULL_FORMAT_110 0x010100U
#define QWEN_FULL_FORMAT_131 0x010301U

/** @brief 当前固件是否为使用独立证书区的全托管产品配置。 */
int qwen_full_credential_enabled(void);
/** @brief 在 SDK 启动前恢复独立证书；独立槽均空时继续检查旧 VM16、33。
 * @param[in] identity 注册归属摘要，32 字节。
 * @param[in] format SDK 序列化格式。
 * @return D_OK 可启动；有未完成注册、损坏、未知格式或身份冲突时返回错误并保留原记录。
 * @note open、reserve、commit 由 Qwen 唯一工作任务串行调用，均可能访问 Flash。
 */
d_err_t qwen_full_credential_open(const uint8_t identity[32], uint32_t format);
/** @brief 使用原物理 16、33 的产品在 SDK 初始化前核对并载入凭据。 */
d_err_t qwen_legacy_credential_open(const uint8_t identity[32]);
/** @brief 本次准备的独立存储允许发起首次注册。由唯一工作任务查询。 */
int qwen_full_credential_can_register(void);
/** @brief 首次发送注册请求前，将本次申请标识写入 Flash 并读回。已经申请过的身份不能再次认领。 */
d_err_t qwen_full_credential_reserve(void);
/** @brief 已核验意图后收到取消且尚未尝试 HTTP 首字节写入，仅当前启动允许复用该意图。 */
void qwen_full_credential_defer_unsent(void);
/** @brief 持久化记录已完整恢复时返回非零，此时 SDK 身份不符须停止。工作任务读取。 */
int qwen_full_credential_restored(void);
/** @brief 官方 SDK 已确认注册、设备凭据与归属已导出后保存快照并回读。 */
d_err_t qwen_full_credential_commit(void);
/** @brief SDK/归属回调读取当前运行副本。逻辑槽 16 或 33；成功返回实际长度。 */
int qwen_full_credential_read(uint16_t id, void *bytes, uint16_t capacity);
/** @brief SDK/归属回调暂存本次运行的数据，成功返回写入长度；挂起和最终提交由工作任务持有。 */
int qwen_full_credential_write(uint16_t id, const void *bytes, uint16_t length);
#endif
