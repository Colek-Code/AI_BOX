/** @file product_ui_ota_gc9307.h @brief 将产品各资源所有者的维护收敛接到 OTA 服务。 */
#ifndef PRODUCT_UI_OTA_GC9307_H
#define PRODUCT_UI_OTA_GC9307_H
#include "framework/d_error.h"
/**
 * @brief 向 OTA 服务注册静态维护入口、排空与释放回调。
 * @return 原样返回 ota_service_register_maintenance 的结果。
 * @note 本接口只注册协调器；真正维护由 OTA 服务调用，不在此下载或改写固件。
 */
d_err_t product_ui_ota_register_backend(void);
#endif
