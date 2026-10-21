/**
 * @file d_sdk_system_config.h
 * @brief JieLi system, memory, diagnostics and power configuration.
 */

#ifndef D_SDK_SYSTEM_CONFIG_H
#define D_SDK_SYSTEM_CONFIG_H

#define CONFIG_CXX_SUPPORT
#define RTOS_STACK_CHECK_ENABLE
#define CONFIG_DOUBLE_BANK_ENABLE           D_OTA_DOUBLE_BANK

#ifdef CONFIG_NO_SDRAM_ENABLE
#undef __SDRAM_SIZE__
#define __SDRAM_SIZE__                      (0 * 1024 * 1024)
#endif

#if defined __SDRAM_SIZE__ && (__SDRAM_SIZE__ == 0)
#define CONFIG_NO_SDRAM_ENABLE              1
#endif

#if !(D_DEBUG_VISUAL_CAPTURE)
#define CONFIG_DEBUG_ENABLE
#endif

#if !defined CONFIG_DEBUG_ENABLE || defined CONFIG_LIB_DEBUG_DISABLE
#define LIB_DEBUG                           0
#else
#define LIB_DEBUG                           1
#endif
#define CONFIG_DEBUG_LIB(x)                 (x & LIB_DEBUG)

#define TCFG_LOWPOWER_BTOSC_DISABLE         0
#ifdef CONFIG_LOW_POWER_ENABLE
#define TCFG_LOWPOWER_LOWPOWER_SEL          \
    (RF_SLEEP_EN | SYS_SLEEP_EN | RF_FORCE_SYS_SLEEP_EN | SYS_SLEEP_BY_IDLE)
#else
#define TCFG_LOWPOWER_LOWPOWER_SEL          0
#endif

#ifdef CONFIG_INTERNAL_VDDIO_POWER_SUPPLY_ENABLE
#ifdef CONFIG_NO_SDRAM_ENABLE
#define TCFG_LOWPOWER_VDDIOM_LEVEL          VDDIOM_VOL_33V
#else
#define TCFG_LOWPOWER_VDDIOM_LEVEL          VDDIOM_VOL_35V
#endif
#else
#define TCFG_LOWPOWER_VDDIOM_LEVEL          VDDIOM_VOL_32V
#endif

#ifdef CONFIG_RTC_ENABLE
#define TCFG_LOWPOWER_VDDIOW_LEVEL          VDDIOW_VOL_32V
#else
#define TCFG_LOWPOWER_VDDIOW_LEVEL          VDDIOW_VOL_21V
#endif

#define VDC14_VOL_SEL_LEVEL                 VDC14_VOL_SEL_140V
#define SYSVDD_VOL_SEL_LEVEL                SYSVDD_VOL_SEL_126V

#endif /* D_SDK_SYSTEM_CONFIG_H */
