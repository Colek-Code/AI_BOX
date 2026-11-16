/**
 * @file d_page_manager.h
 * @brief 提供 UI 线程内的页面注册、生命周期和导航接口，不管理业务服务资源。
 */

#ifndef D_PAGE_MANAGER_H
#define D_PAGE_MANAGER_H

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "platform/screen/transition/d_page_transition.h"
#include "d_page_types.h"

/**
 * @brief 初始化静态页面注册表及导航状态。
 * @note 仅供 UI/LVGL 线程在启动装配时调用，不是运行期重置或销毁接口；页面回调不得调用。
 */
void page_manager_init(void);
/**
 * @brief 复制页面描述符并登记一个尚未创建的页面。
 * @param[in] desc 页面描述符；结构体被复制，name 和 ops 指向的对象仍为借用，须持续有效。
 * @return 0 表示成功；-1 表示参数无效、编号重复或注册表已满。
 * @note 仅在 UI/LVGL 线程启动阶段注册，不从页面生命周期回调中修改注册表。
 */
int page_manager_register(const page_desc_t *desc);
/**
 * @brief 在 UI 线程同步完成目标页面的生命周期切换。
 * @param[in] page_type 已注册的目标页面编号。
 * @param[in] policy 立即切换或 TE 合帧过渡策略。
 * @return 0 表示生命周期已提交或目标已经激活；-1 表示忙、目标无效或创建失败。
 * @note create、enter、exit、destroy 或容器 DELETE 回调中嵌套导航会被拒绝；同线程重入保护不提供跨线程同步。
 * @note 成功不表示 TE/DMA 传输完成；该完成事实由显示驱动管理。导航后不得继续使用先前借用的当前页面容器。
 */
int page_manager_switch_to(int page_type, page_transition_policy_e policy);
/**
 * @brief 请求立即返回主页面。
 * @note 仅供 UI 线程调用；内部忽略切换结果，重入保护拒绝时维持当前页。
 */
void page_manager_back_to_main(void);
/**
 * @brief 读取当前逻辑页面编号。
 * @return 当前记录的页面编号；启动初值为 PAGE_TYPE_MAIN，不单独证明容器已创建。
 */
int page_manager_get_current(void);
/**
 * @brief 借用当前页面容器。
 * @return 当前已登记容器，找不到页面或容器尚未创建时为 NULL。
 * @note 仅供 UI 线程即时使用；不得跨导航保存指针或自行销毁该容器。
 */
lv_obj_t *page_manager_get_current_container(void);
/**
 * @brief 调用当前页面的可选周期回调。
 * @param[in] elapsed_ms 距上轮更新经过的毫秒数。
 * @note 仅在 UI 线程执行；不为页面另建任务。
 */
void page_manager_update(uint32_t elapsed_ms);
/**
 * @brief 按页面标志处理通用返回手势或转交页面手势回调。
 * @param[in] gesture_type 输入层产生的手势类型；1 表示上滑。
 * @note 仅在 UI 线程执行；PAGE_FLAG_NO_GESTURE 会抑制通用返回和页面手势回调。
 */
void page_manager_handle_gesture(int gesture_type);

#endif /* D_LVGL_ENABLE */
#endif /* D_PAGE_MANAGER_H */
