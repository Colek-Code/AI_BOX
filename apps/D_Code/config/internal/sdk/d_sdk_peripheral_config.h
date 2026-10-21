/**
 * @file d_sdk_peripheral_config.h
 * @brief JieLi key, storage and USB configuration owned by board peripherals.
 */

#ifndef D_SDK_PERIPHERAL_CONFIG_H
#define D_SDK_PERIPHERAL_CONFIG_H

#if D_FEATURE_KEY_SERVICE
#define TCFG_ADKEY_ENABLE                    1
#else
#define TCFG_ADKEY_ENABLE                    0
#endif
#define TCFG_IRKEY_ENABLE                    0

#if D_BOARD_IS_EMI_ILI948X
#define TCFG_SD0_ENABLE                      1
#define TCFG_SD1_ENABLE                      0
#else
#define TCFG_SD0_ENABLE                      0
#define TCFG_SD1_ENABLE                      0
#endif

#if TCFG_SD0_ENABLE
#define SDX_DEV                              "sd0"
#define CONFIG_STORAGE_PATH                  "storage/sd0"
#elif TCFG_SD1_ENABLE
#define SDX_DEV                              "sd1"
#define CONFIG_STORAGE_PATH                  "storage/sd1"
#else
#define SDX_DEV                              "sdx"
#define CONFIG_STORAGE_PATH                  "storage/sdx"
#endif

#define FAT_CACHE_NUM                        32
#define CONFIG_UDISK_STORAGE_PATH            "storage/udisk0"
#define CONFIG_ROOT_PATH                     CONFIG_STORAGE_PATH"/C/"
#define CONFIG_UDISK_ROOT_PATH               CONFIG_UDISK_STORAGE_PATH"/C/"
#define CONFIG_MUSIC_PATH_SD                 CONFIG_ROOT_PATH
#define CONFIG_MUSIC_PATH_SD0                "storage/sd0/C/"
#define CONFIG_MUSIC_PATH_SD1                "storage/sd1/C/"
#define CONFIG_MUSIC_PATH_UDISK              CONFIG_UDISK_ROOT_PATH
#define CONFIG_MUSIC_PATH_UDISK0             "storage/udisk0/C/"
#define CONFIG_MUSIC_PATH_UDISK1             "storage/udisk1/C/"
#define CONFIG_EQ_FILE_NAME                  "mnt/sdfile/res/cfg/eq_cfg_hw.bin"

#if defined CONFIG_VOICE_PROMPT_FILE_SAVE_IN_RESERVED_EXPAND_ZONE
#define CONFIG_MUSIC_PATH_FLASH              "mnt/sdfile/EXT_RESERVED/EXT_RESERVED/aupackres/"
#define CONFIG_VOICE_PROMPT_FILE_PATH        "mnt/sdfile/EXT_RESERVED/aupackres/tone/"
#elif defined CONFIG_VOICE_PROMPT_FILE_SAVE_IN_RESERVED_ZONE
#define CONFIG_MUSIC_PATH_FLASH              "mnt/sdfile/app/app_area_head/aupackres/"
#define CONFIG_VOICE_PROMPT_FILE_PATH        "mnt/sdfile/app/aupackres/tone/"
#else
#define CONFIG_MUSIC_PATH_FLASH              "mnt/sdfile/res/"
#define CONFIG_VOICE_PROMPT_FILE_PATH        "mnt/sdfile/res/audlogo/"
#endif

#define CONFIG_UI_PATH_FLASH                 "mnt/sdfile/res/ui_res/"
#define CONFIG_UPGRADE_PATH                  CONFIG_ROOT_PATH"update.ufw"
#define CONFIG_UDISK_UPGRADE_PATH            CONFIG_UDISK_ROOT_PATH"update.ufw"
#define CONFIG_REC_PATH_0                    CONFIG_ROOT_PATH"DCIM/1/"

#if TCFG_SD0_ENABLE
#define TCFG_SD_PORTS                        'A'
#define TCFG_SD_DAT_WIDTH                    1
#define TCFG_SD_DET_MODE                     SD_CMD_DECT
#define TCFG_SD_DET_IO                       IO_PORTA_01
#define TCFG_SD_DET_IO_LEVEL                 0
#define TCFG_SD_CLK                          20000000
#endif

#if TCFG_EQ_ONLINE_ENABLE
#define TCFG_PC_ENABLE                       1
#define USB_DEVICE_CLASS_CONFIG              (CDC_CLASS)
#else
#define TCFG_PC_ENABLE                       0
#define USB_DEVICE_CLASS_CONFIG              (MASSSTORAGE_CLASS)
#endif
#define USB_PC_NO_APP_MODE                   2
#define USB_MALLOC_ENABLE                    1
#define TCFG_UDISK_ENABLE                    0
#define TCFG_HOST_AUDIO_ENABLE               0

#include "usb_std_class_def.h"
#include "usb_common_def.h"

#endif /* D_SDK_PERIPHERAL_CONFIG_H */
