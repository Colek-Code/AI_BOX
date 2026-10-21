#ifndef D_BOARD_CONFIG_H
#define D_BOARD_CONFIG_H

/*=============================================================================
 * D_Code 硬件配置菜单
 *
 * 这是普通开发者需要查看的第一个配置文件。
 * 修改下面一行即可切换整套板级硬件，不需要修改 Makefile。
 *===========================================================================*/

#define D_BOARD_COPY_GC9307_HYN      1001
#define D_BOARD_COPY_EMI_ILI948X     1002

/*--------------------------------------------------------------------------
 * 1. 选择开发板：只能填写一个可选值
 *
 * D_BOARD_COPY_GC9307_HYN
 *   Copy_board：GC9307 屏、HYN/CST8xxT 触摸，LVGL 为 320×240 横屏。
 *
 * D_BOARD_COPY_EMI_ILI948X
 *   Copy_board 旧硬件：EMI/ILI948x 屏、GT911/FT6236 触摸。
 *-------------------------------------------------------------------------*/

#define D_BOARD_TARGET  D_BOARD_COPY_GC9307_HYN

#endif /* D_BOARD_CONFIG_H */
