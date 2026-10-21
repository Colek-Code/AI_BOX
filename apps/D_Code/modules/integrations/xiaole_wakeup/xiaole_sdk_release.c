/** @file xiaole_sdk_release.c @brief Bounded library-local deferred free, without a global allocator hook. */
#include "config/app_config.h"
#if D_FEATURE_XIAOLE_WAKEUP
#include "xiaole_sdk_release.h"
#include "system/includes.h"
#include "spinlock.h"

#define XIAOLE_DEFERRED_FREE_MAX 64U

static DEFINE_SPINLOCK(s_release_lock);
static struct {
    void *memory[XIAOLE_DEFERRED_FREE_MAX];
    uint16_t count;
    uint8_t active;
    d_err_t error;
} s_release;

void xiaole_sdk_free(void *memory)
{
    unsigned index;
    uint8_t deferred;
    if (memory == NULL) return;
    spin_lock(&s_release_lock);
    deferred = s_release.active;
    if (deferred) {
        for (index = 0U; index < s_release.count; index++) {
            if (s_release.memory[index] == memory) break;
        }
        if (index != s_release.count) s_release.error = D_ERR_IO;
        else if (s_release.count == XIAOLE_DEFERRED_FREE_MAX) s_release.error = D_ERR_FULL;
        else s_release.memory[s_release.count++] = memory;
    }
    spin_unlock(&s_release_lock);
    /* Overflow/duplicate frees fail closed: retain memory until reboot instead of freeing a live buffer. */
    if (!deferred) free(memory);
}

d_err_t xiaole_sdk_release_begin(void)
{
    d_err_t result = D_OK;
    spin_lock(&s_release_lock);
    if (s_release.error != D_OK) result = s_release.error;
    else if (s_release.active || s_release.count) result = D_ERR_BUSY;
    else s_release.active = 1U;
    spin_unlock(&s_release_lock);
    return result;
}

d_err_t xiaole_sdk_release_complete(uint16_t *released)
{
    uint16_t count = 0U;
    d_err_t result;
    void *memory;
    if (released != NULL) *released = 0U;
    for (;;) {
        spin_lock(&s_release_lock);
        result = s_release.error;
        if (result == D_OK && !s_release.active) result = D_ERR_NOT_READY;
        if (result != D_OK) {
            spin_unlock(&s_release_lock);
            return result;
        }
        if (s_release.count == 0U) {
            s_release.active = 0U;
            spin_unlock(&s_release_lock);
            if (released != NULL) *released = count;
            return D_OK;
        }
        memory = s_release.memory[--s_release.count];
        s_release.memory[s_release.count] = NULL;
        spin_unlock(&s_release_lock);
        /* Both library workers have exited; no storage, SDK or heap operation occurs under our lock. */
        free(memory);
        count++;
    }
}
#endif
