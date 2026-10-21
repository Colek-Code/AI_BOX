/**
 * @file camera_frame.c
 * @brief Latest-frame ownership; never overwrite an active reader or writer.
 * @author D_Code
 * @date 2026-09-11
 */
#include "camera_frame.h"
#include "yuv_soft_scalling.h"
#include "yuv_to_rgb.h"
#include <string.h>

#define CAMERA_FRAME_SDK_RGB565_BIG_ENDIAN 1

static uint8_t s_camera_scaled_yuv[CAMERA_FRAME_SCALED_BYTES] __attribute__((aligned(32)));

void camera_frame_init(camera_frame_pool_t *pool)
{
    /* Only at service creation. Session teardown must preserve live leases. */
    memset(pool->leases, 0, sizeof(pool->leases));
    pool->sequence = 0U;
    pool->next_process_ms = 0U;
}

int camera_frame_acquire_write(camera_frame_pool_t *pool, uint32_t session)
{
    int slot;
    int candidate = -1;
    for (slot = 0; slot < (int)CAMERA_FRAME_SLOT_COUNT; slot++) {
        if (pool->leases[slot].state == CAMERA_FRAME_FREE) {
            candidate = slot;
            break;
        }
        if (pool->leases[slot].state == CAMERA_FRAME_READY) {
            candidate = slot;
        }
    }
    if (candidate >= 0) {
        pool->leases[candidate].state = CAMERA_FRAME_WRITING;
        pool->leases[candidate].session = session;
    }
    return candidate;
}

void camera_frame_discard_ready(camera_frame_pool_t *pool)
{
    unsigned slot;
    for (slot = 0U; slot < CAMERA_FRAME_SLOT_COUNT; slot++) {
        if (pool->leases[slot].state == CAMERA_FRAME_READY) {
            pool->leases[slot].state = CAMERA_FRAME_FREE;
        }
    }
}

void camera_frame_finish_write(camera_frame_pool_t *pool, int slot, uint8_t can_publish)
{
    if (slot < 0 || slot >= (int)CAMERA_FRAME_SLOT_COUNT ||
        pool->leases[slot].state != CAMERA_FRAME_WRITING) {
        return;
    }
    if (!can_publish) {
        pool->leases[slot].state = CAMERA_FRAME_FREE;
        return;
    }
    camera_frame_discard_ready(pool);
    if (++pool->sequence == 0U) {
        ++pool->sequence;
    }
    pool->leases[slot].sequence = pool->sequence;
    pool->leases[slot].state = CAMERA_FRAME_READY;
}

int camera_frame_acquire_read(camera_frame_pool_t *pool, uint32_t session, uint32_t sequence)
{
    unsigned slot;
    for (slot = 0U; slot < CAMERA_FRAME_SLOT_COUNT; slot++) {
        camera_frame_lease_t *lease = &pool->leases[slot];
        if (lease->state == CAMERA_FRAME_READY && lease->session == session && lease->sequence != sequence) {
            lease->state = CAMERA_FRAME_READING;
            return (int)slot;
        }
    }
    return -1;
}

void camera_frame_release_read(camera_frame_pool_t *pool, int slot)
{
    if (slot >= 0 && slot < (int)CAMERA_FRAME_SLOT_COUNT &&
        pool->leases[slot].state == CAMERA_FRAME_READING) {
        pool->leases[slot].state = CAMERA_FRAME_FREE;
    }
}

uint8_t camera_frame_is_idle(const camera_frame_pool_t *pool)
{
    unsigned slot;
    for (slot = 0U; slot < CAMERA_FRAME_SLOT_COUNT; slot++) {
        if (pool->leases[slot].state != CAMERA_FRAME_FREE) {
            return 0U;
        }
    }
    return 1U;
}

uint8_t camera_frame_advance_if_due(camera_frame_pool_t *pool, uint32_t now_ms)
{
    uint32_t late_ms = now_ms - pool->next_process_ms;
    if ((int32_t)late_ms < 0) {
        return 0U;
    }
    /* Preserve the 100 ms grid with 15 fps input; skip missed deadlines, never catch up. */
    pool->next_process_ms += (late_ms / CAMERA_FRAME_PERIOD_MS + 1U) * CAMERA_FRAME_PERIOD_MS;
    return 1U;
}

d_err_t camera_frame_convert(uint8_t *input, uint8_t *rgb)
{
    int scaled_bytes;
    uint32_t offset;
    if (input == NULL || rgb == NULL) {
        return D_ERR_INVALID_ARG;
    }
    scaled_bytes = YUV420p_Soft_Scaling(input, s_camera_scaled_yuv,
                                       CAMERA_FRAME_INPUT_WIDTH, CAMERA_FRAME_INPUT_HEIGHT,
                                       CAMERA_PREVIEW_WIDTH, CAMERA_PREVIEW_HEIGHT);
    if (scaled_bytes != (int)CAMERA_FRAME_SCALED_BYTES) {
        return D_ERR_IO;
    }
    /* Current SDK ignores be and always emits MSB first. Request that format explicitly,
     * then normalize only the camera image to LVGL's native little-endian RGB565. */
    yuv420p_quto_rgb565(s_camera_scaled_yuv, rgb, CAMERA_PREVIEW_WIDTH, CAMERA_PREVIEW_HEIGHT,
                       CAMERA_FRAME_SDK_RGB565_BIG_ENDIAN);
    for (offset = 0U; offset < CAMERA_PREVIEW_RGB_BYTES; offset += 2U) {
        uint8_t high_byte = rgb[offset];
        rgb[offset] = rgb[offset + 1U];
        rgb[offset + 1U] = high_byte;
    }
    return D_OK;
}
