
# 直接执行 make 仍沿用 SDK/根 Makefile 的构建并下载入口。
# 只需编译/链接证据时，必须显式选择 firmware_elf。
.DEFAULT_GOAL := all

# 工具路径设置
ifeq ($(OS), Windows_NT)
TOOL_DIR := C:/JL/pi32/bin
CC    := clang.exe
CXX   := clang.exe
LD    := pi32v2-lto-wrapper.exe
AR    := llvm-ar.exe
MKDIR := mkdir_win -p
RM    := rm -rf

SYS_LIB_DIR := 
SYS_INC_DIR := 
EXT_CFLAGS  :=
export PATH:=$(TOOL_DIR);../../../../tools/utils;$(PATH)

FIXBAT          := ../../../../tools/utils/fixbat.exe
POST_SCRIPT     := ../../../../cpu/wl82/tools/download.bat
RUN_POST_SCRIPT := ..\..\..\..\cpu\wl82\tools\download.bat
else
TOOL_DIR := /opt/jieli/pi32v2/bin
CC    := clang
CXX   := clang
LD    := lto-wrapper
AR    := lto-ar
MKDIR := mkdir -p
RM    := rm -rf
export OBJDUMP := $(TOOL_DIR)/objdump
export OBJCOPY := $(TOOL_DIR)/objcopy
export OBJSIZEDUMP := $(TOOL_DIR)/objsizedump

SYS_LIB_DIR := $(TOOL_DIR)/../lib/r3
SYS_INC_DIR := $(TOOL_DIR)/../include
EXT_CFLAGS  := -D__SHELL__
export PATH:=$(TOOL_DIR):$(PATH)

FIXBAT          := touch
POST_SCRIPT     := ../../../../cpu/wl82/tools/download.sh
RUN_POST_SCRIPT := bash $(POST_SCRIPT)
endif

# 有线下载的 VM 策略；OTA 包独立使用 D_OTA_KEEP_VM，不相互替代。
KEEP_VM ?= 0
ifneq ($(KEEP_VM),0)
ifneq ($(KEEP_VM),1)
$(error KEEP_VM for wired download must be 0 or 1)
endif
endif
MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
APP_CONFIG_FILE := $(MAKEFILE_DIR)../../config/app_config.h
OTA_CONFIG_FILE := $(MAKEFILE_DIR)../../config/ota_config.h
BOARD_CONFIG_FILE := $(MAKEFILE_DIR)../../config/board_config.h
DEPLOYMENT_CONFIG_FILE := $(MAKEFILE_DIR)../../config/deployment_config.h
QWEN_CONFIG_FILE := $(MAKEFILE_DIR)../../config/qwen_config.h
RUNTIME_CONFIG_FILE := $(MAKEFILE_DIR)../../config/runtime_config.h
BOARD_RESOLVE_FILE := $(MAKEFILE_DIR)../../config/internal/d_board_resolve.h
FEATURE_RESOLVE_FILE := $(MAKEFILE_DIR)../../config/internal/d_feature_resolve.h
CLOUD_RESOLVE_FILE := $(MAKEFILE_DIR)../../config/internal/d_cloud_resolve.h
CONFIG_VALIDATE_FILE := $(MAKEFILE_DIR)../../config/internal/d_config_validate.h
SDK_CONFIG_FILES := $(wildcard $(MAKEFILE_DIR)../../config/internal/d_sdk_config.h \
	$(MAKEFILE_DIR)../../config/internal/sdk/*.h)
BUILD_CONFIG_TEMPLATE := $(MAKEFILE_DIR)../../build/d_build_config.in
FIRMWARE_SOURCE_FILE := $(MAKEFILE_DIR)../../build/d_firmware_sources.def

ifeq ($(OS), Windows_NT)
KEEP_VM_PATCH_MAIN := powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-Content '../../../../cpu/wl82/tools/isd_config.ini') -replace 'VM_OPT=0;','VM_OPT=1;' | Set-Content '../../../../cpu/wl82/tools/isd_config.ini' -Encoding ASCII"
KEEP_VM_PATCH_LOADER := powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-Content '../../../../cpu/wl82/tools/isd_config_loader.ini') -replace 'VM_OPT=0;','VM_OPT=1;' | Set-Content '../../../../cpu/wl82/tools/isd_config_loader.ini' -Encoding ASCII"
BOOT_VERSION_PATCH_MAIN = powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-Content '../../../../cpu/wl82/tools/isd_config.ini') -replace '^BR22_TWS_VERSION=[0-9]+;','BR22_TWS_VERSION=$(D_BUILD_OTA_BOOT_VERSION);' | Set-Content '../../../../cpu/wl82/tools/isd_config.ini' -Encoding ASCII"
else
KEEP_VM_PATCH_MAIN := python3 -c "from pathlib import Path; p = Path('../../../../cpu/wl82/tools/isd_config.ini'); p.write_text(p.read_text(encoding='utf-8').replace('VM_OPT=0;', 'VM_OPT=1;'), encoding='utf-8')"
KEEP_VM_PATCH_LOADER := python3 -c "from pathlib import Path; p = Path('../../../../cpu/wl82/tools/isd_config_loader.ini'); p.write_text(p.read_text(encoding='utf-8').replace('VM_OPT=0;', 'VM_OPT=1;'), encoding='utf-8')"
BOOT_VERSION_PATCH_MAIN = python3 -c "from pathlib import Path; import re; p = Path('../../../../cpu/wl82/tools/isd_config.ini'); p.write_text(re.sub(r'(?m)^BR22_TWS_VERSION=[0-9]+;', 'BR22_TWS_VERSION=$(D_BUILD_OTA_BOOT_VERSION);', p.read_text(encoding='utf-8')), encoding='utf-8')"
endif

# D++: GC9307 需要既定 80MHz SPI；只适配生成的应用 ini，不修改原厂时钟规则源码。
ifeq ($(OS), Windows_NT)
GC9307_LSB80_PATCH_MAIN := powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-Content '$(dir $(POST_SCRIPT))isd_config.ini') -replace 'LSB_DIV=2;','LSB_DIV=1;' | Set-Content '$(dir $(POST_SCRIPT))isd_config.ini' -Encoding ASCII"
else
GC9307_LSB80_PATCH_MAIN := python3 -c "from pathlib import Path; p = Path('$(dir $(POST_SCRIPT))isd_config.ini'); p.write_text(p.read_text(encoding='utf-8').replace('LSB_DIV=2;', 'LSB_DIV=1;'), encoding='utf-8')"
endif

CC  := $(TOOL_DIR)/$(CC)
CXX := $(TOOL_DIR)/$(CXX)
LD  := $(TOOL_DIR)/$(LD)
AR  := $(TOOL_DIR)/$(AR)

OUT_ELF   ?= ../../../../cpu/wl82/tools/sdk.elf
OBJ_FILE  ?= $(OUT_ELF).objs.txt
BUILD_DIR ?= ../../build/out/wl82/obj
LEGACY_BUILD_DIRS ?= obj objs
MAP_FILE ?= ../../../../cpu/wl82/tools/sdk.map
USED_SYMBOL_FILE ?= ../../../../cpu/wl82/sdk_used_list.used
MOVABLE_SECTION_FILE ?= ../../../../apps/common/movable/section.txt
LINKER_SCRIPT ?= ../../../../cpu/wl82/sdk.ld
ROOT_PREFIX := ../../../..
# Git 不包含原厂 SDK 载荷；隔离工作区可将此只读根指向完整 SDK，
# 无需复制或修改原厂载荷。
VENDOR_ROOT ?= $(ROOT_PREFIX)

ifeq ($(OS), Windows_NT)
define ENSURE_DIR
$(shell powershell -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(1)' | Out-Null")
endef
else
define ENSURE_DIR
$(shell mkdir -p "$(1)")
endef
endif

# 用户配置只写在 C 宏中；Make 消费 C 预处理结果，
# 不扫描注释或未激活分支里的 #define。
RESOLVED_CONFIG_MK := $(BUILD_DIR)/d_resolved_config.mk
CONFIG_CPP_INCLUDES := -I$(MAKEFILE_DIR)../../config -I$(MAKEFILE_DIR)../../build \
	-I$(MAKEFILE_DIR)../.. -I$(MAKEFILE_DIR)
EFFECTIVE_GOALS := $(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)
ifneq ($(strip $(EFFECTIVE_GOALS)),help)
-include $(RESOLVED_CONFIG_MK)
endif

# C 预处理器从 d_firmware_sources.def 输出带引号的相对路径。
# 此处只去掉对应引号；普通功能配置不应改本 Makefile。
D_CODE_SOURCE_PATHS := $(subst ",,$(D_BUILD_D_CODE_SOURCES))

DOUBLE_BANK_UPDATE_ENABLE := $(D_BUILD_DOUBLE_BANK)
HTTP_OTA_ENABLE := $(D_BUILD_FEATURE_OTA)
SCREEN_PROFILE_GC9307_ENABLE := $(D_BUILD_BOARD_GC9307)
SCREEN_PROFILE_EMI_ILI948X_ENABLE := $(D_BUILD_BOARD_EMI_ILI948X)
LVGL_ENABLE := 1

CFLAGS := \
	-target pi32v2 \
	-integrated-as \
	-mcpu=r3 \
	-mfprev1 \
	-Wuninitialized \
	-Wno-invalid-noreturn \
	-fno-common \
	-integrated-as \
	-Oz \
	-g \
	-flto \
	-fallow-pointer-null \
	-fprefer-gnu-section \
	-femulated-tls \
	-Wno-shift-negative-value \
	-Wframe-larger-than=2560 \
	-mllvm -pi32v2-large-program=true \
	-fms-extensions \
	-w \
	-fno-unwind-tables \
	-ffunction-sections \
	-fdata-sections \
	-fmessage-length=0 \

CXXFLAGS := \
	-std=gnu++14 \
	-fno-exceptions \
	-fno-rtti \

DEFINES := \
	-DSUPPORT_MS_EXTENSIONS \
	-DCONFIG_RELEASE_ENABLE \
	-DCONFIG_CPU_WL82 \
	-DCONFIG_FREE_RTOS_ENABLE \
	-DCONFIG_THREAD_ENABLE \
	-DCONFIG_AUDIO_ONCHIP \
	-DCONFIG_AUDIO_ENABLE \
	-DCONFIG_JLFAT_ENABLE \
	-D__GCC_PI32V2__ \
	-DCONFIG_NEW_ECC_ENABLE \
	-DCONFIG_EQ_SUPPORT_ASYNC \
	-DEQ_CORE_V1 \
	-DCONFIG_SFC_ENABLE \
	-DCONFIG_VIDEO_ENABLE=1 \
	-DCONFIG_IIC_ENABLE=1 \
	-DCONFIG_VIDEO_IMAGE_W=640 \
	-DCONFIG_VIDEO_IMAGE_H=480 \
	-DCONFIG_CAMERA_H_V_EXCHANGE=1 \
	-DEVENT_HANDLER_NUM_CONFIG=2 \
	-DEVENT_TOUCH_ENABLE_CONFIG=0 \
	-DEVENT_POOL_SIZE_CONFIG=256 \
	-DCONFIG_EVENT_KEY_MAP_ENABLE=0 \
	-DTIMER_POOL_NUM_CONFIG=15 \
	-DAPP_ASYNC_POOL_NUM_CONFIG=0 \
	-DVFS_ENABLE=1 \
	-DUSE_SDFILE_NEW=1 \
	-DSDFILE_STORAGE=1 \
	-DVFS_FILE_POOL_NUM_CONFIG=8 \
	-DVM_MAX_PAGE_ALIGN_SIZE_CONFIG=64*1024 \
	-DVM_MAX_SECTOR_ALIGN_SIZE_CONFIG=64*1024 \
	-DVM_ITEM_MAX_NUM=256 \
	-DCONFIG_PLATFORM_AC7911=1 \
	-DCONFIG_UPDATA_ENABLE \
	-DCONFIG_OTA_UPDATA_ENABLE \
	-DCONFIG_ITEM_FORMAT_VM \
	-D_XOPEN_SOURCE=700 \
	-D_GNU_SOURCE \
	-D__ELF__ \
	-DCONFIG_BT_ENABLE=1 \

ifeq ($(D_BUILD_FEATURE_CAMERA),0)
# 相机/视频编译宏跟随显式功能开关。
DEFINES := $(filter-out \
	-DCONFIG_VIDEO_ENABLE=1 \
	-DCONFIG_VIDEO_IMAGE_W=640 \
	-DCONFIG_VIDEO_IMAGE_H=480 \
	-DCONFIG_CAMERA_H_V_EXCHANGE=1, \
	$(DEFINES))
endif

ifeq ($(D_BUILD_AUDIO),0)
# 音频编译宏跟随已经解析的功能依赖。
DEFINES := $(filter-out \
	-DCONFIG_AUDIO_ONCHIP \
	-DCONFIG_AUDIO_ENABLE \
	-DCONFIG_EQ_SUPPORT_ASYNC \
	-DEQ_CORE_V1, \
	$(DEFINES))
endif

ifeq ($(D_BUILD_FEATURE_BLE_PROVISION),0)
DEFINES := $(filter-out -DCONFIG_BT_ENABLE=1,$(DEFINES))
endif

DEFINES += $(EXT_CFLAGS)

# SDK/common 源码按文件名包含 app_config.h，故 D_Code 应用配置
# 必须位于所有示例包含根之前。
INCLUDES := \
	-I../../../../include_lib/c++ \
	-I../../../../include_lib/c++/include \
	-I../../../../include_lib/newlib/include \
	-I../../../../include_lib/c++/simple_pthread \
	-I../../../../include_lib \
	-I../../../../include_lib/driver \
	-I../../../../include_lib/driver/device \
	-I../../../../include_lib/driver/cpu/wl82 \
	-I../../../../include_lib/system \
	-I../../../../include_lib/system/generic \
	-I../../../../include_lib/btctrler \
	-I../../../../include_lib/btctrler/port/wl82 \
	-I../../../../include_lib/update \
	-I../../../../include_lib/btstack/third_party/common \
	-I../../../../include_lib/btstack/third_party/rcsp \
	-I../../../../include_lib/utils \
	-I../../../../include_lib/utils/syscfg \
	-I../../../../include_lib/utils/event \
	-I../../../../include_lib/media \
	-I../../../../include_lib/media/cpu/wl82 \
	-I../../../../include_lib/net \
	-I../../../../include_lib/net/mbedtls_3_4_0 \
	-I../../../../include_lib/net/lwip_2_2_0/lwip/src/include \
	-I../../../../include_lib/net/lwip_2_2_0/lwip/src/include/lwip \
	-I../../../../include_lib/net/lwip_2_2_0/lwip/port \
	-I../../../../include_lib/net/lwip_2_2_0/lwip/app \
	-I../../../../apps \
	-I../../../../apps/D_Code/config \
	-I../../../../apps/D_Code \
	-I../../../../apps/D_Code/modules/integrations/xiaole_wakeup/sdk \
	-I../../../../apps/D_Code/modules/integrations/qwen/sdk/include \
	-I../../../../apps/D_Code/modules/integrations/qwen/sdk/include/c_utils \
	-I../../../../apps/D_Code/modules/integrations/qwen/sdk/include/c_mmi_cmd \
	-I../../../../apps/D_Code/modules/integrations/qwen/sdk/third_party/cJSON \
	-I../../../../apps/D_Code/modules/integrations/qwen/sdk/third_party/tinycrypt/include \
	-I../../../../apps/D_Code/modules/integrations/baidu_rtc/sdk/include \
	-I../../../../apps/D_Code/modules/integrations/baidu_rtc/sdk/platform \
	-I../../../../include_lib/net/cJSON_common \
	-I../../../../apps/common/LLM/audio \
	-I../../../../apps/demo/demo_audio/include \
	-I../../../../apps/common \
	-I../../../../apps/common/include \
	-I../../../../apps/common/config/include \
	-I../../../../apps/common/key \
	-I../../../../apps/common/jl_math \
	-I../../../../apps/common/usb \
	-I../../../../apps/common/usb/device \
	-I../../../../apps/common/usb/host \
	-I../../../../apps/common/usb/include \
	-I../../../../apps/common/usb/include/host \
	-I../../../../include_lib/driver/device/usb \
	-I../../../../include_lib/driver/device/usb/device \
	-I../../../../include_lib/driver/device/usb/host \
	-I../../../../include_lib/driver/device/video \
	-I../../../../include_lib/system/os \
	-I../../../../apps/common/example/third_party \
	-I../../../../apps/common/example/third_party/lvgl_v9 \
	-I../../../../apps/common/example/third_party/lvgl_v9/src \
	-I../../../../apps/common/example/third_party/lvgl_v9/examples/porting \
	-I../../../../apps/common/ui/include \
	-I../../../../apps/common/ble/include \
	-I../../../../apps/demo/demo_DevKitBoard/include \
	-I../../../../include_lib/btstack

c_SRC_FILES := \
	../../../../apps/common/config/log_config/lib_driver_config.c \
	../../../../apps/common/config/log_config/lib_system_config.c \
	../../../../apps/common/config/log_config/lib_update_config.c \
	../../../../apps/common/config/log_config/app_config.c \
	../../../../apps/common/config/new_cfg_tool.c \
	../../../../apps/common/config/user_cfg.c \
	../../../../apps/common/debug/debug.c \
	../../../../apps/common/debug/debug_user.c \
	../../../../apps/common/system/init.c \
	../../../../apps/common/system/system_reset_reason.c \
	../../../../apps/common/system/version.c \
	../../../../apps/common/update/update.c \
	../../../../apps/common/jl_math/flfft_core_pi32v2.c \
	../../../../apps/common/jl_math/jl_fft.c \
	../../../../apps/common/jl_math/jl_math.c \
	../../../../apps/common/iic/iic.c \
	../../../../apps/common/iic/software_iic.c \
	../../../../apps/common/system/device_mount.c \
	../../../../cpu/wl82/debug.c \
	../../../../cpu/wl82/iic.c \
	../../../../cpu/wl82/key/adc_api.c \
	../../../../cpu/wl82/key/ctmu.c \
	../../../../cpu/wl82/key/irflt.c \
	../../../../cpu/wl82/key/plcnt.c \
	../../../../cpu/wl82/key/rdec.c \
	../../../../cpu/wl82/port_waked_up.c \
	../../../../cpu/wl82/setup.c \

# D_Code 源成员由 C 风格清单解析，不在此重复维护。
c_SRC_FILES += $(addprefix ../../../../apps/D_Code/,$(D_CODE_SOURCE_PATHS))

# 专用写码镜像使用原厂 JL CDC 协议，VM 业务仍归 D_Code 所有者。
# 不链接仅打印选项的示例 product_port.c。
ifeq ($(D_BUILD_QWEN_FACTORY),1)
INCLUDES += -I../../../../apps/common/product_test_tool/includes
c_SRC_FILES += \
	../../../../apps/common/product_test_tool/product_main.c \
	../../../../apps/common/product_test_tool/communication.c \
	../../../../apps/common/product_test_tool/devices_control.c
endif

# LCD-data 仅属于 EMI 配置；GC 工厂配置只附加原有 USB CDC。
ifeq ($(D_BUILD_BOARD_EMI_ILI948X),1)
c_SRC_FILES += ../../../../apps/common/ui/lcd_driver/lcd_data_driver.c
endif
ifneq ($(filter 1,$(D_BUILD_BOARD_EMI_ILI948X) $(D_BUILD_QWEN_FACTORY)),)
c_SRC_FILES += \
	../../../../apps/common/usb/device/cdc.c \
	../../../../apps/common/usb/device/descriptor.c \
	../../../../apps/common/usb/device/hid.c \
	../../../../apps/common/usb/device/msd.c \
	../../../../apps/common/usb/device/msd_upgrade.c \
	../../../../apps/common/usb/device/printer.c \
	../../../../apps/common/usb/device/ram_disk_circle.c \
	../../../../apps/common/usb/device/ram_disk_update.c \
	../../../../apps/common/usb/device/task_pc.c \
	../../../../apps/common/usb/device/uac.c \
	../../../../apps/common/usb/device/uac2.c \
	../../../../apps/common/usb/device/uac_stream.c \
	../../../../apps/common/usb/device/usb_device.c \
	../../../../apps/common/usb/device/user_setup.c \
	../../../../apps/common/usb/device/uvc.c \
	../../../../apps/common/usb/host/adb.c \
	../../../../apps/common/usb/host/aoa.c \
	../../../../apps/common/usb/host/audio.c \
	../../../../apps/common/usb/host/hid.c \
	../../../../apps/common/usb/host/usb_bulk_transfer.c \
	../../../../apps/common/usb/host/usb_ctrl_transfer.c \
	../../../../apps/common/usb/host/usb_host.c \
	../../../../apps/common/usb/host/usb_host_cdc.c \
	../../../../apps/common/usb/host/usb_storage.c \
	../../../../apps/common/usb/host/usb_video.c \
	../../../../apps/common/usb/host/usbnet.c \
	../../../../apps/common/usb/host/uvc_host.c \
	../../../../apps/common/usb/usb_config.c \
	../../../../apps/common/usb/usb_epbuf_manager.c \
	../../../../apps/common/usb/usb_host_config.c
endif


# 按 app_config.h 正向选择 D_Code 功能源码。
ifeq ($(D_BUILD_AUDIO),1)
c_SRC_FILES += \
	../../../../apps/common/LLM/audio/audio_input.c \
	../../../../apps/common/audio_music/audio_config.c \
	../../../../apps/common/audio_music/audio_eff_default_parm.c \
	../../../../apps/common/audio_music/effects_adj.c \
	../../../../apps/common/audio_music/eq_config_new.c \
	../../../../apps/common/audio_music/midi/audio_dec_midi_ctrl.c \
	../../../../apps/common/audio_music/midi/midi_dec.c \
	../../../../apps/common/audio_music/midi/midi_event.c \
	../../../../apps/common/audio_music/midi/midi_play.c \
	../../../../apps/common/audio_music/music_decrypt.c \
	../../../../apps/common/audio_music/usb_audio_api.c
endif

ifeq ($(D_BUILD_FEATURE_KEY_SERVICE),1)
c_SRC_FILES += \
	../../../../apps/common/key/adkey.c \
	../../../../apps/common/key/irkey.c \
	../../../../apps/common/key/key_driver.c
endif

ifeq ($(D_BUILD_FEATURE_WIFI),1)
c_SRC_FILES += \
	../../../../apps/common/net/assign_macaddr.c \
	../../../../apps/common/net/wifi_conf.c \
	../../../../apps/common/net/config_network.c
endif

ifeq ($(D_BUILD_FEATURE_BLE_PROVISION),1)
c_SRC_FILES += \
	../../../../apps/common/config/bt_profile_config.c \
	../../../../apps/common/config/log_config/lib_btctrler_config.c \
	../../../../apps/common/config/log_config/lib_btstack_config.c
endif

ifeq ($(D_BUILD_FEATURE_CAMERA),1)
ifneq ($(D_BUILD_CAMERA_PREVIEW),1)
c_SRC_FILES += ../../../../apps/demo/demo_DevKitBoard/get_yuv_data.c
endif
c_SRC_FILES += \
	../../../../apps/common/camera/gc0307_dvp/gc0307.c \
	../../../../apps/common/camera/gc0308_dvp/gc0308.c \
	../../../../apps/common/camera/gc0309_dvp/gc0309.c \
	../../../../apps/common/camera/gc0312_dvp/gc0312.c \
	../../../../apps/common/camera/gc0328_dvp/gc0328.c \
	../../../../apps/common/camera/gc0329_dvp/gc0329.c \
	../../../../apps/common/camera/gc032a_dvp/gc032a.c \
	../../../../apps/common/camera/byd20a6/byd20a6.c \
	../../../../apps/common/camera/byd30a2/byd30a2.c \
	../../../../apps/common/camera/byd3720/byd3720.c \
	../../../../apps/common/camera/byd3901/byd3901.c \
	../../../../apps/common/camera/byd3a03/byd3a03.c \
	../../../../apps/common/camera/bydBF2013_dvp/bf2013.c \
	../../../../apps/common/camera/gc2035_dvp/gc2035.c \
	../../../../apps/common/camera/gc2145_dvp/gc2145.c \
	../../../../apps/common/camera/ov7670_dvp/ov7670.c \
	../../../../apps/common/camera/xc7011_dvp/xc7011.c \
	../../../../apps/common/camera/h7375s/h7375s.c \
	../../../../apps/common/camera/pr2000k/pr2000k.c \
	../../../../apps/common/camera/pv4109/pv4109.c
endif

S_SRC_FILES := \
	../../../../include_lib/btctrler/version.z.S \
	../../../../include_lib/btstack/version.z.S \
	../../../../include_lib/driver/version.z.S \
	../../../../include_lib/media/version.z.S \
	../../../../include_lib/net/version.z.S \
	../../../../include_lib/server/version.z.S \
	../../../../include_lib/system/version.z.S \
	../../../../include_lib/update/version.z.S \
	../../../../include_lib/utils/version.z.S \
	../../../../lib/net/version.z.S \
	../../../../lib/server/version.z.S \
	../../../../lib/utils/version.z.S \

ifeq ($(SCREEN_PROFILE_GC9307_ENABLE),1)
S_SRC_FILES :=
endif

s_SRC_FILES :=

# LVGL 源文件（跟随 config/internal/d_board_resolve.h 的 D_LVGL_ENABLE）
ifeq ($(VENDOR_ROOT),$(ROOT_PREFIX))
LVGL_ROOT := $(ROOT_PREFIX)/apps/common/example/third_party/lvgl_v9
else
LVGL_ROOT := $(VENDOR_ROOT)/apps/common/example/third_party/lvgl_v9
endif
ifeq ($(LVGL_ENABLE),1)
LVGL_SRC_FILES := $(wildcard $(LVGL_ROOT)/src/*.c) \
	$(wildcard $(LVGL_ROOT)/src/core/*.c) \
	$(wildcard $(LVGL_ROOT)/src/display/*.c) \
	$(wildcard $(LVGL_ROOT)/src/draw/*.c) \
	$(wildcard $(LVGL_ROOT)/src/draw/sw/*.c) \
	$(wildcard $(LVGL_ROOT)/src/draw/sw/blend/*.c) \
	$(wildcard $(LVGL_ROOT)/src/font/*.c) \
	$(wildcard $(LVGL_ROOT)/src/indev/*.c) \
	$(wildcard $(LVGL_ROOT)/src/layouts/*.c) \
	$(wildcard $(LVGL_ROOT)/src/layouts/flex/*.c) \
	$(wildcard $(LVGL_ROOT)/src/layouts/grid/*.c) \
	$(wildcard $(LVGL_ROOT)/src/libs/bin_decoder/*.c) \
	$(wildcard $(LVGL_ROOT)/src/misc/*.c) \
	$(wildcard $(LVGL_ROOT)/src/misc/cache/*.c) \
	$(wildcard $(LVGL_ROOT)/src/osal/*.c) \
	$(wildcard $(LVGL_ROOT)/src/others/fragment/*.c) \
	$(wildcard $(LVGL_ROOT)/src/others/gridnav/*.c) \
	$(wildcard $(LVGL_ROOT)/src/others/ime/*.c) \
	$(wildcard $(LVGL_ROOT)/src/others/observer/*.c) \
	$(wildcard $(LVGL_ROOT)/src/others/snapshot/*.c) \
	$(wildcard $(LVGL_ROOT)/src/others/sysmon/*.c) \
	$(wildcard $(LVGL_ROOT)/src/stdlib/*.c) \
	$(wildcard $(LVGL_ROOT)/src/stdlib/builtin/*.c) \
	$(wildcard $(LVGL_ROOT)/src/stdlib/clib/*.c) \
	$(wildcard $(LVGL_ROOT)/src/themes/*.c) \
	$(wildcard $(LVGL_ROOT)/src/themes/default/*.c) \
	$(wildcard $(LVGL_ROOT)/src/themes/mono/*.c) \
	$(wildcard $(LVGL_ROOT)/src/themes/simple/*.c) \
	$(wildcard $(LVGL_ROOT)/src/tick/*.c) \
	$(wildcard $(LVGL_ROOT)/src/widgets/*/*.c)
c_SRC_FILES += $(LVGL_SRC_FILES)
endif

# D_Code 持有已核实的 HTTPS 链路；仅启用双区布局不得
# 额外引入 SDK 自动 TF 扫描或旧 net_fclose 重启行为。

cpp_SRC_FILES :=

cc_SRC_FILES :=

cxx_SRC_FILES :=

D_NETWORK_LIBS :=
ifeq ($(D_BUILD_FEATURE_WIFI),1)
D_NETWORK_LIBS += \
	../../../../cpu/wl82/liba/wl_rf_common.a \
	../../../../cpu/wl82/liba/libdns.a \
	../../../../cpu/wl82/liba/lwip_2_2_0_sfc.a \
	../../../../cpu/wl82/liba/wl_wifi_sfc.a \
	../../../../cpu/wl82/liba/wpasupplicant.a \
	../../../../cpu/wl82/liba/libmbedtls_3_4_0.a \
	../../../../cpu/wl82/liba/hsm.a \
	../../../../cpu/wl82/liba/network_download.a \
	../../../../cpu/wl82/liba/http_cli.a \
	../../../../cpu/wl82/liba/https_cli.a \
	../../../../cpu/wl82/liba/json.a \
	../../../../cpu/wl82/liba/websocket.a
endif

D_BLE_LIBS :=
ifeq ($(D_BUILD_FEATURE_BLE_PROVISION),1)
D_BLE_LIBS += \
	../../../../cpu/wl82/liba/btctrler.a \
	../../../../cpu/wl82/liba/btstack.a
endif

D_AGENT_LIBS :=
ifeq ($(D_BUILD_FEATURE_BRTC),1)
D_AGENT_LIBS += ../../../../apps/D_Code/modules/integrations/baidu_rtc/sdk/lib/libbrtc.a
endif
ifeq ($(D_BUILD_FEATURE_QWEN),1)
D_QWEN_ACCESS_LIB := ../../modules/integrations/qwen/lib/libqwen_access.a
D_AGENT_LIBS += $(D_QWEN_ACCESS_LIB)
D_AGENT_LIBS += \
	--whole-archive \
	../../../../apps/D_Code/modules/integrations/qwen/sdk/libs/libc_license.a \
	--no-whole-archive \
	../../../../apps/D_Code/modules/integrations/qwen/sdk/libs/libqwen_sdk.a \
	../../../../apps/D_Code/modules/integrations/qwen/sdk/libs/libqwen_test.a \
	../../../../apps/D_Code/modules/integrations/qwen/sdk/libs/libc_mmi_cmd.a \
	../../../../apps/D_Code/modules/integrations/qwen/sdk/libs/libc_mmi_cmd_application.a \
	../../../../apps/D_Code/modules/integrations/qwen/sdk/libs/libc_mmi_cmd_voice_translate.a \
	../../../../apps/D_Code/modules/integrations/qwen/sdk/libs/libc_mmi_cmd_volume.a \
	../../../../apps/D_Code/modules/integrations/qwen/sdk/third_party/tinycrypt/libtinycrypt.a
endif
ifeq ($(D_BUILD_FEATURE_XIAOLE_WAKEUP),1)
D_XIAOLE_LIB := ../../../../apps/D_Code/modules/integrations/xiaole_wakeup/sdk/liblx_aec.a
D_AGENT_LIBS += $(D_XIAOLE_LIB)
D_LINK_ADAPTER_INPUTS := $(D_XIAOLE_LIB)
endif

D_AUDIO_LIBS :=
ifeq ($(D_BUILD_AUDIO),1)
D_AUDIO_LIBS += \
	../../../../cpu/wl82/liba/audio_server.a \
	../../../../cpu/wl82/liba/echo_server.a \
	../../../../cpu/wl82/liba/libspeex.a \
	../../../../cpu/wl82/liba/libvad.a \
	../../../../cpu/wl82/liba/lib_aac_enc.a \
	../../../../cpu/wl82/liba/lib_m4a_dec.a \
	../../../../cpu/wl82/liba/lib_mp3_dec.a \
	../../../../cpu/wl82/liba/lib_mp3_enc.a \
	../../../../cpu/wl82/liba/lib_opus_enc.a \
	../../../../cpu/wl82/liba/lib_opus_stenc.a \
	../../../../cpu/wl82/liba/lib_opus_dec.a \
	../../../../cpu/wl82/liba/lib_wma_dec.a \
	../../../../cpu/wl82/liba/lib_ape_dec.a \
	../../../../cpu/wl82/liba/lib_flac_dec.a \
	../../../../cpu/wl82/liba/lib_wav_dec.a \
	../../../../cpu/wl82/liba/lib_dts_dec.a \
	../../../../cpu/wl82/liba/lib_amr_dec.a \
	../../../../cpu/wl82/liba/lib_amr_enc.a \
	../../../../cpu/wl82/liba/lib_amrwb_enc.a \
	../../../../cpu/wl82/liba/lib_adpcm_enc.a \
	../../../../cpu/wl82/liba/lib_pitchshifter.a \
	../../../../cpu/wl82/liba/lib_rmono2stereo.a \
	../../../../cpu/wl82/liba/lib_reverb_cal.a \
	../../../../cpu/wl82/liba/lib_howling.a \
	../../../../cpu/wl82/liba/lib_pitch_speed.a \
	../../../../cpu/wl82/liba/libaec.a \
	../../../../cpu/wl82/liba/lib_dvad.a \
	../../../../cpu/wl82/liba/libjlsp.a \
	../../../../cpu/wl82/liba/libjlsp_kws_far_keyword.a \
	../../../../cpu/wl82/liba/libkwscommon.a \
	../../../../cpu/wl82/liba/lib_usb_syn.a \
	../../../../cpu/wl82/liba/libaisp_single.a \
	../../../../cpu/wl82/liba/libauth_aisp.a \
	../../../../cpu/wl82/liba/libauth.a \
	../../../../cpu/wl82/liba/aisIntlib_single_xiaoai.a \
	../../../../cpu/wl82/liba/jla_codec_lib.a \
	../../../../cpu/wl82/liba/lib_spectrum_show.a \
	../../../../cpu/wl82/liba/lib_ogg_vorbis_dec.a \
	../../../../cpu/wl82/liba/lib_midi_dec.a \
	../../../../cpu/wl82/liba/libcompressor.a \
	../../../../cpu/wl82/liba/lib_crossover_coff.a \
	../../../../cpu/wl82/liba/libdrc.a \
	../../../../cpu/wl82/liba/media_app.a \
	../../../../cpu/wl82/liba/ai_server.a
endif

LFLAGS := \
	--plugin-opt=-pi32v2-always-use-itblock=false \
	--plugin-opt=-enable-ipra=true \
	--plugin-opt=-pi32v2-merge-max-offset=4096 \
	--plugin-opt=-pi32v2-enable-simd=true \
	--plugin-opt=mcpu=r3 \
	--plugin-opt=-global-merge-on-const \
	--plugin-opt=-inline-threshold=5 \
	--plugin-opt=-inline-max-allocated-size=32 \
	--plugin-opt=-inline-normal-into-special-section=true \
	--plugin-opt=-dont-used-symbol-list=malloc,free,sprintf,printf,puts,putchar \
	--plugin-opt=save-temps \
	--plugin-opt=-pi32v2-enable-rep-memop \
	--plugin-opt=-emulated-tls \
	--sort-common \
	--plugin-opt=-used-symbol-file=$(USED_SYMBOL_FILE) \
	--plugin-opt=-enable-movable-region=true \
	--plugin-opt=-movable-region-section-prefix=.movable.slot. \
	--plugin-opt=-movable-region-stub-section-prefix=.movable.stub. \
	--plugin-opt=-movable-region-prefix=.movable.region. \
	--plugin-opt=-movable-region-stub-prefix=__movable_stub_ \
	--plugin-opt=-movable-region-stub-swi-number=-2 \
	--plugin-opt=-movable-region-map-file-list=../../../../apps/common/movable/funcname.txt \
	--plugin-opt=-movable-region-section-map-file-list=$(MOVABLE_SECTION_FILE) \
	--plugin-opt=-movable-region-exclude-func-file-list=../../../../apps/common/movable/exclude.txt \
	--plugin-opt=-pi32v2-large-program=true \
	--start-group \
	../../../../include_lib/newlib/pi32v2-lib/libm.a \
	../../../../include_lib/newlib/pi32v2-lib/libc.a \
	../../../../include_lib/newlib/pi32v2-lib/libcompiler_rt.a \
	--start-group  \
	../../../../cpu/wl82/liba/cpu.a \
	../../../../cpu/wl82/liba/event.a \
	../../../../cpu/wl82/liba/system.a \
	../../../../cpu/wl82/liba/cfg_tool.a \
	../../../../cpu/wl82/liba/fs.a \
	../../../../cpu/wl82/liba/common_lib.a \
	../../../../cpu/wl82/liba/update.a \
	$(D_NETWORK_LIBS) \
	$(D_AUDIO_LIBS) \
	$(D_AGENT_LIBS) \
	../../../../cpu/wl82/liba/cJSON.a \
	$(D_BLE_LIBS) \
	../../../../cpu/wl82/liba/crypto_toolbox_Osize.a \
	../../../../cpu/wl82/liba/lib_ccm_aes.a \
	--end-group \
	-T$(LINKER_SCRIPT) \
	-M=$(MAP_FILE) \
	--end-group  \
	--plugin-opt=mcpu=r3 \
	--plugin-opt=-mattr=+fprev1

# 使用 SDK 公开探针扩展，保留原基础配置。
ifeq ($(D_BUILD_DEBUG_UI_PERFORMANCE),1)
DEFINES += -DLV_CONF_PATH=config/d_lvgl_profile_config.h
endif

# 显式开启的共用服务 API 即使没有自动启动消费者也保留可用。
ifeq ($(D_BUILD_FEATURE_WEBSOCKET),1)
LFLAGS += $(addprefix --undefined=,ws_service_defaults ws_service_capabilities ws_service_init \
    ws_service_configure ws_service_start ws_service_stop ws_service_deinit ws_service_send \
    ws_service_ping ws_service_close ws_service_get_snapshot ws_service_maintenance_begin \
    ws_service_maintenance_poll ws_service_maintenance_release ws_service_maintenance_cancel)
endif

# 普通构建将 VENDOR_ROOT 保持为 ROOT_PREFIX；二级隔离保留本地 D_Code，
# 并只读映射被忽略的 SDK 载荷（含 apps/common）。
ifneq ($(VENDOR_ROOT),$(ROOT_PREFIX))
INCLUDES := $(subst -I$(ROOT_PREFIX)/include_lib,-I$(VENDOR_ROOT)/include_lib,$(INCLUDES))
# apps/common 包含未跟踪的 SDK 载荷，例如 LVGL 源码及链接片段。
INCLUDES := $(subst -I$(ROOT_PREFIX)/apps/common,-I$(VENDOR_ROOT)/apps/common,$(INCLUDES))
INCLUDES += -I$(VENDOR_ROOT)/apps
c_SRC_FILES := $(patsubst $(ROOT_PREFIX)/apps/common/%,$(VENDOR_ROOT)/apps/common/%,$(c_SRC_FILES))
c_SRC_FILES := $(patsubst $(ROOT_PREFIX)/cpu/%,$(VENDOR_ROOT)/cpu/%,$(c_SRC_FILES))
S_SRC_FILES := $(patsubst $(ROOT_PREFIX)/include_lib/%,$(VENDOR_ROOT)/include_lib/%,$(S_SRC_FILES))
S_SRC_FILES := $(patsubst $(ROOT_PREFIX)/lib/%,$(VENDOR_ROOT)/lib/%,$(S_SRC_FILES))
LFLAGS := $(subst $(ROOT_PREFIX)/include_lib/,$(VENDOR_ROOT)/include_lib/,$(LFLAGS))
LFLAGS := $(subst $(ROOT_PREFIX)/cpu/,$(VENDOR_ROOT)/cpu/,$(LFLAGS))
LFLAGS := $(subst $(ROOT_PREFIX)/lib/,$(VENDOR_ROOT)/lib/,$(LFLAGS))
LFLAGS := $(subst $(ROOT_PREFIX)/apps/common/,$(VENDOR_ROOT)/apps/common/,$(LFLAGS))
endif

c_OBJS    := $(c_SRC_FILES:%.c=%.c.o)
S_OBJS    := $(S_SRC_FILES:%.S=%.S.o)
s_OBJS    := $(s_SRC_FILES:%.s=%.s.o)
cpp_OBJS  := $(cpp_SRC_FILES:%.cpp=%.cpp.o)
cxx_OBJS  := $(cxx_SRC_FILES:%.cxx=%.cxx.o)
cc_OBJS   := $(cc_SRC_FILES:%.cc=%.cc.o)

OBJS      := $(c_OBJS) $(S_OBJS) $(s_OBJS) $(cpp_OBJS) $(cxx_OBJS) $(cc_OBJS)
ifneq ($(VENDOR_ROOT),$(ROOT_PREFIX))
OBJS      := $(patsubst $(VENDOR_ROOT)/%,vendor/%,$(OBJS))
endif
DEP_FILES := $(OBJS:%.o=%.d.v2)

OBJS      := $(addprefix $(BUILD_DIR)/, $(OBJS:$(ROOT_PREFIX)/%=%))
DEP_FILES := $(addprefix $(BUILD_DIR)/, $(DEP_FILES:$(ROOT_PREFIX)/%=%))

# 配置变化会影响整个固件的编译宏。
# 普通前置依赖使菜单 A→B→A 切换时重编译所有对象，
# 避免复用上一套配置编译的对象。
$(OBJS): $(RESOLVED_CONFIG_MK)

VERBOSE ?= 0
ifeq ($(VERBOSE), 1)
QUITE :=
else
QUITE := @
endif

ifeq ($(OS),Windows_NT)
# 随源文件增多，原生 D_Code 也可能超出 CreateProcess 命令行长度。
# 原生与隔离构建均通过既有对象列表响应文件调用链接器。
LINK_AT ?= 1
else ifeq ($(VENDOR_ROOT),$(ROOT_PREFIX))
LINK_AT ?= 0
else
# 其他宿主的隔离构建也保留响应文件链接。
LINK_AT ?= 1
endif

D_PYTHON ?= python
ifeq ($(OS),Windows_NT)
D_LINKER_PREPARE = powershell -NoProfile -ExecutionPolicy Bypass -File "$(VENDOR_ROOT)/apps/D_Code/build/prepare_d_code_linker.ps1" -InputPath $(LINKER_SCRIPT).vendor -OutputPath $(LINKER_SCRIPT)
D_LINKER_CHECK = powershell -NoProfile -ExecutionPolicy Bypass -File "$(VENDOR_ROOT)/apps/D_Code/build/prepare_d_code_linker.ps1" -ElfPath $(OUT_ELF)
else
D_LINKER_PREPARE = $(D_PYTHON) $(VENDOR_ROOT)/apps/D_Code/build/prepare_d_code_linker.py --input $(LINKER_SCRIPT).vendor --output $(LINKER_SCRIPT)
D_LINKER_CHECK = $(D_PYTHON) $(VENDOR_ROOT)/apps/D_Code/build/prepare_d_code_linker.py --elf $(OUT_ELF)
endif
OTA_CONFIG_OUTPUT ?= $(ROOT_PREFIX)/apps/D_Code/build/out/product_ui_ota/release/isd_config.ini
OTA_DOWNLOAD_OUTPUT ?= $(dir $(OTA_CONFIG_OUTPUT))download.bat

.PHONY: help firmware_elf ota_config all clean link_prepare pre_build FORCE_APP_BUILD_STAMP

.SUFFIXES:

$(RESOLVED_CONFIG_MK): $(BUILD_CONFIG_TEMPLATE) $(APP_CONFIG_FILE) $(OTA_CONFIG_FILE) \
		$(BOARD_CONFIG_FILE) $(DEPLOYMENT_CONFIG_FILE) \
		$(QWEN_CONFIG_FILE) $(RUNTIME_CONFIG_FILE) $(CONFIG_VALIDATE_FILE) \
		$(BOARD_RESOLVE_FILE) $(FEATURE_RESOLVE_FILE) $(CLOUD_RESOLVE_FILE) $(SDK_CONFIG_FILES) \
		$(FIRMWARE_SOURCE_FILE) \
		$(MAKEFILE_DIR)d_board_memory_config.h
	$(info +RESOLVE-CONFIG)
	$(call ENSURE_DIR,$(BUILD_DIR))
	$(QUITE) $(CC) -E -P -x c $(CONFIG_CPP_INCLUDES) $(BUILD_CONFIG_TEMPLATE) -o $@

help:
	@echo D_Code build targets:
	@echo   firmware_elf  Build and link the firmware ELF only
	@echo   all           Build, link, package, and start the SDK download flow
	@echo                 Wired download erases VM by default. Use KEEP_VM=1 to preserve VM
	@echo   ota_config    Generate OTA config using D_OTA_KEEP_VM, independently of wired download
	@echo   clean         Remove generated ELF and object files

# 仅供私有开发显式调用；公开固件始终直链提供的归档。
# all/firmware_elf 不依赖此目标，本目标也不调用下载器。
QWEN_ACCESS_OUT ?= ../../modules/integrations/qwen/lib/libqwen_access.a
QWEN_ACCESS_BUILD_DIR ?= ../../build/out/qwen_access_library/objects
QWEN_ACCESS_UNITS := qwen_access qwen_factory_record qwen_auth_store qwen_auth_port qwen_activation_protocol qwen_activation qwen_net qwen_wss qwen_sdk_logging
QWEN_ACCESS_OBJECTS := $(addprefix $(QWEN_ACCESS_BUILD_DIR)/,$(addsuffix .o,$(QWEN_ACCESS_UNITS)))
QWEN_ACCESS_DEPS := $(QWEN_ACCESS_OBJECTS:.o=.d)
QWEN_ACCESS_HEADERS := $(wildcard ../../modules/integrations/qwen/*.h ../../modules/integrations/qwen/private/*.h)
$(OUT_ELF): $(D_QWEN_ACCESS_LIB)
.PHONY: qwen_access_library
qwen_access_library: $(QWEN_ACCESS_OBJECTS)
	$(if $(filter 1,$(D_BUILD_FEATURE_QWEN)),,$(error Private library build requires Qwen enabled))
	$(call ENSURE_DIR,$(dir $(QWEN_ACCESS_OUT)))
	$(AR) rcs $(QWEN_ACCESS_OUT) $(QWEN_ACCESS_OBJECTS)

$(QWEN_ACCESS_BUILD_DIR)/%.o: ../../modules/integrations/qwen/%.c $(QWEN_ACCESS_HEADERS) $(QWEN_CONFIG_FILE) $(APP_CONFIG_FILE)
	$(call ENSURE_DIR,$(QWEN_ACCESS_BUILD_DIR))
	$(CC) $(CFLAGS) -g0 $(DEFINES) $(INCLUDES) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

# 私有编库缓存依赖不得进入普通固件或导出工程构建。
ifneq ($(filter qwen_access_library,$(MAKECMDGOALS)),)
-include $(QWEN_ACCESS_DEPS)
# 为尚未生成依赖或遗失 .d 的缓存补齐构建依赖。
QWEN_ACCESS_MISSING_DEPS := $(foreach obj,$(QWEN_ACCESS_OBJECTS),$(if $(wildcard $(obj:.o=.d)),,$(obj)))
.PHONY: qwen_access_missing_dependency
ifneq ($(strip $(QWEN_ACCESS_MISSING_DEPS)),)
$(QWEN_ACCESS_MISSING_DEPS): qwen_access_missing_dependency
endif
endif

firmware_elf: pre_build $(OUT_ELF)
ifeq ($(OS), Windows_NT)
	$(call ENSURE_DIR,../../build/out/wl82/ota)
	$(QUITE) $(RM) ../../build/out/wl82/ota/d_code_config.mk
	$(QUITE) powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OUT_ELF)' -Destination '../../build/out/wl82/ota/sdk.elf' -ErrorAction Stop"
	$(QUITE) powershell -NoProfile -Command "Copy-Item -LiteralPath '../../../../cpu/wl82/tools/isd_config.ini' -Destination '../../build/out/wl82/ota/isd_config.ini' -ErrorAction Stop"
	$(QUITE) powershell -NoProfile -Command "Copy-Item -LiteralPath '$(POST_SCRIPT)' -Destination '../../build/out/wl82/ota/download.bat' -ErrorAction Stop"
	$(QUITE) powershell -NoProfile -Command "Copy-Item -LiteralPath '$(RESOLVED_CONFIG_MK)' -Destination '../../build/out/wl82/ota/d_code_config.mk' -ErrorAction Stop"
endif

# SDK 模板只读，配置输出在工程内；不触发构建后下载器。
ota_config:
	$(call ENSURE_DIR,$(dir $(OTA_CONFIG_OUTPUT)))
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P $(VENDOR_ROOT)/cpu/wl82/tools/isd_config_rule.c -o $(OTA_CONFIG_OUTPUT)
	$(QUITE) $(D_PYTHON) $(VENDOR_ROOT)/apps/D_Code/build/prepare_ota_release.py --normalize-config $(OTA_CONFIG_OUTPUT) --board $(D_BUILD_BOARD_TARGET) --bank $(D_BUILD_DOUBLE_BANK) --keep-vm $(D_BUILD_OTA_KEEP_VM) --boot-version $(D_BUILD_OTA_BOOT_VERSION)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P $(VENDOR_ROOT)/cpu/wl82/tools/download.c -o $(OTA_DOWNLOAD_OUTPUT)
ifeq ($(OS), Windows_NT)
	$(QUITE) $(VENDOR_ROOT)/tools/utils/fixbat.exe $(OTA_DOWNLOAD_OUTPUT)
endif

all: firmware_elf
ifeq ($(OS), Windows_NT)
	$(QUITE) powershell -NoProfile -ExecutionPolicy Bypass -File "$(VENDOR_ROOT)/apps/D_Code/tools/prepare_wired_programmer.ps1" -ConfigFile "$(RESOLVED_CONFIG_MK)" -SdkDirectory "$(dir $(POST_SCRIPT))"
endif
	$(info +POST-BUILD)
	$(QUITE) $(RUN_POST_SCRIPT) sdk

link_prepare:
	$(info +LINK-PREPARE)
	$(call ENSURE_DIR,$(dir $(USED_SYMBOL_FILE)))
	$(call ENSURE_DIR,$(dir $(MOVABLE_SECTION_FILE)))
	$(call ENSURE_DIR,$(dir $(LINKER_SCRIPT)))
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P $(VENDOR_ROOT)/cpu/wl82/sdk_used_list.c -o $(USED_SYMBOL_FILE)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P $(VENDOR_ROOT)/apps/common/movable/section.c -o $(MOVABLE_SECTION_FILE)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P $(VENDOR_ROOT)/cpu/wl82/sdk_ld.c -o $(LINKER_SCRIPT).vendor
	$(QUITE) $(D_LINKER_PREPARE)

pre_build: link_prepare
	$(info +PRE-BUILD)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../../cpu/wl82/tools/download.c -o $(POST_SCRIPT)
	$(QUITE) $(FIXBAT) $(POST_SCRIPT)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../../cpu/wl82/tools/isd_config_rule.c -o ../../../../cpu/wl82/tools/isd_config.ini
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P ../../../../cpu/wl82/tools/isd_config_rule_loader.c -o ../../../../cpu/wl82/tools/isd_config_loader.ini
ifeq ($(SCREEN_PROFILE_GC9307_ENABLE),1)
	$(QUITE) $(GC9307_LSB80_PATCH_MAIN)
endif
ifneq ($(KEEP_VM),0)
	$(QUITE) $(KEEP_VM_PATCH_MAIN)
	$(QUITE) $(KEEP_VM_PATCH_LOADER)
endif
ifeq ($(D_BUILD_DOUBLE_BANK),1)
	$(QUITE) $(BOOT_VERSION_PATCH_MAIN)
endif

clean:
	$(QUITE) $(RM) $(OUT_ELF)
	$(QUITE) $(RM) $(BUILD_DIR)
	$(QUITE) $(RM) $(LEGACY_BUILD_DIRS)

# D++: 固件 Build 标识归 app_main，故每次构建都刷新该编译单元。
FORCE_APP_BUILD_STAMP:

$(BUILD_DIR)/apps/D_Code/app/app_main.c.o: FORCE_APP_BUILD_STAMP

ifeq ($(LINK_AT), 1)
$(OUT_ELF): link_prepare $(OBJS) $(D_LINK_ADAPTER_INPUTS)
	$(info +LINK $@)
	$(call ENSURE_DIR,$(@D))
	$(file >$(OBJ_FILE), $(OBJS))
	$(QUITE) $(LD) -o $(OUT_ELF) @$(OBJ_FILE) $(LFLAGS) $(LIBPATHS) $(LIBS)
	$(QUITE) $(D_LINKER_CHECK)
else
$(OUT_ELF): link_prepare $(OBJS) $(D_LINK_ADAPTER_INPUTS)
	$(info +LINK $@)
	$(call ENSURE_DIR,$(@D))
	$(file >$(OBJ_FILE), $(OBJS))
	$(QUITE) $(LD) -o $(OUT_ELF) $(OBJS) $(LFLAGS) $(LIBPATHS) $(LIBS)
	$(QUITE) $(D_LINKER_CHECK)
endif

$(BUILD_DIR)/%.c.o : $(ROOT_PREFIX)/%.c
	$(info +CC $<)
	$(call ENSURE_DIR,$(@D))
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -MF $(@:.o=.d.v2) -c $< -o $@

ifneq ($(VENDOR_ROOT),$(ROOT_PREFIX))
$(BUILD_DIR)/vendor/%.c.o : $(VENDOR_ROOT)/%.c
	$(info +CC $<)
	$(call ENSURE_DIR,$(@D))
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -MF $(@:.o=.d.v2) -c $< -o $@

$(BUILD_DIR)/vendor/%.S.o : $(VENDOR_ROOT)/%.S
	$(info +AS $<)
	$(call ENSURE_DIR,$(@D))
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -MF $(@:.o=.d.v2) -c $< -o $@
endif

$(BUILD_DIR)/%.S.o : $(ROOT_PREFIX)/%.S
	$(info +AS $<)
	$(call ENSURE_DIR,$(@D))
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -MF $(@:.o=.d.v2) -c $< -o $@

$(BUILD_DIR)/%.s.o : $(ROOT_PREFIX)/%.s
	$(info +AS $<)
	$(call ENSURE_DIR,$(@D))
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -MF $(@:.o=.d.v2) -c $< -o $@

$(BUILD_DIR)/%.cpp.o : $(ROOT_PREFIX)/%.cpp
	$(info +CXX $<)
	$(call ENSURE_DIR,$(@D))
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -MF $(@:.o=.d.v2) -c $< -o $@

$(BUILD_DIR)/%.cxx.o : $(ROOT_PREFIX)/%.cxx
	$(info +CXX $<)
	$(call ENSURE_DIR,$(@D))
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -MF $(@:.o=.d.v2) -c $< -o $@

$(BUILD_DIR)/%.cc.o : $(ROOT_PREFIX)/%.cc
	$(info +CXX $<)
	$(call ENSURE_DIR,$(@D))
	$(QUITE) $(CXX) $(CXXFLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -MF $(@:.o=.d.v2) -c $< -o $@

-include $(DEP_FILES)
