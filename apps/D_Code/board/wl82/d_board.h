/**
 * @file d_board.h
 * @brief Board-level platform interface for D_Code (WL82 variant)
 * @author D_Code
 * @date 2026-06-04
 */

#ifndef D_BOARD_H
#define D_BOARD_H

/**
 * @brief Switch wakeup configuration to soft-poweroff mode.
 *
 * Must be called immediately before power_set_soft_poweroff().
 * Enables long-press wakeup on PORT_WAKEUP_IO and clears the plain
 * POWER_OFF_WAKEUP to prevent the device from re-waking instantly
 * while the key is still held down during shutdown.
 */
void d_board_prepare_soft_poweroff_wakeup(void);

#endif /* D_BOARD_H */
