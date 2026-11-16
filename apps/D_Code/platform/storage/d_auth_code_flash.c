/**
 * @file d_auth_code_flash.c
 * @brief 从已核实的 16 MB NOR 末尾位置只读获取杰理短记录。
 */
#include "config/app_config.h"
#if D_AUTH_CODE_READ_ENABLE
#include "d_auth_code.h"
#include "asm/sfc_norflash_api.h"
#include "ioctl_cmds.h"
#include <stddef.h>
#include <string.h>

/**
 * @brief 只读 NOR 原始记录并在返回前清空栈上副本；完整合同见 d_auth_code.h。
 */
d_auth_code_result_e d_auth_code_read(d_auth_code_value_t *output,
                                    d_auth_code_read_info_t *info)
{
    uint8_t record[D_AUTH_CODE_RECORD_SIZE] = {0};
    d_auth_code_read_info_t observed = {0U, 0U, -1};
    d_auth_code_result_e result = D_AUTH_CODE_OPEN;
    if (info) *info = observed;
    if (!output) return D_AUTH_CODE_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (norflash_open(NULL, NULL, NULL) != 0) goto finish;
    result = D_AUTH_CODE_IO;
    if (norflash_ioctl(NULL, IOCTL_GET_CAPACITY, (u32)&observed.capacity) != 0) goto finish;
    result = D_AUTH_CODE_CAPACITY;
    if (observed.capacity != (uint32_t)__FLASH_SIZE__ ||
        observed.capacity != 16U * 1024U * 1024U) goto finish;
    observed.address = observed.capacity - D_AUTH_CODE_TAIL_DISTANCE;
    observed.read_result = norflash_origin_read(record, observed.address, sizeof(record));
    /* 已核实的 AC79 V1.2.12 实现返回请求长度而隐藏内部 SPI 结果；
     * 不能仅凭字节数判定读成功，仍须用解码器验证 CRC。 */
    result = D_AUTH_CODE_IO;
    if (observed.read_result != (int)sizeof(record)) goto finish;
    result = d_auth_code_decode(record, sizeof(record), output);
finish:
    memset(record, 0, sizeof(record));
    if (info) *info = observed;
    return result;
}
#endif
