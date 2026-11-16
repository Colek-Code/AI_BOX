/**
 * @file ota_config.h
 * @brief 选择 OTA 发布包策略，不推断远端 URL 中的实际内容。
 * @note 设备与 VM 的最终影响须由实机验收确认。
 */
#ifndef D_OTA_CONFIG_H
#define D_OTA_CONFIG_H

/* 仅控制 OTA 目标包；有线擦除及保留入口分别通过 KEEP_VM 选择策略。 */
#define D_OTA_KEEP_VM 1
/* SDK 双区序号：初始基线为 0，每次发布递增；与 deployment_config.h 中的应用版本独立。 */
#define D_OTA_BOOT_VERSION 1
/* 仅配置产品发布 URL，不沿用曾用于开发板验证的地址。 */
#define D_OTA_RELEASE_URL "https://example.com/ac791/ota/version.json"

/* 网络服务关闭时，已部署的双区 Flash 布局仍须保持不变。 */
#if D_BOARD_TARGET == D_BOARD_COPY_GC9307_HYN
#define D_OTA_DOUBLE_BANK 1
#else
#define D_OTA_DOUBLE_BANK 0
#endif
#endif
