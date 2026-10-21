/** @file ota_sdk_wl82.c @brief Serialized passive-update ownership and callbacks. */
#include "config/app_config.h"
#if D_FEATURE_OTA
#include "ota_port.h"
#include "system/includes.h"
#include "update/dual_bank_updata_api.h"

/* Existing SDK export used by net_update.c to align the initial packet. */
extern uint32_t get_target_udate_addr(void);
static struct {
    uint8_t owned;
    uint8_t pending;
    uint8_t in_call;
    d_err_t result;
    d_err_t write_error;
    uint16_t first_size;
} s_sdk;

static void ota_sdk_result(d_err_t result)
{
    ota_port_lock();
    s_sdk.result = result;
    s_sdk.pending = 0U;
    ota_port_unlock();
}
static int ota_written(void *error)
{
    /* A write can be buffered without any callback. Never use this callback
     * to complete a later VERIFY/COMMIT command. Keep errors sticky instead. */
    if ((intptr_t)error != 0) {
        ota_port_lock(); s_sdk.write_error = D_ERR_IO; ota_port_unlock();
    }
    return 0;
}
static int ota_verified(int passed)
{ ota_sdk_result(passed == 1 ? D_OK : D_ERR_IO); return 0; }
static int ota_committed(int error)
{ ota_sdk_result(error == 0 ? D_OK : D_ERR_IO); return 0; }

static void ota_sdk_call_begin(void)
{
    ota_port_lock();
    s_sdk.pending = s_sdk.in_call = 1U;
    s_sdk.result = D_ERR_BUSY;
    ota_port_unlock();
}

static d_err_t ota_sdk_wait(uint32_t immediate)
{
    d_err_t result;
    ota_port_lock(); s_sdk.in_call = 0U; ota_port_unlock();
    if (immediate != 0U) return D_ERR_IO;
    /* The SDK does not provide cancellation/queue admission. A missing result
     * retains this worker and static buffers. The independent observer exposes
     * deadline failure; a late callback can still finish this same operation. */
    for (;;) {
        uint8_t pending;
        ota_port_lock(); pending = s_sdk.pending; result = s_sdk.result; ota_port_unlock();
        if (!pending) return result;
        ota_service_observe_deadline();
        ota_port_wait(10U);
    }
}

d_err_t ota_sdk_begin(void)
{
    uint32_t result, address, remainder;
    if (s_sdk.owned || get_dual_bank_passive_update_max_buf() != 0U) return D_ERR_BUSY;
    result = dual_bank_passive_update_init(0U, 0U, OTA_PACKET_SIZE, NULL);
    s_sdk.write_error = D_OK;
    s_sdk.owned = get_dual_bank_passive_update_max_buf() != 0U;
    if (result || !s_sdk.owned) {
        /* A partial SDK initialization with a remaining context is uncertain. */
        s_sdk.pending = s_sdk.owned;
        return D_ERR_IO;
    }
    ota_port_lock(); s_sdk.in_call = 1U; ota_port_unlock();
    result = dual_bank_update_allow_check(0U);
    ota_port_lock(); s_sdk.in_call = 0U; ota_port_unlock();
    if (result) return D_ERR_IO;
    address = get_target_udate_addr();
    remainder = address % OTA_PACKET_SIZE;
    s_sdk.first_size = (uint16_t)(remainder ? OTA_PACKET_SIZE - remainder : OTA_PACKET_SIZE);
    return D_OK;
}
uint16_t ota_sdk_first_size(void) { return s_sdk.first_size; }

d_err_t ota_sdk_write(const uint8_t *bytes, uint16_t length)
{
    uint32_t immediate;
    d_err_t result;
    if (!s_sdk.owned || !bytes || !length || length > OTA_PACKET_SIZE || ota_sdk_pending())
        return D_ERR_INVALID_ARG;
    ota_port_lock(); s_sdk.in_call = 1U; ota_port_unlock();
    /* Pinned SDK copies into its own cbuffer and, if it posts a Flash write,
     * waits for that write's semaphore before returning. A buffered packet
     * returns without a callback. This input buffer never escapes the call. */
    immediate = dual_bank_update_write((void *)bytes, length, ota_written);
    ota_port_lock();
    s_sdk.in_call = 0U;
    result = immediate ? D_ERR_IO : s_sdk.write_error;
    ota_port_unlock();
    return result;
}
d_err_t ota_sdk_verify(void)
{
    d_err_t result;
    if (!s_sdk.owned || ota_sdk_pending()) return D_ERR_NOT_READY;
    ota_sdk_call_begin();
    result = ota_sdk_wait(dual_bank_update_verify(NULL, NULL, ota_verified));
    ota_port_lock();
    if (s_sdk.write_error != D_OK) result = s_sdk.write_error;
    ota_port_unlock();
    return result;
}
d_err_t ota_sdk_commit(void)
{
    if (!s_sdk.owned || ota_sdk_pending()) return D_ERR_NOT_READY;
    ota_sdk_call_begin();
    return ota_sdk_wait(dual_bank_update_burn_boot_info(ota_committed));
}
uint8_t ota_sdk_pending(void)
{
    uint8_t pending;
    ota_port_lock(); pending = s_sdk.pending || s_sdk.in_call; ota_port_unlock();
    return pending;
}
d_err_t ota_sdk_close(void)
{
    if (!s_sdk.owned) return D_OK;
    if (ota_sdk_pending()) return D_ERR_BUSY;
    /* Only the OTA worker calls exit, never the dw_update callback task. */
    if (dual_bank_passive_update_exit(NULL) != 0U || get_dual_bank_passive_update_max_buf() != 0U)
        return D_ERR_BUSY;
    s_sdk.owned = 0U;
    return D_OK;
}
#endif
