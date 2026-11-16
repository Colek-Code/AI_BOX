/**
 * @file d_auth_code.c
 * @brief 校验杰理短认证记录的长度与 CRC，并复制有界载荷。
 */
#include "d_auth_code.h"
#include <stddef.h>
#include <string.h>

/**
 * @brief 按杰理短记录算法计算长度字段及载荷的 CRC16。
 * @note 算法来源为已核实的 isd_download 4.2.74，不推断其他版本的格式兼容性。
 * @param[in] data 包含两字节长度及载荷的有效输入。
 * @param[in] length 参与计算的字节数。
 * @return 多项式 0x1021、初值 0、无反射且无最终异或的 16 位校验值。
 */
static uint16_t d_auth_code_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0U;
    uint16_t index;
    uint8_t bit;
    for (index = 0U; index < length; index++) {
        crc ^= (uint16_t)((uint16_t)data[index] << 8);
        for (bit = 0U; bit < 8U; bit++) {
            crc = (uint16_t)((crc & 0x8000U) ? ((uint32_t)crc << 1) ^ 0x1021U : (uint32_t)crc << 1);
        }
    }
    return crc;
}

/**
 * @brief 校验并复制原始记录；完整合同见 d_auth_code.h。
 */
d_auth_code_result_e d_auth_code_decode(const uint8_t *record,
                                      uint16_t record_size,
                                      d_auth_code_value_t *output)
{
    uint16_t size;
    uint16_t expected_crc;
    if (output == NULL) return D_AUTH_CODE_ARGUMENT;
    output->length = 0U;
    if (record == NULL) return D_AUTH_CODE_ARGUMENT;
    if (record_size < D_AUTH_CODE_HEADER_SIZE) return D_AUTH_CODE_LENGTH;
    expected_crc = (uint16_t)(record[0] | ((uint16_t)record[1] << 8));
    size = (uint16_t)(record[2] | ((uint16_t)record[3] << 8));
    if ((expected_crc == 0xffffU && size == 0xffffU) || size == 0U) {
        return D_AUTH_CODE_EMPTY;
    }
    if (size > D_AUTH_CODE_MAX_LENGTH || size > record_size - D_AUTH_CODE_HEADER_SIZE) {
        return D_AUTH_CODE_LENGTH;
    }
    if (d_auth_code_crc16(record + 2U, (uint16_t)(size + 2U)) != expected_crc) {
        return D_AUTH_CODE_CRC;
    }
    memcpy(output->data, record + D_AUTH_CODE_HEADER_SIZE, size);
    output->length = size;
    return D_AUTH_CODE_OK;
}
