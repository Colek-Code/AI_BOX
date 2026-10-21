/**
 * @file product_ui_clock.c
 * @brief Shared software clock with a UI-independent sampling owner.
 * @author D_Code
 * @date 2026-09-21
 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_clock.h"
#include "product_ui_clock_port.h"
#include "product_ui_time_domain_internal.h"
#include "system/spinlock.h"
#include <stddef.h>

static DEFINE_SPINLOCK(s_clock_lock);
static product_ui_clock_model_t s_clock;
static uint16_t s_clock_timer;
static uint32_t s_clock_sample_ms;
static uint8_t s_clock_attempted;
static uint8_t s_clock_ready;
static uint8_t s_clock_sample_now;
static uint8_t s_clock_quiescing;
static uint32_t s_clock_sample_generation;

static void product_ui_clock_tick(void *context)
{
    uint32_t now = product_ui_clock_port_ms();
    uint8_t sample;
    uint32_t sample_revision;
    uint32_t sample_generation;
    int64_t seconds = 0;
    product_ui_time_unavailable_reason_e reason;
    (void)context;
    spin_lock(&s_clock_lock);
    if (!s_clock_ready) { spin_unlock(&s_clock_lock); return; }
    product_ui_clock_model_tick(&s_clock, now);
    sample = !s_clock_quiescing && s_clock.state.automatic && (s_clock_sample_now || (uint32_t)(now - s_clock_sample_ms) >= 1000U);
    sample_revision = s_clock.state.revision;
    sample_generation = s_clock_sample_generation;
    if (sample) { s_clock_sample_ms = now; s_clock_sample_now = 0U; }
    spin_unlock(&s_clock_lock);
    if (!sample) return;
    reason = product_ui_clock_port_network(&seconds);
    spin_lock(&s_clock_lock);
    /* A concurrent manual command makes the model reject this older network sample. */
    if (s_clock_ready && !s_clock_quiescing && s_clock_sample_generation == sample_generation &&
        s_clock.state.revision == sample_revision && reason == PRODUCT_UI_TIME_UNAVAILABLE_NONE)
        (void)product_ui_clock_model_network(&s_clock, seconds);
    spin_unlock(&s_clock_lock);
}

d_err_t product_ui_clock_init(void)
{
    uint16_t timer;
    uint32_t now = product_ui_clock_port_ms();
    spin_lock(&s_clock_lock);
    if (s_clock_attempted) {
        d_err_t result = s_clock_ready ? D_OK : D_ERR_NOT_READY;
        spin_unlock(&s_clock_lock); return result;
    }
    s_clock_attempted = 1U;
    product_ui_clock_model_init(&s_clock, now);
    s_clock_sample_now = 1U;
    spin_unlock(&s_clock_lock);
    timer = product_ui_clock_port_start(product_ui_clock_tick);
    spin_lock(&s_clock_lock);
    s_clock_timer = timer;
    s_clock_ready = timer != 0U;
    spin_unlock(&s_clock_lock);
    if (timer) product_ui_clock_tick(NULL);
    return timer ? D_OK : D_ERR_IO;
}

void product_ui_clock_get_snapshot(product_ui_clock_snapshot_t *snapshot)
{
    uint32_t now;
    if (snapshot == NULL) return;
    now = product_ui_clock_port_ms();
    spin_lock(&s_clock_lock);
    if (s_clock_ready) product_ui_clock_model_tick(&s_clock, now);
    product_ui_clock_model_snapshot(&s_clock, snapshot);
    if (!s_clock_ready) snapshot->valid = 0U;
    spin_unlock(&s_clock_lock);
}

product_ui_time_unavailable_reason_e product_ui_clock_read(product_ui_datetime_t *value)
{
    product_ui_clock_snapshot_t snapshot;
    if (value == NULL) return PRODUCT_UI_TIME_UNAVAILABLE_SOURCE_INVALID;
    product_ui_clock_get_snapshot(&snapshot);
    if (!snapshot.valid) return PRODUCT_UI_TIME_UNAVAILABLE_NOT_READY;
    return product_ui_time_utc_to_beijing(snapshot.utc_seconds, value) == PRODUCT_UI_TIME_OK ?
        PRODUCT_UI_TIME_UNAVAILABLE_NONE : PRODUCT_UI_TIME_UNAVAILABLE_SOURCE_INVALID;
}

d_err_t product_ui_clock_set_manual(const product_ui_datetime_t *value, uint32_t expected_revision)
{
    product_ui_time_result_e result;
    uint32_t now = product_ui_clock_port_ms();
    spin_lock(&s_clock_lock);
    if (!s_clock_ready || s_clock_quiescing) { spin_unlock(&s_clock_lock); return D_ERR_NOT_READY; }
    if (expected_revision != s_clock.state.revision) { spin_unlock(&s_clock_lock); return D_ERR_BUSY; }
    product_ui_clock_model_tick(&s_clock, now);
    result = product_ui_clock_model_manual(&s_clock, value);
    spin_unlock(&s_clock_lock);
    return result == PRODUCT_UI_TIME_OK ? D_OK : D_ERR_INVALID_ARG;
}

d_err_t product_ui_clock_use_automatic(uint32_t expected_revision)
{
    spin_lock(&s_clock_lock);
    if (!s_clock_ready || s_clock_quiescing) { spin_unlock(&s_clock_lock); return D_ERR_NOT_READY; }
    if (expected_revision != s_clock.state.revision) { spin_unlock(&s_clock_lock); return D_ERR_BUSY; }
    product_ui_clock_model_automatic(&s_clock);
    s_clock_sample_now = 1U;
    spin_unlock(&s_clock_lock);
    product_ui_clock_tick(NULL);
    return D_OK;
}

void product_ui_clock_quiesce_begin(void)
{
    spin_lock(&s_clock_lock);
    s_clock_quiescing = 1U; s_clock_sample_generation++;
    spin_unlock(&s_clock_lock);
}

void product_ui_clock_quiesce_cancel(void)
{
    spin_lock(&s_clock_lock);
    s_clock_quiescing = 0U; s_clock_sample_now = 1U; s_clock_sample_generation++;
    spin_unlock(&s_clock_lock);
}

void product_ui_clock_stop(void)
{
    uint16_t timer;
    spin_lock(&s_clock_lock);
    timer = s_clock_timer;
    s_clock_timer = 0U; s_clock_ready = 0U;
    spin_unlock(&s_clock_lock);
    if (timer) product_ui_clock_port_stop(timer);
}
#endif
