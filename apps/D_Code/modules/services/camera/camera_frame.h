/**
 * @file camera_frame.h
 * @brief Bounded RGB leases and fixed-deadline preview pacing.
 * @author D_Code
 * @date 2026-09-11
 */
#ifndef __CAMERA_FRAME_H__
#define __CAMERA_FRAME_H__
#include "camera_preview.h"

#define CAMERA_FRAME_INPUT_WIDTH    640U
#define CAMERA_FRAME_INPUT_HEIGHT   480U
#define CAMERA_FRAME_INPUT_BYTES    (CAMERA_FRAME_INPUT_WIDTH * CAMERA_FRAME_INPUT_HEIGHT * 3U / 2U)
#define CAMERA_FRAME_SCALED_BYTES   (CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * 3U / 2U)
#define CAMERA_FRAME_SLOT_COUNT     2U
#define CAMERA_FRAME_PERIOD_MS      100U

typedef enum {
    CAMERA_FRAME_FREE = 0,
    CAMERA_FRAME_WRITING,
    CAMERA_FRAME_READY,
    CAMERA_FRAME_READING,
} camera_frame_slot_state_e;

typedef struct {
    camera_frame_slot_state_e state;
    uint32_t session;
    uint32_t sequence;
} camera_frame_lease_t;

typedef struct {
    uint8_t pixels[CAMERA_FRAME_SLOT_COUNT][CAMERA_PREVIEW_RGB_BYTES] __attribute__((aligned(32)));
    camera_frame_lease_t leases[CAMERA_FRAME_SLOT_COUNT];
    uint32_t sequence;
    uint32_t next_process_ms;
} camera_frame_pool_t;

/* 内部元数据接口：owner保证pool非空并持短临界区；转换和像素复制在临界区外。 */
/** @brief 仅服务创建时清空租约，不能在会话停止时重置活跃读写槽。 */
void camera_frame_init(camera_frame_pool_t *pool);
/** @return 可写槽号；无槽为-1。只替换未读取的READY帧，不能覆盖READING/WRITING。 */
int camera_frame_acquire_write(camera_frame_pool_t *pool, uint32_t session);
/** @brief 归还写租约；允许发布时只保留最新READY帧，否则释放该槽。 */
void camera_frame_finish_write(camera_frame_pool_t *pool, int slot, uint8_t can_publish);
/** @return 当前会话的新帧读租约，或-1；sequence为上次成功消费的帧号。 */
int camera_frame_acquire_read(camera_frame_pool_t *pool, uint32_t session, uint32_t sequence);
/** @brief 复制结束后归还读租约；停止不能代替读者提前释放。 */
void camera_frame_release_read(camera_frame_pool_t *pool, int slot);
/** @brief 只释放无人读取的READY帧，保留活跃读写租约。 */
void camera_frame_discard_ready(camera_frame_pool_t *pool);
/** @return 所有槽为FREE时返回1；调用者仍需独立确认SDK已关闭。 */
uint8_t camera_frame_is_idle(const camera_frame_pool_t *pool);
/** @brief 到期时领取处理机会，并将100ms期限推进到now之后；会修改pool。
 *  @return 已到期并推进为1，未到期为0且不改期限；落后时跳过旧期限，不追赶积压。
 *  即使随后没有可写槽，本次机会也已消费。同一时刻再次调用不会再获得机会。
 */
uint8_t camera_frame_advance_if_due(camera_frame_pool_t *pool, uint32_t now_ms);
/** @brief 单个相机worker调用；输入VGA YUV420，输出页面原生小端RGB565。
 *  缩放必须返回完整CAMERA_FRAME_SCALED_BYTES；内部静态缩放缓冲禁止并发使用。
 */
d_err_t camera_frame_convert(uint8_t *input, uint8_t *rgb);

#endif
