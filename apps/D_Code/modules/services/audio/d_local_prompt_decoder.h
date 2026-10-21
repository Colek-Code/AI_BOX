/** @file d_local_prompt_decoder.h @brief Decoder adapter owned exclusively by local_prompt task. */
#ifndef D_LOCAL_PROMPT_DECODER_H
#define D_LOCAL_PROMPT_DECODER_H
#include "framework/d_error.h"
#include <stdint.h>
d_err_t d_local_prompt_decoder_start(uint32_t generation, uint8_t volume);
d_err_t d_local_prompt_decoder_start_alarm(uint32_t generation, uint8_t volume);
/** Revalidate the owner after OPEN and before START; cancellation keeps the
 * opened server owned until the caller performs checked STOP. */
d_err_t d_local_prompt_decoder_start_checked(uint32_t generation, uint8_t volume, uint8_t repeating,
    d_err_t (*permit_start)(uint32_t generation));
d_err_t d_local_prompt_decoder_stop(void);
d_err_t d_local_prompt_decoder_volume(uint8_t volume);
uint8_t d_local_prompt_decoder_finished(void);
d_err_t d_local_prompt_decoder_result(void);
#endif
