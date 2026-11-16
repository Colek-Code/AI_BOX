/** @file product_ui_visual_capture.h @brief 验证用现有 GC9307 过渡暂存帧导出，带请求身份及像素 CRC。 */

#ifndef __PRODUCT_UI_VISUAL_CAPTURE_H__
#define __PRODUCT_UI_VISUAL_CAPTURE_H__

/**
 * @brief 在截图诊断启用时安装过渡帧观察器，关闭时为空操作。
 * @return 当前两种编译分支均返回 0；不证明 UART 或帧已就绪。
 */
int product_ui_visual_capture_init(void);
/**
 * @brief 在截图诊断启用时排入一次延后帧捕获。
 * @param[in] case_id 调用期间借用的非空用例标识，启用时复制到固定十五字节有效长度内。
 * @return 受理返回 0；启用时空标识、已有请求／定时器或定时器失败返回 -1；关闭分支直接返回 0。
 * @note 受理不表示已导出；实际帧须匹配专用过渡编号，日志报告传输及 UART 结果。
 */
int product_ui_visual_capture_request(const char *case_id);

#endif /* __PRODUCT_UI_VISUAL_CAPTURE_H__ */
