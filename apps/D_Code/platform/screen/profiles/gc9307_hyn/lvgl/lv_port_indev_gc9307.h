/**
 * @file lv_port_indev_gc9307.h
 * @brief 声明 UI 线程内真实触摸的取消入口。
 */
#ifndef LV_PORT_INDEV_GC9307_H
#define LV_PORT_INDEV_GC9307_H
#include <stdint.h>
/**
 * @brief 在 UI 线程复位按压和页面手势，并等待真实接触松开。
 * @note 取消而非正常 UP，不补发点击/返回；门禁先向 LVGL 交付中性样本，等待真实接触松开。
 */
void lv_port_indev_gc9307_reset(void);
#endif
