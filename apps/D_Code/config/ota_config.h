/** OTA release policy. This selects the generated target package, not a remote
 * URL's unknown contents. Device/VM effects require device acceptance. */
#ifndef D_OTA_CONFIG_H
#define D_OTA_CONFIG_H

/* OTA target package only. Wired erase/keep entries select KEEP_VM separately. */
#define D_OTA_KEEP_VM 1
/* SDK dual-bank sequence: initial baseline 0, then increment for each release.
 * Keep this separate from the application version in deployment_config.h. */
#define D_OTA_BOOT_VERSION 1
/* Product release URL only. The tested DevKitBoard URL is intentionally absent. */
#define D_OTA_RELEASE_URL ""

/* A deployed dual-bank layout must not change when its network service is off. */
#if D_BOARD_TARGET == D_BOARD_COPY_GC9307_HYN
#define D_OTA_DOUBLE_BANK 1
#else
#define D_OTA_DOUBLE_BANK 0
#endif
#endif
