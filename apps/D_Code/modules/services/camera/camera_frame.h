/**
 * @file camera_frame.h
 * @brief 双 RGB 槽的占用票据与固定期限预览节拍。
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
/**
 * @brief 只在服务创建时清空槽状态及节拍。
 * @param[in,out] pool 帧池；调用者保证非空并在元数据操作期间持有短临界区。
 * @note 停止会话时不可调用，不能撤销在途读写占用。
 */
void camera_frame_init(camera_frame_pool_t *pool);
/**
 * @brief 领取空闲槽或替换尚未被读取的 READY 槽。
 * @param[in,out] pool 帧池；调用者保证非空并在元数据操作期间持有短临界区。
 * @param[in] session 写入帧所属会话编号。
 * @return 可写槽号；没有可用槽返回 -1。
 * @note 不覆盖 READING 或 WRITING 槽。
 */
int camera_frame_acquire_write(camera_frame_pool_t *pool, uint32_t session);
/**
 * @brief 结束写占用，按准入结果发布最新帧或释放槽。
 * @param[in,out] pool 帧池；调用者保证非空并在元数据操作期间持有短临界区。
 * @param[in] slot 先前领取的写槽号；无效或状态不符时不处理。
 * @param[in] can_publish 非零允许发布，并丢弃其他未读取 READY 帧。
 */
void camera_frame_finish_write(camera_frame_pool_t *pool, int slot, uint8_t can_publish);
/**
 * @brief 为当前会话领取一张未消费的新帧。
 * @param[in,out] pool 帧池；调用者保证非空并在元数据操作期间持有短临界区。
 * @param[in] session 待读取会话编号。
 * @param[in] sequence 上次成功消费的帧序号。
 * @return 成功返回读槽号；无新帧返回 -1。
 */
int camera_frame_acquire_read(camera_frame_pool_t *pool, uint32_t session, uint32_t sequence);
/**
 * @brief 像素复制结束后归还读占用。
 * @param[in,out] pool 帧池；调用者保证非空并在元数据操作期间持有短临界区。
 * @param[in] slot 先前领取的读槽号；无效或状态不符时不处理。
 * @note 停止会话不能代替读者提前归还。
 */
void camera_frame_release_read(camera_frame_pool_t *pool, int slot);
/**
 * @brief 释放尚未读取的 READY 帧，保留在途读写占用。
 * @param[in,out] pool 帧池；调用者保证非空并在元数据操作期间持有短临界区。
 */
void camera_frame_discard_ready(camera_frame_pool_t *pool);
/**
 * @brief 检查帧池全部槽是否已归还。
 * @param[in] pool 非空帧池；调用者序列化元数据访问。
 * @return 全部为 FREE 返回 1，否则返回 0。
 * @note 空闲不证明 SDK 已关闭。
 */
uint8_t camera_frame_is_idle(const camera_frame_pool_t *pool);
/**
 * @brief 到期时消费一次处理机会，将期限推进至 now_ms 之后。
 * @param[in,out] pool 帧池；调用者保证非空并在元数据操作期间持有短临界区。
 * @param[in] now_ms 当前毫秒计时值。
 * @return 到期并推进返回 1；未到期返回 0 且不改期限。
 * @note 落后时跳过旧期限而不追赶积压；后续即使未领取写槽，本次机会也已消费。
 */
uint8_t camera_frame_advance_if_due(camera_frame_pool_t *pool, uint32_t now_ms);
/**
 * @brief 将 VGA YUV420 转为页面尺寸的小端 RGB565。
 * @param[in] input 至少 CAMERA_FRAME_INPUT_BYTES 字节的 YUV420 输入。
 * @param[out] rgb 至少 CAMERA_PREVIEW_RGB_BYTES 字节的目标缓冲。
 * @return 成功返回 D_OK；空指针返回 D_ERR_INVALID_ARG；缩放长度异常返回 D_ERR_IO。
 * @note 仅由单个相机工作任务调用；内部静态缩放缓冲不支持并发复用。
 */
d_err_t camera_frame_convert(uint8_t *input, uint8_t *rgb);

#endif
