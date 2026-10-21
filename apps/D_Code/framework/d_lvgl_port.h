#ifndef D_LVGL_PORT_H
#define D_LVGL_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Screen profiles implement this shared contract inside D_Code. */
void lv_port_disp_init(void);
void lv_port_indev_init(void);
void disp_enable_update(void);
void disp_disable_update(void);

#ifdef __cplusplus
}
#endif /* D_LVGL_PORT_H */

#endif
