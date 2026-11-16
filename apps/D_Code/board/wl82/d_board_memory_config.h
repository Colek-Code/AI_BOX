/**
 * @file d_board_memory_config.h
 * @brief 定义链接器可见的 WL82 Flash 与 SDRAM 容量，不选择业务功能。
 * @note 板型由 config/board_config.h 选择，d_board_resolve.h 解析显示、触摸及外设配置。
 * 当前容量：GC9307/HYN Flash 为 16 MB，旧 EMI 板为 8 MB；外挂 SDRAM 为 8 MB（4M×16 位）。
 * __FLASH_SIZE__ 直接影响 sdk.ld 地址分配；修改前必须核对实际器件丝印或原理图。
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
