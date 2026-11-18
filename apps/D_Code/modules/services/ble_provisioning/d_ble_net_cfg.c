/** @file d_ble_net_cfg.c @brief AE80/AE81/AE82 配网 GATT、JSON 收发与蓝牙栈适配。 */
//*********************************************************************************//
//                              D_Code BLE 蓝牙配网模块                            //
//                                                                                 //
//  GATT Profile：ae80 服务 / ae81 写入 / ae82 通知                                //
//  JSON 明文协议                                                                //
//  预留加密扩展接口（d_ble_decrypt / d_ble_encrypt 弱函数）                       //
//*********************************************************************************//

#include "config/app_config.h"

#if D_FEATURE_BLE_PROVISION

#include "system/app_core.h"
#include "system/includes.h"
#include "event/net_event.h"
#include "btstack/btstack_task.h"
#include "btstack/bluetooth.h"
#include "btstack/avctp_user.h"
#include "btcontroller_modules.h"
#include "bt_common.h"
#include "event/bt_event.h"
#include "system/wait.h"
#include "third_party/common/ble_user.h"
#include "d_ble_net_cfg.h"
#include "d_ble_provision_json.h"
#include "ble_service.h"
#include "le_common.h"

// 使用通用配网枚举值，但不编译 SDK 的 le_net_cfg.c（cbp 中不包含它）
#if (TCFG_BLE_DEMO_SELECT == DEF_BLE_DEMO_NET_CFG)

#define D_ATT_LOCAL_PAYLOAD_SIZE    128
#define D_ATT_SEND_CBUF_SIZE        512
#define D_ADV_INTERVAL_MIN          160
#define D_BLE_JSON_BUF_SIZE         D_BLE_JSON_CAPACITY

#define ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE                 0x0003
#define ATT_CHARACTERISTIC_ae81_01_VALUE_HANDLE                 0x0006
#define ATT_CHARACTERISTIC_ae82_01_VALUE_HANDLE                 0x0008
#define ATT_CHARACTERISTIC_ae82_01_CLIENT_CONFIGURATION_HANDLE  0x0009

static const uint8_t profile_data[] = {
    0x0a, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0x00, 0x18,
    0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x0a, 0x03, 0x00, 0x00, 0x2a,
    0x08, 0x00, 0x0a, 0x01, 0x03, 0x00, 0x00, 0x2a,
    0x0a, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x28, 0x80, 0xae,
    0x0d, 0x00, 0x02, 0x00, 0x05, 0x00, 0x03, 0x28, 0x04, 0x06, 0x00, 0x81, 0xae,
    0x08, 0x00, 0x04, 0x01, 0x06, 0x00, 0x81, 0xae,
    0x0d, 0x00, 0x02, 0x00, 0x07, 0x00, 0x03, 0x28, 0x10, 0x08, 0x00, 0x82, 0xae,
    0x08, 0x00, 0x10, 0x00, 0x08, 0x00, 0x82, 0xae,
    0x0a, 0x00, 0x0a, 0x01, 0x09, 0x00, 0x02, 0x29, 0x00, 0x00,
    0x00, 0x00,
};

//*********************************************************************************//
//                              调试日志配置                                       //
//*********************************************************************************//
#if 1
extern void printf_buf(u8 *buf, u32 len);
#define log_info          printf
#define log_info_hexdump  printf_buf
#else
#define log_info(...)
#define log_info_hexdump(...)
#endif

//*********************************************************************************//
//                              ATT 缓冲区                                        //
//*********************************************************************************//
#define ATT_RAM_BUFSIZE   (ATT_CTRL_BLOCK_SIZE + D_ATT_LOCAL_PAYLOAD_SIZE + D_ATT_SEND_CBUF_SIZE)
static u8 att_ram_buffer[ATT_RAM_BUFSIZE] __attribute__((aligned(4)));

#if (ATT_RAM_BUFSIZE < 64)
#error "ATT buffer too small!"
#endif

// 复用 att_ram_buffer 前 64 字节作为广播数据缓冲区
#define adv_data       &att_ram_buffer[0]
#define scan_rsp_data  &att_ram_buffer[32]

//*********************************************************************************//
//                              连接参数                                           //
//*********************************************************************************//
static volatile hci_con_handle_t con_handle;
static void ble_module_enable(u8 en);

static const uint8_t connection_update_enable = 1;
static uint8_t connection_update_cnt = 0;
static const struct conn_update_param_t connection_param_table[] = {
    {16, 24, 10, 600},
    {12, 28, 10, 600},
    {8,  20, 10, 600},
};
#define CONN_PARAM_TABLE_CNT  (sizeof(connection_param_table)/sizeof(struct conn_update_param_t))

//*********************************************************************************//
//                              内部状态变量                                       //
//*********************************************************************************//
static char gap_device_name[BT_NAME_LEN_MAX] = "D_CODE_ble";
static u8 gap_device_name_len = 0;
static u8 ble_work_state = 0;
static u8 adv_ctrl_en;
/* 1=允许配网广播（WiFi 未就绪/已断开）；0=WiFi 已连，禁止 DISCONNECT 后自动再开广播 */
static u8 s_prov_adv_wanted = 1;
static u8 complete = 0;
static OS_MUTEX mutex;

static void (*app_recieve_callback)(void *priv, void *buf, u16 len) = NULL;
static void (*app_ble_state_callback)(void *priv, ble_state_e state) = NULL;
static void (*ble_resume_send_wakeup)(void) = NULL;
static u32 channel_priv;

// JSON 接收缓冲区
static u8  json_recv_buf[D_BLE_JSON_BUF_SIZE];
static u16 json_recv_offset = 0;

// 前置声明
static int app_send_user_data_check(u16 len);
static int app_send_user_data_do(void *priv, u8 *data, u16 len);
static int app_send_user_data(u16 handle, const u8 *data, u16 len, u8 handle_type);
extern u8 is_in_config_network_state(void);
extern const char *bt_get_local_name();

//*********************************************************************************//
//                              JSON 响应（纯文本，无 JL 帧封装）                  //
//*********************************************************************************//
static const char rsp_connecting[]   = "{\"status\":1,\"msg\":\"connecting\"}";
static const char rsp_failed[]       = "{\"status\":2,\"msg\":\"failed\"}";
static const char rsp_no_ssid[]      = "{\"status\":3,\"msg\":\"ssid_not_found\"}";
static const char rsp_wrong_pwd[]    = "{\"status\":4,\"msg\":\"wrong_pwd\"}";

// 动态响应缓冲区（成功时需包含 MAC/UUID）
static char rsp_buf[192];

//*********************************************************************************//
//                              加密扩展接口（弱函数，默认透传）                   //
//*********************************************************************************//
/**
 * @brief 弱实现按明文复制接收内容，可由产品覆盖；完整合同见 d_ble_net_cfg.h。
 */
__attribute__((weak)) int d_ble_decrypt(const uint8_t *in, uint16_t in_len, uint8_t *out, uint16_t *out_len)
{
    if (in == NULL || out == NULL || out_len == NULL || in_len > *out_len) return -1;
    memcpy(out, in, in_len);
    *out_len = in_len;
    return 0;
}

/**
 * @brief 弱实现原样复制发送内容，可由产品覆盖；完整合同见 d_ble_net_cfg.h。
 */
__attribute__((weak)) int d_ble_encrypt(const uint8_t *in, uint16_t in_len, uint8_t *out, uint16_t *out_len)
{
    if (in == NULL || out == NULL || out_len == NULL || in_len > *out_len) return -1;
    memcpy(out, in, in_len);
    *out_len = in_len;
    return 0;
}

//*********************************************************************************//
//                              产品信息接口（弱函数，可覆盖）                     //
//*********************************************************************************//
/**
 * @brief 借用编译配置中的产品 UUID。
 * @return D_BLE_PRODUCT_UUID 静态字符串。
 */
__attribute__((weak)) const char *d_ble_get_product_uuid(void)
{
    return D_BLE_PRODUCT_UUID;
}

/**
 * @brief 借用编译配置中的产品批次。
 * @return D_BLE_BATCH_NUM 静态字符串。
 */
__attribute__((weak)) const char *d_ble_get_batch_num(void)
{
    return D_BLE_BATCH_NUM;
}

//*********************************************************************************//
//                              连接参数更新                                       //
//*********************************************************************************//
/**
 * @brief 向已连接手机请求表中一组连接参数。
 * @param[in] table_index 有效连接参数表索引，调用者保证范围。
 */
static void send_request_connect_parameter(u8 table_index)
{
    struct conn_update_param_t *param = (void *)&connection_param_table[table_index];

    log_info("update_request:-%d-%d-%d-%d-\n", param->interval_min, param->interval_max, param->latency, param->timeout);
    if (con_handle) {
        ble_user_cmd_prepare(BLE_CMD_REQ_CONN_PARAM_UPDATE, 2, con_handle, param);
    }
}

/**
 * @brief 参数更新启用且表未遍历完时提交当前候选。
 */
static void check_connetion_updata_deal(void)
{
    if (connection_update_enable) {
        if (connection_update_cnt < CONN_PARAM_TABLE_CNT) {
            send_request_connect_parameter(connection_update_cnt);
        }
    }
}

/**
 * @brief 解析并记录连接间隔、延迟与监督超时。
 * @param[in] packet SDK 格式的连接参数完成事件。
 */
static void connection_update_complete_success(u8 *packet)
{
    int conn_interval, conn_latency, conn_timeout;

    conn_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
    conn_latency = hci_subevent_le_connection_update_complete_get_conn_latency(packet);
    conn_timeout = hci_subevent_le_connection_update_complete_get_supervision_timeout(packet);

    log_info("conn_interval = %d\n", conn_interval);
    log_info("conn_latency = %d\n", conn_latency);
    log_info("conn_timeout = %d\n", conn_timeout);
}

//*********************************************************************************//
//                              BLE 状态管理                                       //
//*********************************************************************************//
/**
 * @brief 状态变化时同步调用已注册状态回调。
 * @param[in] state 新 BLE 工作状态。
 */
static void set_ble_work_state(ble_state_e state)
{
    if (state != ble_work_state) {
        log_info("ble_work_st:%x->%x\n", ble_work_state, state);
        ble_work_state = state;
        if (app_ble_state_callback) {
            app_ble_state_callback((void *)channel_priv, state);
        }
    }
}

/**
 * @brief 读取当前 BLE 工作状态。
 * @return 当前 ble_work_state。
 */
static ble_state_e get_ble_work_state(void)
{
    return ble_work_state;
}

//*********************************************************************************//
//                              SM 安全管理回调                                    //
//*********************************************************************************//
/**
 * @brief 响应安全管理的 Just Works 确认与口令显示事件。
 * @param[in] packet_type SDK 数据包类型。
 * @param[in] channel 回调兼容通道，当前不使用。
 * @param[in] packet SDK 事件内容。
 * @param[in] size 事件字节数，供口令显示诊断。
 */
static void cbk_sm_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    sm_just_event_t *event = (void *)packet;
    u32 tmp32;

    switch (packet_type) {
    case HCI_EVENT_PACKET:
        switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            log_info("Just Works Confirmed.\n");
            break;
        case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
            log_info_hexdump(packet, size);
            memcpy(&tmp32, event->data, 4);
            log_info("Passkey display: %06u.\n", tmp32);
            break;
        }
        break;
    }
}

/**
 * @brief 通知已注册的发送恢复回调。
 */
static void can_send_now_wakeup(void)
{
    putchar('E');
    if (ble_resume_send_wakeup) {
        ble_resume_send_wakeup();
    }
}

//*********************************************************************************//
//                              发送 JSON 响应工具函数                             //
//*********************************************************************************//
/**
 * @brief 将零结尾 JSON 经加密扩展后尝试发送到 AE82。
 * @param[in] json_str 调用期间稳定的零结尾JSON；空指针或超出192字节的输入拒绝发送。
 * @note 不返回加密或 ATT 发送结果；本函数不代表对端已收到。
 */
void d_ble_send_json_response(const char *json_str)
{
    u16 len = 0;
    u8 enc_buf[192];
    u16 enc_len = sizeof(enc_buf);

    if (json_str == NULL) return;
    while (len < sizeof(enc_buf) && json_str[len] != '\0') len++;
    if (json_str[len] != '\0') return;
    if (d_ble_encrypt((const u8 *)json_str, len, enc_buf, &enc_len) != 0 ||
        enc_len > sizeof(enc_buf)) return;
    app_send_user_data(ATT_CHARACTERISTIC_ae82_01_VALUE_HANDLE, enc_buf, enc_len, ATT_OP_AUTO_READ_CCC);
}

//*********************************************************************************//
//                              HCI 事件回调                                      //
//*********************************************************************************//
static const char *const phy_result[] = {
    "None", "1M", "2M", "Coded",
};

/**
 * @brief 为新连接绑定 ATT 缓冲并进入已连接状态。
 * @param[in] con_handle SDK 新连接句柄。
 */
static void server_profile_start(u16 con_handle)
{
    ble_user_cmd_prepare(BLE_CMD_ATT_SEND_INIT, 4, con_handle, att_ram_buffer, ATT_RAM_BUFSIZE, D_ATT_LOCAL_PAYLOAD_SIZE);
    set_ble_work_state(BLE_ST_CONNECT);
}

/**
 * @brief 处理 HCI、ATT 与 L2CAP 事件，维护连接及广播意图。
 * @param[in] packet_type SDK 包类型。
 * @param[in] channel 兼容通道，当前不使用。
 * @param[in] packet 有效 SDK 事件数据。
 * @param[in] size 兼容事件长度，当前未用于边界校验。
 * @note 断连后只在 s_prov_adv_wanted 有效时重开配网广播。
 */
static void cbk_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    int mtu;
    u32 tmp;
    u8 status;

    switch (packet_type) {
    case HCI_EVENT_PACKET:
        switch (hci_event_packet_get_type(packet)) {

        case ATT_EVENT_HANDLE_VALUE_INDICATION_COMPLETE:
            log_info("ATT_EVENT_HANDLE_VALUE_INDICATION_COMPLETE\n");
        case ATT_EVENT_CAN_SEND_NOW:
            can_send_now_wakeup();
            break;

        case HCI_EVENT_LE_META:
            switch (hci_event_le_meta_get_subevent_code(packet)) {
            case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE:
                status = hci_subevent_le_enhanced_connection_complete_get_status(packet);
                if (status) {
                    log_info("LE_SLAVE CONNECTION FAIL!!! %0x\n", status);
                    set_ble_work_state(BLE_ST_DISCONN);
                    break;
                }
                con_handle = hci_subevent_le_enhanced_connection_complete_get_connection_handle(packet);
                log_info("HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE : %0x\n", con_handle);
                log_info("conn_interval = %d\n", hci_subevent_le_enhanced_connection_complete_get_conn_interval(packet));
                log_info("conn_latency = %d\n", hci_subevent_le_enhanced_connection_complete_get_conn_latency(packet));
                log_info("conn_timeout = %d\n", hci_subevent_le_enhanced_connection_complete_get_supervision_timeout(packet));
                server_profile_start(con_handle);
                break;

            case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                status = hci_subevent_le_connection_complete_get_status(packet);
                if (status) {
                    log_info("LE_SLAVE CONNECTION FAIL!!! %0x\n", status);
                    set_ble_work_state(BLE_ST_DISCONN);
                    break;
                }
                con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                log_info("HCI_SUBEVENT_LE_CONNECTION_COMPLETE: %0x\n", con_handle);
                connection_update_complete_success(packet + 8);
                server_profile_start(con_handle);
                break;

            case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
                connection_update_complete_success(packet);
                break;

            case HCI_SUBEVENT_LE_DATA_LENGTH_CHANGE:
                log_info("APP HCI_SUBEVENT_LE_DATA_LENGTH_CHANGE\n");
                break;

            case HCI_SUBEVENT_LE_PHY_UPDATE_COMPLETE:
                log_info("APP HCI_SUBEVENT_LE_PHY_UPDATE %s\n", hci_event_le_meta_get_phy_update_complete_status(packet) ? "Fail" : "Succ");
                log_info("Tx PHY: %s\n", phy_result[hci_event_le_meta_get_phy_update_complete_tx_phy(packet)]);
                log_info("Rx PHY: %s\n", phy_result[hci_event_le_meta_get_phy_update_complete_rx_phy(packet)]);
                break;
            }
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            log_info("HCI_EVENT_DISCONNECTION_COMPLETE: %0x\n", packet[5]);
            con_handle = 0;
            json_recv_offset = 0;
            memset(json_recv_buf, 0, sizeof(json_recv_buf));
            ble_user_cmd_prepare(BLE_CMD_ATT_SEND_INIT, 4, con_handle, 0, 0, 0);
            set_ble_work_state(BLE_ST_DISCONN);
            /*
             * 仅在 WiFi 未就绪或显式重配要求广播时重开。
             * WiFi 已在线时保持广播关闭，避免手机 BLE 断开
             * 导致设备在已联网状态仍持续可被发现。
             */
#if !CONFIG_POWER_ON_ENABLE_BLE
            if (is_in_config_network_state() && s_prov_adv_wanted) {
#else
            if (s_prov_adv_wanted) {
#endif
                os_mutex_pend(&mutex, 0);
                bt_ble_adv_enable(1);
                complete = 0;
                os_mutex_post(&mutex);
            }
            connection_update_cnt = 0;
            break;

        case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
            mtu = att_event_mtu_exchange_complete_get_MTU(packet) - 3;
            log_info("ATT MTU = %d\n", mtu);
            ble_user_cmd_prepare(BLE_CMD_ATT_MTU_SIZE, 1, mtu);
            break;

        case HCI_EVENT_VENDOR_REMOTE_TEST:
            log_info("--- HCI_EVENT_VENDOR_REMOTE_TEST\n");
            break;

        case L2CAP_EVENT_CONNECTION_PARAMETER_UPDATE_RESPONSE:
            tmp = little_endian_read_16(packet, 4);
            log_info("-update_rsp: %02x\n", tmp);
            if (tmp) {
                connection_update_cnt++;
                log_info("remoter reject!!!\n");
                check_connetion_updata_deal();
            } else {
                connection_update_cnt = CONN_PARAM_TABLE_CNT;
            }
            break;

        case HCI_EVENT_ENCRYPTION_CHANGE:
            log_info("HCI_EVENT_ENCRYPTION_CHANGE= %d\n", packet[2]);
            break;
        }
        break;
    }
}

//*********************************************************************************//
//                              ATT 读回调                                        //
//*********************************************************************************//
/**
 * @brief 读取 GAP 设备名或 AE82 通知配置。
 * @param[in] connection_handle SDK 连接句柄，本实现不使用。
 * @param[in] att_handle 属性句柄。
 * @param[in] offset 设备名读取偏移。
 * @param[out] buffer 可空输出；设备名分支允许查询长度。
 * @param[in] buffer_size 本次输出容量。
 * @return 本次报告的属性值长度，未知属性为 0。
 * @note 遵守 SDK ATT 回调入参合同，当前 CCC 分支要求至少 2 字节输出。
 */
static uint16_t att_read_callback(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    uint16_t att_value_len = 0;
    uint16_t handle = att_handle;

    log_info("read_callback, handle= 0x%04x,buffer= %08x\n", handle, (u32)buffer);

    switch (handle) {
    case ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE:
        att_value_len = gap_device_name_len;
        if ((offset >= att_value_len) || (offset + buffer_size) > att_value_len) {
            break;
        }
        if (buffer) {
            memcpy(buffer, &gap_device_name[offset], buffer_size);
            att_value_len = buffer_size;
            log_info("\n------read gap_name: %s \n", gap_device_name);
        }
        break;

    case ATT_CHARACTERISTIC_ae82_01_CLIENT_CONFIGURATION_HANDLE:
        if (buffer == NULL || buffer_size < 2U) break;
        if (buffer) {
            buffer[0] = att_get_ccc_config(handle);
            buffer[1] = 0;
        }
        att_value_len = 2;
        break;

    default:
        break;
    }

    log_info("att_value_len= %d\n", att_value_len);
    return att_value_len;
}

//*********************************************************************************//
//                              JSON 配网数据解析                                    //
//*********************************************************************************//

/**
 * @brief 在 WiFi 副作用前解码完整命令，再按受理结果响应。
 * @param[in] json_str 完整零结尾 JSON 对象。
 * @param[in] json_len 不含末尾零的字节数。
 */
static void d_ble_process_json_cmd(const char *json_str, u16 json_len)
{
    d_ble_json_command_t command;
    if (d_ble_json_decode(json_str, json_len, &command) != 0) {
        log_info("[D_BLE] invalid JSON command\n");
        d_ble_send_json_response(rsp_failed);
        return;
    }
    if (command.kind == D_BLE_JSON_WIFI) {
        if (ble_service_on_wifi_cfg(command.ssid, command.password) == 0) {
            d_ble_send_json_response(rsp_connecting);
            complete = 1;
        } else d_ble_send_json_response(rsp_failed);
        return;
    }
    if (command.kind == D_BLE_JSON_STATUS || command.kind == D_BLE_JSON_MAC) {
        u8 mac[6];
        extern void wifi_get_mac(u8 *mac);
        wifi_get_mac(mac);
        if (command.kind == D_BLE_JSON_STATUS) {
            snprintf(rsp_buf, sizeof(rsp_buf),
                "{\"status\":\"ok\",\"mac\":\"%02X%02X%02X%02X%02X%02X\",\"name\":\"%s\"}",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], gap_device_name);
        } else {
            snprintf(rsp_buf, sizeof(rsp_buf), "{\"mac\":\"%02X%02X%02X%02X%02X%02X\"}",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        d_ble_send_json_response(rsp_buf);
        return;
    }
    d_ble_send_json_response("{\"status\":-1,\"msg\":\"unknown_cmd\"}");
}

//*********************************************************************************//
//                              ATT 写回调                                          //
//*********************************************************************************//
/**
 * @brief 接收通知订阅或 AE81 分片，完成有界解密、分帧与命令派发。
 * @param[in] connection_handle SDK 连接句柄，本实现不使用。
 * @param[in] att_handle 写入属性句柄。
 * @param[in] transaction_mode SDK 事务模式，本实现不使用。
 * @param[in] offset SDK 写偏移，本实现不使用。
 * @param[in] buffer SDK 写入内容，借用至回调返回。
 * @param[in] buffer_size 输入字节数。
 * @return 当前实现固定返回 0，命令失败通过 JSON 响应表达。
 */
static int att_write_callback(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    u16 handle = att_handle;
    log_info("write_callback, handle= 0x%04x,size = %d\n", handle, buffer_size);

    switch (handle) {

    case ATT_CHARACTERISTIC_2a00_01_VALUE_HANDLE:
        break;

    case ATT_CHARACTERISTIC_ae82_01_CLIENT_CONFIGURATION_HANDLE:
        att_set_ccc_config(handle, buffer[0]);
        set_ble_work_state(buffer[0] ? BLE_ST_NOTIFY_IDICATE : BLE_ST_CONNECT);
        check_connetion_updata_deal();
        log_info("\n------write ccc:%04x,%02x\n", handle, buffer[0]);
        // 重置 JSON 接收状态
        json_recv_offset = 0;
        memset(json_recv_buf, 0, sizeof(json_recv_buf));
        complete = 0;
        break;

    case ATT_CHARACTERISTIC_ae81_01_VALUE_HANDLE:
        // ====== 核心改造：JSON 明文协议（替换 JL 二进制帧） ======
        {
            // 解密（默认透传）
            u8 dec_buf[D_BLE_JSON_BUF_SIZE];
            u16 dec_len = sizeof(dec_buf);
            int frame;
            if (buffer == NULL || buffer_size == 0U || buffer_size > sizeof(dec_buf) ||
                d_ble_decrypt(buffer, buffer_size, dec_buf, &dec_len) != 0 ||
                dec_len == 0U || dec_len > sizeof(dec_buf)) {
                json_recv_offset = 0;
                d_ble_send_json_response(rsp_failed);
                break;
            }
            printf("\n[D_BLE] ae81_rx(%d):", buffer_size);
            printf_buf(buffer, buffer_size);

            // 拼接到接收缓冲区（支持分包）
            if (dec_len < sizeof(json_recv_buf) - json_recv_offset) {
                memcpy(json_recv_buf + json_recv_offset, dec_buf, dec_len);
                json_recv_offset += dec_len;
                json_recv_buf[json_recv_offset] = '\0';
            } else {
                // 缓冲区溢出，重置
                log_info("[D_BLE] json buf overflow, reset\n");
                json_recv_offset = 0;
                memset(json_recv_buf, 0, sizeof(json_recv_buf));
                d_ble_send_json_response(rsp_failed);
                break;
            }

            frame = d_ble_json_frame((const char *)json_recv_buf, json_recv_offset);
            if (frame != 0) {
                if (frame > 0) d_ble_process_json_cmd((const char *)json_recv_buf, json_recv_offset);
                else d_ble_send_json_response(rsp_failed);
                // 重置接收缓冲区
                json_recv_offset = 0;
                memset(json_recv_buf, 0, sizeof(json_recv_buf));
            }
        }
        break;

    default:
        break;
    }

    return 0;
}

//*********************************************************************************//
//                              数据发送                                          //
//*********************************************************************************//
/**
 * @brief 检查连接与 CCC，在互斥区向 ATT 提交通知数据。
 * @param[in] handle 通知属性句柄。
 * @param[in] data 本次提交的数据。
 * @param[in] len 待发字节数。
 * @param[in] handle_type SDK ATT 发送操作类型。
 * @return 返回 APP_BLE_* 或 SDK 提交错误；成功仅表示底层命令受理。
 */
static int app_send_user_data(u16 handle, const u8 *data, u16 len, u8 handle_type)
{
    u32 ret = APP_BLE_NO_ERROR;

    if (!con_handle) {
        return APP_BLE_OPERATION_ERROR;
    }

    os_mutex_pend(&mutex, 0);

    if (!att_get_ccc_config(handle + 1)) {
        log_info("fail,no write ccc!!!,%04x\n", handle + 1);
        os_mutex_post(&mutex);
        return APP_BLE_NO_WRITE_CCC;
    }

    ret = ble_user_cmd_prepare(BLE_CMD_ATT_SEND_DATA, 4, handle, data, len, handle_type);
    if (ret == BLE_BUFFER_FULL) {
        ret = APP_BLE_BUFF_FULL;
    }

    os_mutex_post(&mutex);

    if (ret) {
        log_info("app_send_fail:%d !!!!!!\n", ret);
    }
    return ret;
}

//*********************************************************************************//
//                              广播数据设置                                      //
//*********************************************************************************//
/**
 * @brief 构建 Flags 与 AE80 服务广播数据并提交 SDK。
 * @return 构建成功返回 0，超过广播容量返回 -1。
 */
static int make_set_adv_data(void)
{
    u8 offset = 0;
    u8 *buf = adv_data;

    offset += make_eir_packet_val(&buf[offset], offset, HCI_EIR_DATATYPE_FLAGS, 0x06, 1);
    offset += make_eir_packet_val(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS, 0xAE80, 2);

    if (offset > ADV_RSP_PACKET_MAX) {
        puts("***adv_data overflow!!!!!!\n");
        return -1;
    }
    log_info("adv_data(%d):", offset);
    log_info_hexdump(buf, offset);
    ble_user_cmd_prepare(BLE_CMD_ADV_DATA, 2, offset, buf);
    return 0;
}

/**
 * @brief 以有界本地名构建扫描响应并提交 SDK。
 * @return 构建成功返回 0，超过容量返回 -1。
 */
static int make_set_rsp_data(void)
{
    u8 offset = 0;
    u8 *buf = scan_rsp_data;

    u8 name_len = gap_device_name_len;
    u8 vaild_len = ADV_RSP_PACKET_MAX - (offset + 2);
    if (name_len > vaild_len) {
        name_len = vaild_len;
    }
    offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)gap_device_name, name_len);

    if (offset > ADV_RSP_PACKET_MAX) {
        puts("***rsp_data overflow!!!!!!\n");
        return -1;
    }

    log_info("rsp_data(%d):", offset);
    log_info_hexdump(buf, offset);
    ble_user_cmd_prepare(BLE_CMD_RSP_DATA, 2, offset, buf);
    return 0;
}

/**
 * @brief 设置广播参数，并按顺序提交广播与扫描响应内容。
 */
static void advertisements_setup_init()
{
    uint8_t adv_type = ADV_IND;
    uint8_t adv_channel = ADV_CHANNEL_ALL;
    int ret = 0;

    ble_user_cmd_prepare(BLE_CMD_ADV_PARAM, 3, D_ADV_INTERVAL_MIN, adv_type, adv_channel);

    ret |= make_set_adv_data();
    ret |= make_set_rsp_data();

    if (ret) {
        puts("advertisements_setup_init fail !!!!!!\n");
        return;
    }
}

//*********************************************************************************//
//                              SM/Profile 初始化                                 //
//*********************************************************************************//
#define PASSKEY_ENTER_ENABLE      0

/**
 * @brief 生成六位十进制范围口令并写回 SDK。
 * @param[out] key 非空新口令输出。
 */
static void reset_passkey_cb(u32 *key)
{
    u32 newkey = rand32();
    newkey &= 0xfffff;
    if (newkey > 999999) {
        newkey = newkey - 999999;
    }
    *key = newkey;
    printf("set new_key= %06u\n", *key);
}

extern void reset_PK_cb_register(void (*reset_pk)(u32 *));
/**
 * @brief 配置安全管理能力、认证、密钥长度与回调。
 * @param[in] io_type SDK I/O 能力。
 * @param[in] auth_req SDK 认证要求位。
 * @param[in] min_key_size 允许的最小加密密钥长度。
 * @param[in] security_en 是否主动请求安全过程。
 */
void ble_sm_setup_init(io_capability_t io_type, u8 auth_req, uint8_t min_key_size, u8 security_en)
{
    sm_init();
    sm_set_io_capabilities(io_type);
    sm_set_authentication_requirements(auth_req);
    sm_set_encryption_key_size_range(min_key_size, 16);
    sm_set_request_security(security_en);
    sm_event_callback_set(&cbk_sm_packet_handler);

    if (io_type == IO_CAPABILITY_DISPLAY_ONLY) {
        reset_PK_cb_register(reset_passkey_cb);
    }
}

extern void le_device_db_init(void);
/**
 * @brief 初始化设备数据库、安全管理、GATT 与协议事件回调。
 * @note 作为 SDK 协议栈回调入口，GATT Profile 数据保持固定。
 */
void ble_profile_init(void)
{
    printf("[D_BLE] profile init\n");

    printf("[D_BLE] step1: le_device_db_init\n");
    le_device_db_init();

    printf("[D_BLE] step2: ble_sm_setup_init\n");
#if PASSKEY_ENTER_ENABLE
    ble_sm_setup_init(IO_CAPABILITY_DISPLAY_ONLY, SM_AUTHREQ_BONDING | SM_AUTHREQ_MITM_PROTECTION, 7, TCFG_BLE_SECURITY_EN);
#else
    ble_sm_setup_init(IO_CAPABILITY_NO_INPUT_NO_OUTPUT, SM_AUTHREQ_BONDING | SM_AUTHREQ_MITM_PROTECTION, 7, TCFG_BLE_SECURITY_EN);
#endif

    printf("[D_BLE] step3: att_server_init\n");
    att_server_init(profile_data, att_read_callback, att_write_callback);

    printf("[D_BLE] step4: att_server_register\n");
    att_server_register_packet_handler(cbk_packet_handler);

    printf("[D_BLE] step5: hci_event_callback\n");
    hci_event_callback_set(&cbk_packet_handler);

    printf("[D_BLE] step6: le_l2cap_register\n");
    le_l2cap_register_packet_handler(&cbk_packet_handler);

    printf("[D_BLE] profile init done!\n");
}

//*********************************************************************************//
//                              广播使能/断开/模块控制                            //
//*********************************************************************************//
/**
 * @brief 仅在允许且无连接的可切换状态下提交广播开关。
 * @param[in] priv 操作表兼容上下文，当前不使用。
 * @param[in] en 非零开启广播，0 关闭。
 * @return 允许或已处于目标状态返回 APP_BLE_NO_ERROR；条件不符返回 APP_BLE_OPERATION_ERROR。
 */
static int set_adv_enable(void *priv, u32 en)
{
    ble_state_e next_state, cur_state;

    if (!adv_ctrl_en && en) {
        return APP_BLE_OPERATION_ERROR;
    }

    if (con_handle) {
        return APP_BLE_OPERATION_ERROR;
    }

    if (en) {
        next_state = BLE_ST_ADV;
    } else {
        next_state = BLE_ST_IDLE;
    }

    cur_state = get_ble_work_state();
    switch (cur_state) {
    case BLE_ST_ADV:
    case BLE_ST_IDLE:
    case BLE_ST_INIT_OK:
    case BLE_ST_NULL:
    case BLE_ST_DISCONN:
        break;
    default:
        return APP_BLE_OPERATION_ERROR;
        break;
    }

    if (cur_state == next_state) {
        return APP_BLE_NO_ERROR;
    }
    log_info("adv_en:%d\n", en);
    set_ble_work_state(next_state);
    if (en) {
        advertisements_setup_init();
    }
    ble_user_cmd_prepare(BLE_CMD_ADV_ENABLE, 1, en);
    return APP_BLE_NO_ERROR;
}

/**
 * @brief 对当前连接最多提交一次断开命令。
 * @param[in] priv 操作表兼容上下文，当前不使用。
 * @return 有连接返回 APP_BLE_NO_ERROR；无连接返回 APP_BLE_OPERATION_ERROR。
 * @note 返回成功不等于 HCI 断连事件已完成。
 */
static int ble_disconnect(void *priv)
{
    if (con_handle) {
        if (BLE_ST_SEND_DISCONN != get_ble_work_state()) {
            log_info(">>>ble send disconnect\n");
            set_ble_work_state(BLE_ST_SEND_DISCONN);
            ble_user_cmd_prepare(BLE_CMD_DISCONNECT, 1, con_handle);
        } else {
            log_info(">>>ble wait disconnect...\n");
        }
        return APP_BLE_NO_ERROR;
    } else {
        return APP_BLE_OPERATION_ERROR;
    }
}

/**
 * @brief 向 ATT 查询发送缓冲可用字节数。
 * @param[in] priv 操作表兼容上下文，当前不使用。
 * @return SDK 写回的可用容量。
 */
static int get_buffer_vaild_len(void *priv)
{
    u32 vaild_len = 0;
    ble_user_cmd_prepare(BLE_CMD_ATT_VAILD_LEN, 1, &vaild_len);
    return vaild_len;
}

/**
 * @brief 通过 AE82 通知属性发送操作表数据。
 * @param[in] priv 操作表兼容上下文，当前不使用。
 * @param[in] data 待发字节。
 * @param[in] len 待发长度。
 * @return 底层 ATT 发送结果。
 */
static int app_send_user_data_do(void *priv, u8 *data, u16 len)
{
    return app_send_user_data(ATT_CHARACTERISTIC_ae82_01_VALUE_HANDLE, data, len, ATT_OP_AUTO_READ_CCC);
}

/**
 * @brief 检查 ATT 剩余发送容量是否可容纳本次负载。
 * @param[in] len 需要的字节数。
 * @return 容量足够返回 1，否则返回 0。
 */
static int app_send_user_data_check(u16 len)
{
    u32 buf_space = get_buffer_vaild_len(0);
    if (len <= buf_space) {
        return 1;
    }
    return 0;
}

/**
 * @brief 保存发送恢复回调。
 * @param[in] priv 兼容上下文，当前不使用。
 * @param[in] cbk SDK 操作表传入的回调指针；后续保持有效。
 * @return 固定返回 APP_BLE_NO_ERROR。
 */
static int regiest_wakeup_send(void *priv, void *cbk)
{
    ble_resume_send_wakeup = cbk;
    return APP_BLE_NO_ERROR;
}

/**
 * @brief 保存接收回调及共享通道上下文。
 * @param[in] priv 编码到 channel_priv 的上下文。
 * @param[in] cbk 接收回调指针，当前接收路径未调用此存储项。
 * @return 固定返回 APP_BLE_NO_ERROR。
 */
static int regiest_recieve_cbk(void *priv, void *cbk)
{
    channel_priv = (u32)priv;
    app_recieve_callback = cbk;
    return APP_BLE_NO_ERROR;
}

/**
 * @brief 保存状态回调及共享通道上下文。
 * @param[in] priv 状态回调使用的上下文。
 * @param[in] cbk 后续状态变化时调用的回调，需持续有效。
 * @return 固定返回 APP_BLE_NO_ERROR。
 */
static int regiest_state_cbk(void *priv, void *cbk)
{
    channel_priv = (u32)priv;
    app_ble_state_callback = cbk;
    return APP_BLE_NO_ERROR;
}

/**
 * @brief 调用私有广播开关，忽略立即返回结果。
 * @param[in] enable 非零开启，0 关闭。
 */
void bt_ble_adv_enable(u8 enable)
{
    set_adv_enable(0, enable);
}

/**
 * @brief 保存配网广播意图，并在有连接时等待断连后重开；完整合同见 d_ble_net_cfg.h。
 */
void d_ble_net_cfg_set_adv(uint8_t enable)
{
    s_prov_adv_wanted = enable ? 1 : 0;
    /*
     * 手机仍有 GATT 连接时无法立即开启广播。
     * 先请求断连，wanted=1 时由 DISCONNECTION_COMPLETE 续开广播。
     */
    if (enable && con_handle) {
        log_info("adv deferred until BLE disconnect (wanted=1)\n");
        ble_disconnect(NULL);
        return;
    }
    bt_ble_adv_enable((u8)enable);
}

/**
 * @brief 在互斥区启停配网模块，并根据连接状态提交广播或断连。
 * @param[in] en 非零启用，0 停用。
 * @note 配网 complete 且仍连接时，停用路径保留当前连接；不能据此宣称已关闭。
 */
static void ble_module_enable(u8 en)
{
    os_mutex_pend(&mutex, 0);
    log_info("mode_en:%d\n", en);
    if (en) {
        extern u8 get_ble_gatt_role(void);
        if (1 == get_ble_gatt_role()) {
            ble_stack_gatt_role(0);
            hci_event_callback_set(&cbk_packet_handler);
            le_l2cap_register_packet_handler(&cbk_packet_handler);
        }
        complete = 0;
        adv_ctrl_en = 1;
        bt_ble_adv_enable(s_prov_adv_wanted);
    } else {
        if (con_handle) {
            if (complete) {
                os_mutex_post(&mutex);
                return;
            }
            adv_ctrl_en = 0;
            ble_disconnect(NULL);
        } else {
            bt_ble_adv_enable(0);
            adv_ctrl_en = 0;
        }
        complete = 0;
    }
    os_mutex_post(&mutex);
}

//*********************************************************************************//
//                              对外 API 接口                                      //
//*********************************************************************************//

/**
 * @brief 用 WiFi MAC 尾部生成设备名，建立互斥量并启用配网模块；完整合同见 d_ble_net_cfg.h。
 */
void bt_ble_init(void)
{
    log_info("[D_BLE] ***** ble_init ******\n");
    char bl_name[32];
    u8 mac[6];

    extern void wifi_get_mac(u8 *mac);
    wifi_get_mac(mac);
    snprintf(bl_name, sizeof(bl_name), "%s_%02X%02X%02X",
             D_BLE_DEVICE_NAME_PREFIX, mac[3], mac[4], mac[5]);

    if (!os_mutex_valid(&mutex)) {
        os_mutex_create(&mutex);
    }

    gap_device_name_len = strlen(bl_name);
    if (gap_device_name_len > BT_NAME_LEN_MAX) {
        gap_device_name_len = BT_NAME_LEN_MAX;
    }

    memset(gap_device_name, 0, sizeof(gap_device_name));
    memcpy(gap_device_name, bl_name, gap_device_name_len);

    log_info("[D_BLE] name(%d): %s \n", gap_device_name_len, gap_device_name);

    set_ble_work_state(BLE_ST_INIT_OK);
    ble_module_enable(1);
}

/**
 * @brief 请求停用配网模块。
 */
static void stack_exit(void)
{
    ble_module_enable(0);
}

/**
 * @brief 请求停用配网模块，活动连接可能需要异步断开；完整合同见 d_ble_net_cfg.h。
 */
void bt_ble_exit(void)
{
    log_info("[D_BLE] ***** ble_exit ******\n");
    stack_exit();
}

/**
 * @brief 请求断开当前 BLE 连接，忽略底层立即返回值；完整合同见 d_ble_net_cfg.h。
 */
void ble_app_disconnect(void)
{
    ble_disconnect(NULL);
}

/**
 * @brief 格式化并尝试发送含 MAC、可选 UUID 与批次的成功响应；完整合同见 d_ble_net_cfg.h。
 */
int ble_connected_net_notify(void)
{
    int len = 0;
    int added;
    u8 mac[6];

    extern void wifi_get_mac(u8 *mac);
    wifi_get_mac(mac);

    const char *uuid = d_ble_get_product_uuid();
    const char *batch_num = d_ble_get_batch_num();

    memset(rsp_buf, 0, sizeof(rsp_buf));
    len = snprintf(rsp_buf, sizeof(rsp_buf),
                   "{\"status\":0,\"mac\":\"%02X%02X%02X%02X%02X%02X\"",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (len < 0 || (size_t)len >= sizeof(rsp_buf)) return -1;

    if (uuid && uuid[0]) {
        added = snprintf(rsp_buf + len, sizeof(rsp_buf) - len, ",\"uuid\":\"%s\"", uuid);
        if (added < 0 || (size_t)added >= sizeof(rsp_buf) - (size_t)len) return -1;
        len += added;
    }
    if (batch_num && batch_num[0]) {
        added = snprintf(rsp_buf + len, sizeof(rsp_buf) - len, ",\"batch_num\":\"%s\"", batch_num);
        if (added < 0 || (size_t)added >= sizeof(rsp_buf) - (size_t)len) return -1;
        len += added;
    }
    added = snprintf(rsp_buf + len, sizeof(rsp_buf) - len, ",\"msg\":\"connected\"}");
    if (added < 0 || (size_t)added >= sizeof(rsp_buf) - (size_t)len) return -1;

    log_info("[D_BLE] net_notify: %s\n", rsp_buf);
    d_ble_send_json_response(rsp_buf);
    return 0;
}

/**
 * @brief 仅在配网命令已经受理时转发 WiFi 连接结果；完整合同见 d_ble_net_cfg.h。
 */
void ble_cfg_net_result_notify(int event)
{
    if (!complete) {
        return;
    }

    if (event == NET_CONNECT_TIMEOUT_NOT_FOUND_SSID) {
        d_ble_send_json_response(rsp_no_ssid);
    } else if (event == NET_CONNECT_ASSOCIAT_FAIL) {
        d_ble_send_json_response(rsp_wrong_pwd);
    } else if (event == NET_EVENT_CONNECTED) {
        ble_connected_net_notify();
        complete = 0;
#if !CONFIG_POWER_ON_ENABLE_BLE
        // 配网成功后延时关闭 BLE（暂不关闭，保持BLE常开）
        // sys_timeout_add(0, (void (*)(void *))ble_module_enable, 100);
#endif
    }
}

//*********************************************************************************//
//                              BLE Server Operation 注册                          //
//*********************************************************************************//
static const struct ble_server_operation_t d_ble_operation = {
    .adv_enable        = set_adv_enable,
    .disconnect        = ble_disconnect,
    .get_buffer_vaild  = get_buffer_vaild_len,
    .send_data         = (void *)app_send_user_data_do,
    .regist_wakeup_send = regiest_wakeup_send,
    .regist_recieve_cbk = regiest_recieve_cbk,
    .regist_state_cbk   = regiest_state_cbk,
};

/**
 * @brief 向 SDK 返回静态 BLE 服务操作表。
 * @param[out] interface_pt 非空操作表指针输出。
 */
void ble_get_server_operation_table(struct ble_server_operation_t **interface_pt)
{
    *interface_pt = (void *)&d_ble_operation;
}

#endif // TCFG_BLE_DEMO_SELECT == DEF_BLE_DEMO_NET_CFG

//*********************************************************************************//
//                              BLE 协议栈初始化（必须）                           //
//  参考 SDK: apps/wifi_ipc/wifi/wifi_app_task.c                                   //
//  btstack_init() 启动 BLE 协议栈，协议栈内部会回调 ble_profile_init()            //
//*********************************************************************************//

// BT 协议栈需要的全局变量
void (*lmp_ch_update_resume_hdl)(void *priv) = NULL;

/**
 * @brief 关闭 sniff，设置 BLE 地址并启动蓝牙协议栈；完整合同见 d_ble_net_cfg.h。
 */
void bt_ble_module_init(void)
{
    void lmp_set_sniff_disable(void);
    lmp_set_sniff_disable();

#if TCFG_USER_BLE_ENABLE
    u8 tmp_ble_addr[6];
    extern const u8 *bt_get_mac_addr(void);
    extern void lib_make_ble_address(u8 *ble_address, u8 *edr_address);
    extern int le_controller_set_mac(void *addr);
    lib_make_ble_address(tmp_ble_addr, (u8 *)bt_get_mac_addr());
    le_controller_set_mac((void *)tmp_ble_addr);
    printf("\n[D_BLE] ----- edr + ble address -----\n");
    put_buf((void *)bt_get_mac_addr(), 6);
    put_buf((void *)tmp_ble_addr, 6);
#endif

    btstack_init();
}

/**
 * @brief 在 BT_STATUS_INIT_OK 后启动配网及可选常开 BLE。
 * @param[in] bt 非空 SDK 蓝牙连接事件。
 * @return 固定返回 0。
 */
static int bt_connction_status_event_handler(struct bt_event *bt)
{
    switch (bt->event) {
    case BT_STATUS_INIT_OK:
        printf("[D_BLE] BT_STATUS_INIT_OK\n");
        /*
         * 蓝牙协议栈已完全初始化，现在启动 BLE 配网广播。
         * 必须在这里启动（而非 ble_service_init），否则
         * btctrler 未就绪时广播不会真正发出。
         */
        {
            extern void config_network_start(void);
            extern u8 is_in_config_network_state(void);
            config_network_start();
            printf("[D_BLE] BLE provisioning started after BT init\n");
        }
#if CONFIG_POWER_ON_ENABLE_BLE
        bt_ble_init();
#endif
        break;
    }
    return 0;
}

/**
 * @brief 分发来自蓝牙连接源的系统事件；完整合同见 d_ble_net_cfg.h。
 */
int ble_demo_bt_event_handler(struct sys_event *event)
{
    if (event->from == BT_EVENT_FROM_CON) {
        return bt_connction_status_event_handler((struct bt_event *)event->payload);
    }
    return 0;
}

#endif // D_FEATURE_BLE_PROVISION
