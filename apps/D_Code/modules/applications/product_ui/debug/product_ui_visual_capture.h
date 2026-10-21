/**
 * @file product_ui_visual_capture.h
 * @brief Validation-only Product UI RGB565 frame capture.
 */

#ifndef __PRODUCT_UI_VISUAL_CAPTURE_H__
#define __PRODUCT_UI_VISUAL_CAPTURE_H__

int product_ui_visual_capture_init(void);
int product_ui_visual_capture_request(const char *case_id);

#endif /* __PRODUCT_UI_VISUAL_CAPTURE_H__ */
