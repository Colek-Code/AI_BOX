/** @file product_ui_visual_capture.c @brief 验证用现有 GC9307 过渡暂存帧导出，带请求身份及像素 CRC。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/debug/product_ui_visual_capture.h"

#if D_DEBUG_VISUAL_CAPTURE

#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307_transition.h"
#include "platform/screen/transition/d_page_transition.h"
#include "device/includes.h"
#include "lvgl.h"
#include <string.h>

#define PRODUCT_UI_CAPTURE_MAGIC_SIZE        8U
#define PRODUCT_UI_CAPTURE_HEADER_SIZE      64U
#define PRODUCT_UI_CAPTURE_CASE_SIZE        16U
#define PRODUCT_UI_CAPTURE_UART_CHUNK    32768U
#define PRODUCT_UI_CAPTURE_DELAY_MS         80U
#define PRODUCT_UI_CAPTURE_ID_BASE  0x80000000U

static const uint8_t s_capture_magic[PRODUCT_UI_CAPTURE_MAGIC_SIZE] = {
    'P', 'U', 'I', 'F', 'R', 'M', '1', 0,
};
static void *s_capture_uart;
static lv_timer_t *s_capture_timer;
static uint32_t s_capture_sequence;
static uint32_t s_capture_transition_id;
static uint8_t s_capture_pending;
static char s_capture_case[PRODUCT_UI_CAPTURE_CASE_SIZE];

/**
 * @brief 向协议头写入两字节小端值。
 * @param[out] dst 至少两字节可写片段。
 * @param[in] value 待编码数值。
 */
static void product_ui_capture_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

/**
 * @brief 向协议头写入四字节小端值。
 * @param[out] dst 至少四字节可写片段。
 * @param[in] value 待编码数值。
 */
static void product_ui_capture_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

/**
 * @brief 计算完整像素载荷的反射 CRC32。
 * @param[in] data 至少 length 字节的借用载荷。
 * @param[in] length 参与校验的字节数。
 * @return 使用 0xedb88320、初始全一及最终取反的 CRC32。
 */
static uint32_t product_ui_capture_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xffffffffU;
    uint32_t index;
    uint8_t bit;

    for (index = 0; index < length; index++) {
        crc ^= data[index];
        for (bit = 0; bit < 8U; bit++) {
            crc = (crc >> 1) ^
                  (0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}

/**
 * @brief 在固定用例标识缓冲内取得有效长度。
 * @return 首个 NUL 前的字节数，最大为 CASE_SIZE。
 */
static uint8_t product_ui_capture_case_length(void)
{
    uint8_t length = 0U;

    for (length = 0U; length < PRODUCT_UI_CAPTURE_CASE_SIZE; length++) {
        if (s_capture_case[length] == '\0') {
            break;
        }
    }
    return length;
}

/**
 * @brief 编码固定六十四字节帧头，包括尺寸、方向、像素长度和 CRC。
 * @param[out] header 至少 HEADER_SIZE 字节的输出。
 * @param[in] frame 本次观察器借用的冻结过渡帧。
 * @param[in] frame_crc 相同像素载荷的 CRC32。
 * @note 像素仍按面板 RGB565 大端字节排列，协议头数值另按小端编码。
 */
static void product_ui_capture_build_header(
    uint8_t *header, const gc9307_transition_frame_t *frame,
    uint32_t frame_crc)
{
    memset(header, 0, PRODUCT_UI_CAPTURE_HEADER_SIZE);
    memcpy(header, s_capture_magic, sizeof(s_capture_magic));
    product_ui_capture_put_u16(header + 8, 1U);
    product_ui_capture_put_u16(header + 10,
                               PRODUCT_UI_CAPTURE_HEADER_SIZE);
    product_ui_capture_put_u32(header + 12, frame->frame_bytes);
    product_ui_capture_put_u32(header + 16, frame_crc);
    product_ui_capture_put_u32(header + 20, s_capture_sequence);
    product_ui_capture_put_u32(header + 24, frame->transition_id);
    product_ui_capture_put_u16(header + 28, D_GC9307_PHYS_HOR_RES);
    product_ui_capture_put_u16(header + 30, D_GC9307_PHYS_VER_RES);
    product_ui_capture_put_u16(header + 32, MY_DISP_HOR_RES);
    product_ui_capture_put_u16(header + 34, MY_DISP_VER_RES);
    product_ui_capture_put_u16(header + 36, frame->orientation.angle);
    header[38] = 1U; /* RGB565 像素使用面板大端字节顺序。 */
    header[39] = product_ui_capture_case_length();
    memcpy(header + 40, s_capture_case, PRODUCT_UI_CAPTURE_CASE_SIZE);
}

/**
 * @brief 按需打开 UART2 并分块提交有限数据。
 * @param[in] data 调用期间借用的待发送数据。
 * @param[in] length 发送字节数。
 * @return 打开或任一负写返回 -1，其余返回 0；当前不另核对非负短写长度。
 * @note 在观察器路径同步执行；成功只沿用设备写接口当前检查，不提供更强传输确认。
 */
static int product_ui_capture_write(const uint8_t *data, uint32_t length)
{
    uint32_t offset;

    if (s_capture_uart == NULL) {
        s_capture_uart = dev_open("uart2", NULL);
        if (s_capture_uart == NULL) {
            return -1;
        }
    }
    for (offset = 0; offset < length;) {
        uint32_t remaining = length - offset;
        uint32_t chunk = remaining > PRODUCT_UI_CAPTURE_UART_CHUNK ?
                         PRODUCT_UI_CAPTURE_UART_CHUNK : remaining;
        if (dev_write(s_capture_uart, (void *)(data + offset), chunk) < 0) {
            return -1;
        }
        offset += chunk;
    }
    return 0;
}

/**
 * @brief 只接受匹配截图过渡编号的完成帧，成功后发送帧头及像素并结束本请求。
 * @param[in] frame 观察回调期间借用的帧及像素。
 * @param[in] transfer_result 显示传输结果，非零结束本次捕获但不导出。
 * @param[in] user_data 观察器上下文，当前未使用。
 * @note 不保留帧指针；捕获完毕或传输失败才清 pending。
 */
static void product_ui_capture_frame_observer(
    const gc9307_transition_frame_t *frame, int transfer_result,
    void *user_data)
{
    uint8_t header[PRODUCT_UI_CAPTURE_HEADER_SIZE];
    uint32_t frame_crc;
    int result;

    (void)user_data;
    if (!s_capture_pending || (frame == NULL) ||
        (frame->transition_id != s_capture_transition_id)) {
        return;
    }
    if (transfer_result != 0) {
        s_capture_pending = 0U;
        printf("[ProductUI-VISUAL] transfer failed case=%s id=%u\n",
               s_capture_case, frame->transition_id);
        return;
    }
    frame_crc = product_ui_capture_crc32(frame->frame_data,
                                         frame->frame_bytes);
    product_ui_capture_build_header(header, frame, frame_crc);
    result = product_ui_capture_write(header, sizeof(header));
    if (result == 0) {
        result = product_ui_capture_write(frame->frame_data,
                                          frame->frame_bytes);
    }
    s_capture_pending = 0U;
    printf("[ProductUI-VISUAL] capture case=%s id=%u bytes=%u crc=%08x result=%d\n",
           s_capture_case, frame->transition_id, frame->frame_bytes,
           frame_crc, result);
}

/**
 * @brief 为单次截图生成专用过渡编号并使活动屏幕失效重绘。
 * @param[in] timer LVGL 单次定时器，当前未读取内容。
 * @note 无活动屏幕时清除待捕获状态，不导出未取得的帧。
 */
static void product_ui_capture_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *screen;

    (void)timer;
    s_capture_timer = NULL;
    s_capture_sequence++;
    s_capture_transition_id = PRODUCT_UI_CAPTURE_ID_BASE |
                              s_capture_sequence;
    s_capture_pending = 1U;
    d_page_transition_begin(PAGE_TRANSITION_TE_FRAME,
                            s_capture_transition_id);
    screen = lv_screen_active();
    if (screen != NULL) {
        lv_obj_invalidate(screen);
    } else {
        s_capture_pending = 0U;
    }
}

/**
 * @brief 在截图诊断启用时安装过渡帧观察器，关闭时为空操作。
 */
int product_ui_visual_capture_init(void)
{
    gc9307_transition_set_frame_observer(
        product_ui_capture_frame_observer, NULL);
    return 0;
}

/**
 * @brief 在截图诊断启用时排入一次延后帧捕获。
 */
int product_ui_visual_capture_request(const char *case_id)
{
    uint32_t length;

    if ((case_id == NULL) || (case_id[0] == '\0') ||
        s_capture_pending || (s_capture_timer != NULL)) {
        return -1;
    }
    memset(s_capture_case, 0, sizeof(s_capture_case));
    length = (uint32_t)strlen(case_id);
    if (length >= sizeof(s_capture_case)) {
        length = sizeof(s_capture_case) - 1U;
    }
    memcpy(s_capture_case, case_id, length);
    s_capture_timer = lv_timer_create(product_ui_capture_timer_cb,
                                      PRODUCT_UI_CAPTURE_DELAY_MS, NULL);
    if (s_capture_timer == NULL) {
        return -1;
    }
    lv_timer_set_repeat_count(s_capture_timer, 1);
    return 0;
}

#else

/**
 * @brief 视觉截图诊断关闭时保留入口并返回零。
 */
int product_ui_visual_capture_init(void)
{
    return 0;
}

/**
 * @brief 视觉截图诊断关闭时保留入口并返回零。
 */
int product_ui_visual_capture_request(const char *case_id)
{
    (void)case_id;
    return 0;
}

#endif /* D_DEBUG_VISUAL_CAPTURE */
#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
