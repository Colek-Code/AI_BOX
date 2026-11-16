/**
 * @file d_auth_code.h
 * @brief 只读解析已核实的杰理短认证记录，不解释激活码或设备名业务字段。
 */
#ifndef D_AUTH_CODE_H
#define D_AUTH_CODE_H

#include <stdint.h>

#define D_AUTH_CODE_MAX_LENGTH 127U
#define D_AUTH_CODE_HEADER_SIZE 4U
#define D_AUTH_CODE_RECORD_SIZE (D_AUTH_CODE_HEADER_SIZE + D_AUTH_CODE_MAX_LENGTH)
#define D_AUTH_CODE_TAIL_DISTANCE 176U

typedef enum {
    D_AUTH_CODE_OK = 0,
    D_AUTH_CODE_ARGUMENT = -1,
    D_AUTH_CODE_EMPTY = -2,
    D_AUTH_CODE_LENGTH = -3,
    D_AUTH_CODE_CRC = -4,
    D_AUTH_CODE_CAPACITY = -5,
    D_AUTH_CODE_OPEN = -6,
    D_AUTH_CODE_IO = -7
} d_auth_code_result_e;

typedef struct {
    uint16_t length;
    uint8_t data[D_AUTH_CODE_MAX_LENGTH];
} d_auth_code_value_t;

/**
 * @brief 校验 CRC16 小端、长度小端及载荷组成的原始认证记录。
 * @param[in] record 调用期间有效的原始记录。
 * @param[in] record_size 输入缓冲区字节数，至少含 4 字节头。
 * @param[out] output 成功时复制载荷；非空输出在任何失败时 length 置零，data 保留原值。
 * @return D_AUTH_CODE_OK 为成功；ARGUMENT、EMPTY、LENGTH 或 CRC 分别表示参数、空记录、长度及校验错误。
 * @note 不追加字符串终止符，不保存输入指针，不解释 Q1、激活码或设备名等业务格式。
 */
d_auth_code_result_e d_auth_code_decode(const uint8_t *record,
                                      uint16_t record_size,
                                      d_auth_code_value_t *output);

typedef struct {
    uint32_t capacity;
    uint32_t address;
    int read_result;
} d_auth_code_read_info_t;

/**
 * @brief 同步只读获取板载 NOR 中的认证记录，并校验长度及 CRC。
 * @param[out] output 调用方持有的载荷容器；非空时先整体清零。
 * @param[out] info 可为 NULL；接收本次观察的容量、地址及底层读取结果。
 * @return 返回读取或解码结果；容量不等于配置的 16 MB 时返回 D_AUTH_CODE_CAPACITY。
 * @note 不保存指针，不共享工作缓冲；调用方须与维护及 Flash 写操作串行，当前层不负责占用预约。
 * @note 仅查询原始记录，不能把读取成功等同于平台激活或阿里设备注册成功。
 */
d_auth_code_result_e d_auth_code_read(d_auth_code_value_t *output,
                                    d_auth_code_read_info_t *info);

#endif
