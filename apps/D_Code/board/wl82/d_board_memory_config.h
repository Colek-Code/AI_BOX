/**
 * @file d_board_memory_config.h
 * @brief Linker-visible Flash and SDRAM capacities for the D_Code WL82 board.
 *
 * This file owns memory-capacity constants only. Select the hardware board in
 * config/board_config.h; config/internal/d_board_resolve.h maps that selection
 * to the matching display, touch and peripheral profile.
 *
 * Current firmware capacity contract:
 *   - Flash: GC9307/HYN 16 MB; legacy EMI hardware 8 MB.
 *   - SDRAM: 8 MB external SDRAM (4M x 16-bit, expressed as bytes).
 *
 * __FLASH_SIZE__ directly affects the sdk.ld address-space allocation. Both
 * capacity values must match the assembled memory devices; verify the chip
 * marking or schematic before changing either value.
 */

#ifndef D_BOARD_MEMORY_CONFIG_H
#define D_BOARD_MEMORY_CONFIG_H
#include "config/board_config.h"

#ifndef __FLASH_SIZE__
#if D_BOARD_TARGET == D_BOARD_COPY_GC9307_HYN
#define __FLASH_SIZE__    (16 * 1024 * 1024)
#else
#define __FLASH_SIZE__    (8 * 1024 * 1024)
#endif
#endif

#ifndef __SDRAM_SIZE__
#define __SDRAM_SIZE__    (8 * 1024 * 1024)
#endif

#endif /* D_BOARD_MEMORY_CONFIG_H */
