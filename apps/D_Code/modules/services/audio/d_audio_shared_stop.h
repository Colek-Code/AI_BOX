/** @file d_audio_shared_stop.h @brief Checked drain of the shared Qwen SDK audio owner. */
#ifndef D_AUDIO_SHARED_STOP_H
#define D_AUDIO_SHARED_STOP_H
#include "framework/d_error.h"
#include <stdint.h>

/**
 * Qwen lifecycle worker only, after its producer queues have acknowledged stop.
 * Drains the lower audio queue and confirms encoder/decoder STOP results.
 * Timeout retains the one outstanding barrier; a retry joins that operation.
 * Failure never transfers ownership. No UI or audio-owner thread may wait here.
 */
d_err_t d_audio_shared_stop_wait(uint32_t timeout_ms);
#endif
