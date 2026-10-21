/** @file xiaole_sdk_release.h @brief Keep XiaoLe buffers alive until both library workers exit. */
#ifndef XIAOLE_SDK_RELEASE_H
#define XIAOLE_SDK_RELEASE_H

#include "framework/d_error.h"
#include <stdint.h>

/* Only the pinned WakeupApi.o is rebound to this function by the build adapter. */
void xiaole_sdk_free(void *memory);
/* Called by the XiaoLe owner before lx_aec_free(), never by an SDK callback. */
d_err_t xiaole_sdk_release_begin(void);
/* Call only after lx_aec_free() has successfully joined both internal workers. */
d_err_t xiaole_sdk_release_complete(uint16_t *released);

#endif
