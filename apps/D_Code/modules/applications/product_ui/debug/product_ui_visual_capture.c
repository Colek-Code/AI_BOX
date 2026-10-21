/**
 * @file product_ui_visual_capture.c
 * @brief UART2 export of the existing GC9307 transition staging frame.
 */

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

static void product_ui_capture_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

static void product_ui_capture_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

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
    product_ui_capture_put_u16(header + 36, D_GC9307_ROTATION_DEGREES);
    header[38] = 1U; /* RGB565 bytes are panel order: big endian. */
    header[39] = product_ui_capture_case_length();
    memcpy(header + 40, s_capture_case, PRODUCT_UI_CAPTURE_CASE_SIZE);
}

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

int product_ui_visual_capture_init(void)
{
    gc9307_transition_set_frame_observer(
        product_ui_capture_frame_observer, NULL);
    return 0;
}

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

int product_ui_visual_capture_init(void)
{
    return 0;
}

int product_ui_visual_capture_request(const char *case_id)
{
    (void)case_id;
    return 0;
}

#endif /* D_DEBUG_VISUAL_CAPTURE */
#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
