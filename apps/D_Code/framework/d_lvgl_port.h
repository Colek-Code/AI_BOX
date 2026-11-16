/**
 * @file d_lvgl_port.h
 * @brief 声明显示配置实现的共用 LVGL 装配钩子，不依赖产品页面或调试私有实现。
 */

#ifndef D_LVGL_PORT_H
#define D_LVGL_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 为所选屏幕注册 LVGL 显示设备。
 * @note 由 LVGL 任务启动阶段调用；是否成功须检查默认显示设备，不通过 void 返回判定。
 */
void lv_port_disp_init(void);
/**
 * @brief 注册所选触摸配置对应的 LVGL 输入设备。
 * @note 由 LVGL 任务在显示初始化后调用；不供 USB 线程直接调用。
 */
void lv_port_indev_init(void);
/**
 * @brief 允许所选显示适配继续提交更新。
 * @note 由显示配置提供实现，不代表此前 DMA 已经完成。
 */
void disp_enable_update(void);
/**
 * @brief 暂停所选显示适配的新更新。
 * @note 不把此入口当成底层在途 DMA 已停止的完成确认。
 */
void disp_disable_update(void);

#ifdef __cplusplus
}
#endif /* D_LVGL_PORT_H */

#endif
