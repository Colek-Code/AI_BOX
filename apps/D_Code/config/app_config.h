#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "board_config.h"

/*=============================================================================
 * D_Code 功能配置菜单
 *
 * 这是普通开发者需要查看的第二个配置文件。
 * 互斥项填写一个命名值；独立功能只填写 0（关闭）或 1（开启）。
 *===========================================================================*/

#define D_UI_PRODUCT                 2001
#define D_UI_TABLET_SHELL            2002
#define D_UI_SCREEN_TEST             2003
#define D_UI_CIPHER_PANEL            2004
#define D_UI_LAUNCHER_DEMO           2005
#define D_UI_LIGHT_SPOT_DEMO         2006

/*--------------------------------------------------------------------------
 * 1. 选择主界面：只能填写一个可选值
 *
 * D_UI_PRODUCT          GC 横屏产品界面
 * D_UI_TABLET_SHELL     GC 横屏框架/触控调试界面
 * D_UI_SCREEN_TEST      GC 屏幕基础测试界面
 * D_UI_CIPHER_PANEL     EMI 旧版控制面板
 * D_UI_LAUNCHER_DEMO    EMI 旧版 Launcher 示例
 * D_UI_LIGHT_SPOT_DEMO  EMI 旧版光斑动画示例
 *-------------------------------------------------------------------------*/

#define D_UI_TARGET  D_UI_PRODUCT

/*--------------------------------------------------------------------------
 * 2. 基础设备服务：0 = 关闭，1 = 开启
 *-------------------------------------------------------------------------*/

#define D_FEATURE_KEY_SERVICE        1  /* 物理按键事件服务。 */
#define D_FEATURE_POWER_CONTROL      1  /* 电源键与关机控制服务。 */

/*--------------------------------------------------------------------------
 * 3. 网络与连接：0 = 关闭，1 = 开启
 *-------------------------------------------------------------------------*/

#define D_FEATURE_WIFI               1  /* WiFi STA 联网服务。 */
#define D_FEATURE_BLE_PROVISION      1  /* BLE 配网服务。 */
#define D_FEATURE_WEBSOCKET          1  /* 自有设备云：Qwen全托管时自动隔离，半托管时必须开启。 */

/*--------------------------------------------------------------------------
 * 4. 可选应用：0 = 关闭，1 = 开启
 *-------------------------------------------------------------------------*/

#define D_FEATURE_AI_CHAT            0  /* 旧 HTTP 文本 AI 应用；不控制 Product UI 的 Qwen 入口。 */
#define D_FEATURE_CAMERA             1  /* Product 实时预览；其他根 UI 保留旧相机选择。 */
#define D_FEATURE_GALLERY            0  /* 旧版相册应用。 */
#define D_FEATURE_LOCAL_MUSIC        0  /* 旧版本地音乐服务。 */

/*--------------------------------------------------------------------------
 * 5. 语音与音频：0 = 关闭，1 = 开启
 *
 * Qwen、BRTC、RTC Audio 三者互斥；小乐唤醒依赖 Qwen。
 *-------------------------------------------------------------------------*/

#define D_FEATURE_QWEN               1  /* Qwen 多模态语音服务。 */
#define D_FEATURE_XIAOLE_WAKEUP      1  /* 小乐本地唤醒词服务。 */
#define D_FEATURE_BRTC               0  /* 百度 RTC 智能体。 */
#define D_FEATURE_RTC_AUDIO          0  /* RTC 音频环回验证。 */

/*--------------------------------------------------------------------------
 * 6. 系统功能：0 = 关闭，1 = 开启
 *-------------------------------------------------------------------------*/

#define D_FEATURE_OTA                1  /* Product HTTPS OTA；需 16 MB 双备份基线。 */

/*--------------------------------------------------------------------------
 * 7. 调试开关：正常固件保持为 0
 *-------------------------------------------------------------------------*/

#define D_DEBUG_BOARD_LOG            0  /* 板级启动诊断日志。 */
#define D_DEBUG_UI_PERFORMANCE       0  /* Product UI 性能/内存统计。 */
#define D_DEBUG_VISUAL_CAPTURE       0  /* UART2 RGB565 视觉采集。 */

/*=============================================================================
 * 内部配置解析区：普通开发者不要修改以下内容
 *===========================================================================*/

#include "config/ota_config.h"
#include "config/internal/d_config_validate.h"
#include "config/qwen_config.h"
#include "config/internal/d_cloud_resolve.h"
#include "config/deployment_config.h"
#include "config/runtime_config.h"
#include "config/internal/d_board_resolve.h"
#include "config/internal/d_feature_resolve.h"
#ifndef D_CONFIG_RESOLVE_ONLY
#include "config/internal/d_sdk_config.h"
#endif

#endif /* APP_CONFIG_H */
