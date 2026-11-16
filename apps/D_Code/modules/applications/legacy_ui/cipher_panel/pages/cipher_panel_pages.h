/** @file cipher_panel_pages.h @brief Cipher Panel 的页面辅助及显示／声音子页面，保留旧根 UI 的实际调用合同。 */
#ifndef __CIPHER_PANEL_PAGES_H__
#define __CIPHER_PANEL_PAGES_H__

#include "lvgl.h"
#include <stdint.h>

#define CIPHER_TOP_H              34
#define CIPHER_ACCENT             0x00B7FF
#define CIPHER_ACCENT_2           0x21E6C1
#define CIPHER_PURPLE             0x7C4DFF
#define CIPHER_WARN               0xFF4D6D
#define CIPHER_BG                 0x030712
#define CIPHER_BG_PANEL           0x06101F
#define CIPHER_CARD               0x07324A
#define CIPHER_CARD_PRESSED       0x041B2A
#define CIPHER_ROW                0x071726
#define CIPHER_LINE               0x1B4B68
#define CIPHER_TEXT               0xD6F7FF
#define CIPHER_MUTED              0x6D8B9A

/** @brief 旧 Cipher 根内部导航编号，不是 page_manager 的页面类型。 */
typedef enum {
    CIPHER_PAGE_HOME = 0,
    CIPHER_PAGE_LCD,
    CIPHER_PAGE_WIFI,
    CIPHER_PAGE_SYSTEM,
    CIPHER_PAGE_VOLUME,
    CIPHER_PAGE_AICHAT,
} cipher_page_t;

/**
 * @brief 向现有子页追加背景、状态栏、返回首页操作及标题。
 * @param[in,out] parent 由 LVGL 所有的有效父对象，新对象随父对象销毁。
 * @param[in] title 本次复制到标签的标题文字。
 * @note 须在已完成共用样式初始化的旧 Cipher UI 上下文调用；各子对象失败允许部分界面。
 */
void cipher_page_header(lv_obj_t *parent, const char *title);
/**
 * @brief 创建使用共用行样式的容器。
 * @param[in,out] parent 由 LVGL 所有的有效父对象，新对象随父对象销毁。
 * @param[in] x 相对父对象左边的像素坐标。
 * @param[in] y 相对父对象上边的像素坐标。
 * @param[in] w 对象宽度，像素。
 * @param[in] h 对象高度，像素。
 * @return 返回已挂在父对象下的新对象；该对象分配失败为 NULL。子装饰分配失败不回滚已创建的父对象。
 */
lv_obj_t *cipher_row(lv_obj_t *parent, int x, int y, int w, int h);
/**
 * @brief 创建不响应点击的实色圆角矩形。
 * @param[in,out] parent 由 LVGL 所有的有效父对象，新对象随父对象销毁。
 * @param[in] x 相对父对象左边的像素坐标。
 * @param[in] y 相对父对象上边的像素坐标。
 * @param[in] w 对象宽度，像素。
 * @param[in] h 对象高度，像素。
 * @param[in] color RGB888 背景颜色。
 * @param[in] radius 圆角半径，像素。
 * @return 返回已挂在父对象下的新对象；该对象分配失败为 NULL。子装饰分配失败不回滚已创建的父对象。
 */
lv_obj_t *cipher_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius);
/**
 * @brief 创建裁剪标签，复制文字并设置字体、颜色和对齐。
 * @param[in,out] parent 由 LVGL 所有的有效父对象，新对象随父对象销毁。
 * @param[in] x 相对父对象左边的像素坐标。
 * @param[in] y 相对父对象上边的像素坐标。
 * @param[in] w 对象宽度，像素。
 * @param[in] text 本次复制的文字。
 * @param[in] font LVGL 借用的字体，须覆盖标签生存期。
 * @param[in] color RGB888 文字颜色。
 * @param[in] align LVGL 文本对齐方式。
 * @return 返回父容器持有的新标签；标签分配失败为 NULL。
 */
lv_obj_t *cipher_text(lv_obj_t *parent, const char *text, int x, int y, int w,
                      const lv_font_t *font, uint32_t color, lv_text_align_t align);
/**
 * @brief 创建固定二十像素高的强调色标签卡片。
 * @param[in,out] parent 由 LVGL 所有的有效父对象，新对象随父对象销毁。
 * @param[in] x 相对父对象左边的像素坐标。
 * @param[in] y 相对父对象上边的像素坐标。
 * @param[in] w 对象宽度，像素。
 * @param[in] text 本次复制到子标签的文字。
 * @param[in] accent RGB888 强调色。
 * @return 返回已挂在父对象下的新对象；该对象分配失败为 NULL。子装饰分配失败不回滚已创建的父对象。
 */
lv_obj_t *cipher_chip(lv_obj_t *parent, const char *text, int x, int y, int w, uint32_t accent);
/**
 * @brief 创建点击后切换旧根子页的导航卡片。
 * @param[in,out] parent 由 LVGL 所有的有效父对象，新对象随父对象销毁。
 * @param[in] x 相对父对象左边的像素坐标。
 * @param[in] y 相对父对象上边的像素坐标。
 * @param[in] w 对象宽度，像素。
 * @param[in] h 对象高度，像素。
 * @param[in] text 卡片文字，NULL 或空串时只显示图标。
 * @param[in] icon 本次复制到标签的 LVGL 图标字符串。
 * @param[in] page 保存为事件数值的目标页面，点击时再由导航路径校验。
 * @param[in] accent RGB888 强调色。
 * @return 返回已挂在父对象下的新对象；该对象分配失败为 NULL。子装饰分配失败不回滚已创建的父对象。
 */
lv_obj_t *cipher_button(lv_obj_t *parent, const char *text, const char *icon,
                        int x, int y, int w, int h, cipher_page_t page, uint32_t accent);

/**
 * @brief 初始化旧根 UI 所需的共用字幕订阅。
 */
void cipher_panel_pages_init(void);
/**
 * @brief 创建亮度、灭屏期限及触摸唤醒控件，并读取当前平台值作为初值。
 * @param[in,out] parent 调用方准备的页面父对象。
 * @note 亮度在 VALUE_CHANGED 同步应用，未采用 Product UI 的松手受理策略；void 接口允许部分对象创建后返回。
 */
void cipher_panel_show_lcd(lv_obj_t *parent);
/**
 * @brief 创建旧根 WiFi 状态、扫描列表及操作，功能启用时受理首次扫描。
 * @param[in,out] parent 调用方准备的有效页面父容器。
 * @note 功能关闭时只显示既有不可用文字；void 接口不保证所有子对象完整创建。
 */
void cipher_panel_show_wifi(lv_obj_t *parent);
/**
 * @brief 创建 WiFi、显示及声音三个固定子页导航按钮。
 * @param[in,out] parent 调用方准备的页面父对象。
 */
void cipher_panel_show_system(lv_obj_t *parent);
/**
 * @brief 创建设备音量滑条及数值，以当前兼容同步音量作为初值。
 * @param[in,out] parent 调用方准备的页面父对象。
 * @note VALUE_CHANGED 调用同步兼容 set，后端在当前 UI 上下文执行，不能按异步请求完成来解释。
 */
void cipher_panel_show_volume(lv_obj_t *parent);
/**
 * @brief 委托旧 Cipher 字幕视图创建当前 AIChat 页面。
 * @param[in,out] parent 调用方准备的页面父对象。
 */
void cipher_panel_show_aichat(lv_obj_t *parent);
/**
 * @brief 只为当前字幕页或启用的 WiFi 页转发必要周期。
 * @param[in] page 当前旧根页面枚举。
 * @param[in] elapsed_ms 本轮 UI 间隔毫秒数。
 */
void cipher_panel_pages_update(cipher_page_t page, uint32_t elapsed_ms);
/**
 * @brief 消费匹配编号及种类的 WiFi 请求完成，按周期刷新状态及扫描。
 * @param[in] elapsed_ms 本轮 UI 间隔毫秒数。
 * @note 仅当前 WiFi 页存活时调用；功能关闭分支忽略参数。完成确认不自行等同 DHCP 已完成。
 */
void cipher_panel_wifi_update(uint32_t elapsed_ms);

#endif /* __CIPHER_PANEL_PAGES_H__ */
