/** @file d_auth_code_probe.c @brief 仅启动时运行的授权码只读回读测试。 */
#include "config/app_config.h"
#if D_DEBUG_AUTH_CODE_READBACK
#include "d_auth_code_probe.h"
#include "platform/storage/d_auth_code.h"
#include "framework/d_log.h"
#include <stddef.h>
#include <string.h>

#define TAG "AUTH_READBACK"

/* app_main 仅调用一次，缓冲没有异步消费者；
 * 普通构建不包含该诊断编译单元。 */
static d_auth_code_value_t s_auth_value;
static const uint8_t s_auth_marker_a[] = "DCAUTH0000000001";
static const uint8_t s_auth_marker_b[] = "DCAUTH0000000002";

/**
 * @brief 读取并分类合成标记匹配结果；完整合同见 d_auth_code_probe.h。
 */
void d_auth_code_probe_run(void)
{
    d_auth_code_read_info_t info;
    const char *marker = "OTHER";
    d_auth_code_result_e result;
    memset(&s_auth_value, 0, sizeof(s_auth_value));
    result = d_auth_code_read(&s_auth_value, &info);
    LOGI(TAG, "probe=2 format=JL-short capacity=%u address=0x%08x read_ret=%d result=%d",
         (unsigned int)info.capacity, (unsigned int)info.address, info.read_result, (int)result);
    if (result == D_AUTH_CODE_OK) {
        if (s_auth_value.length == sizeof(s_auth_marker_a) - 1U &&
            memcmp(s_auth_value.data, s_auth_marker_a, sizeof(s_auth_marker_a) - 1U) == 0) {
            marker = "A";
        } else if (s_auth_value.length == sizeof(s_auth_marker_b) - 1U &&
                   memcmp(s_auth_value.data, s_auth_marker_b, sizeof(s_auth_marker_b) - 1U) == 0) {
            marker = "B";
        }
        LOGI(TAG, "crc=PASS length=%u marker=%s match=%u",
             (unsigned int)s_auth_value.length, marker, (unsigned int)(marker[0] != 'O'));
    } else {
        LOGW(TAG, "readback=FAIL error=%d; no data written", (int)result);
    }
    /* 测试标记及现有真实授权码均不发送至云端。 */
    memset(&s_auth_value, 0, sizeof(s_auth_value));
}
#endif
