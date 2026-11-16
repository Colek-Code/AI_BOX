/**
 * @file app_main.c
 * @brief 装配 D_Code 启动链并适配 SDK 应用事件；业务资源由各服务自行管理。
 */

//*********************************************************************************//
//                              D_Code 应用程序主入口                               //
//                                                                                 //
//   1. 事件总线 d_event_bus_init() 在所有模块之前初始化                            //
//   2. 所有模块通过 xxx_service_init() 显式调用                                  //
//   3. 设备事件（SD/WiFi）转发到事件总线                                        //
//   4. 保留 SDK 框架所需的 irq_info_table / task_info_table / REGISTER_APPLICATION //
//   5. 启动顺序：事件总线 → BSP → 服务清单 → UI → SDK 应用框架                //
//*********************************************************************************//

#include "config/app_config.h"
#if D_DEBUG_AUTH_CODE_READBACK
#include "modules/diagnostics/auth_code/d_auth_code_probe.h"
#endif
#if D_QWEN_FACTORY_TEST_BUILD
#include "modules/integrations/qwen/qwen_factory.h"
#endif
#include "system/includes.h"
#include "os/os_api.h"
#include "system/app_core.h"
#include "event/key_event.h"
#include "event/device_event.h"
#ifdef CONFIG_NET_ENABLE
#include "event/net_event.h"
#include "device/device.h"
#ifdef CONFIG_MBEDTLS_ENABLE
#include "framework/d_crypto.h"
#include "ntp/ntp.h"
#include <time.h>
#endif
#endif
#ifdef CONFIG_WIFI_ENABLE
#include "wifi/wifi_connect.h"
#include "net/config_network.h"
#endif

#include "framework/d_event_bus.h"
#include "config/d_config_report.h"
#include "framework/d_error.h"
#include "framework/d_log.h"
#include "app/d_service_startup.h"
#include "app/d_ui_startup.h"
#if (D_UI_TARGET == D_UI_PRODUCT) && D_DEBUG_UI_PERFORMANCE
#include "modules/applications/product_ui/debug/product_ui_perf.h"
#endif
#include "platform/storage/d_storage.h"

#if D_FEATURE_KEY_SERVICE
#include "modules/services/key/d_key_service.h"
#endif
#if D_FEATURE_BLE_PROVISION
#include "modules/services/ble_provisioning/d_ble_net_cfg.h"
#endif

#ifdef D_LCD_ENABLE
#include "platform/screen/d_display.h"
#endif

#ifdef D_LVGL_ENABLE
#include "framework/d_lvgl.h"
#endif


#define TAG "APP"

typedef struct {
	uint16_t optional_failure_count;
	volatile u8 has_startup_error;
#ifdef D_LVGL_ENABLE
	volatile d_err_t ui_result;
	volatile u8 is_ui_result_published;
	volatile u8 is_ui_result_consumed;
	volatile u8 is_ui_init_pending;
	volatile u8 is_initial_summary_reported;
#endif
} d_app_startup_tracker_t;

static d_app_startup_tracker_t s_app_startup = {
#ifdef D_LVGL_ENABLE
	.ui_result = D_ERR_NOT_READY,
#endif
};

/**
 * @brief 根据已记录的必需错误、UI 在途状态及可选失败输出启动摘要。
 * @note 不触发服务重试；摘要不替代各服务的实际就绪快照。
 */
static void d_app_log_startup_summary(void)
{
	if (s_app_startup.has_startup_error != 0U) {
		LOGE(TAG, "系统启动存在初始化错误");
#ifdef D_LVGL_ENABLE
	} else if (s_app_startup.is_ui_init_pending != 0U) {
		LOGW(TAG, "系统基础服务启动完成，LVGL UI 仍在后台初始化");
#endif
	} else if (s_app_startup.optional_failure_count != 0U) {
		LOGW(TAG,
		     "系统启动完成，%u 个可选服务不可用",
		     (unsigned int)s_app_startup.optional_failure_count);
	} else {
		LOGI(TAG, "系统启动完成");
	}
}

#ifdef D_LVGL_ENABLE
/**
 * @brief 在临界区内登记首个尚未消费的 UI 初始化结果。
 * @param[in] result LVGL 初始化完成结果。
 * @note 重复或已消费结果不会覆盖当前记录，不在临界区内打印或执行业务回调。
 */
static void d_app_publish_ui_result(d_err_t result)
{
	OS_ENTER_CRITICAL();
	if ((s_app_startup.is_ui_result_published == 0U) &&
	    (s_app_startup.is_ui_result_consumed == 0U)) {
		s_app_startup.ui_result = result;
		s_app_startup.is_ui_result_published = 1U;
	}
	OS_EXIT_CRITICAL();
}

/**
 * @brief 终态尚未登记或消费时，标记 UI 仍在后台初始化。
 * @return 本次成功标记在途时为 1；完成回调已经取得结果时为 0。
 */
static int d_app_mark_ui_init_pending(void)
{
	int is_pending = 0;

	OS_ENTER_CRITICAL();
	if ((s_app_startup.is_ui_result_published == 0U) &&
	    (s_app_startup.is_ui_result_consumed == 0U)) {
		s_app_startup.is_ui_init_pending = 1U;
		is_pending = 1;
	}
	OS_EXIT_CRITICAL();
	return is_pending;
}

/**
 * @brief 只消费一次已发布的 UI 结果，更新启动事实并在锁外打印。
 * @param[in] log_updated_summary 非零时在消费终态后追加更新后的启动摘要。
 * @return 本次取得一个未消费终态时为 1；没有可消费结果时为 0。
 * @note 只在临界区内更新结果和消费标志；日志输出发生在临界区外。
 */
static int d_app_finalize_ui_result(int log_updated_summary)
{
	d_err_t result = D_ERR_NOT_READY;
	int has_terminal_result = 0;
	int was_pending = 0;

	OS_ENTER_CRITICAL();
	if ((s_app_startup.is_ui_result_published != 0U) &&
	    (s_app_startup.is_ui_result_consumed == 0U)) {
		result = s_app_startup.ui_result;
		s_app_startup.is_ui_result_consumed = 1U;
		was_pending = (s_app_startup.is_ui_init_pending != 0U) ? 1 : 0;
		s_app_startup.is_ui_init_pending = 0U;
		if (result != D_OK) {
			s_app_startup.has_startup_error = 1U;
		}
		has_terminal_result = 1;
	}
	OS_EXIT_CRITICAL();

	if (has_terminal_result == 0) {
		return 0;
	}
	if (result == D_OK) {
		if (was_pending != 0) {
			LOGI(TAG, "LVGL UI 已在后台完成初始化");
		} else {
			LOGI(TAG, "LVGL UI 初始化完成");
		}
	} else {
		if (was_pending != 0) {
			LOGE(TAG, "LVGL 后台初始化失败: %d", result);
		} else {
			LOGE(TAG, "LVGL 初始化失败: %d", result);
		}
	}
	if (log_updated_summary != 0) {
		d_app_log_startup_summary();
	}
	return 1;
}

/**
 * @brief 接收 LVGL 完成回调，首轮摘要已发布时立即消费并补报。
 * @param[in] result LVGL 初始化终态。
 * @param[in] context 装配时传入的借用上下文，当前未使用。
 * @note 回调可以早于同步启动等待结束；发布与消费标志共同避免重复报终态。
 */
static void d_app_on_lvgl_init_complete(d_err_t result, void *context)
{
	int can_finalize;

	(void)context;
	d_app_publish_ui_result(result);
	OS_ENTER_CRITICAL();
	can_finalize = (s_app_startup.is_initial_summary_reported != 0U) ? 1 : 0;
	OS_EXIT_CRITICAL();

	if (can_finalize != 0) {
		(void)d_app_finalize_ui_result(1);
	}
}
#endif

//*********************************************************************************//
//                              弱函数定义（SDK 要求）                              //
//*********************************************************************************//

/**
 * @brief 提供 SDK 所需的弱兼容入口，当前不调整通信计数。
 * @param[in] phcom_cnt SDK 传入的计数，当前未使用。
 */
__attribute__((weak)) void wf_set_phcom_cnt(u32 phcom_cnt) { (void)phcom_cnt; }
/**
 * @brief 提供 SDK 所需的弱 C++ 初始化入口，当前实现不执行额外初始化。
 */
__attribute__((weak)) void cpp_run_init(void) {}

//*********************************************************************************//
//                              中断优先级配置表（保留原有）                        //
//*********************************************************************************//
const struct irq_info irq_info_table[] = {
#ifdef CONFIG_IPMASK_ENABLE
	{ IRQ_SOFT5_IDX,      6,   0    },
	{ IRQ_SOFT4_IDX,      6,   1    },
#endif
#if CPU_CORE_NUM == 1
	{ IRQ_SOFT5_IDX,      7,   0    },
	{ IRQ_SOFT4_IDX,      7,   1    },
	{ -2,     		     -2,  -2    },
#endif
	{ -1,                -1,  -1    },
};

//*********************************************************************************//
//                              任务配置表                             //
//*********************************************************************************//
const struct task_info task_info_table[] = {
	{"init",                30,      512,    256   },
	{"app_core",            15,     2048,   1024   },
	{"sys_event",           29,      512,      0   },
	{"systimer",            14,      256,      0   },
	{"sys_timer",            9,      512,    128   },
	{"update",              21,      512,     32   },
	{"dw_update",           21,      512,     32   },
#ifdef D_LCD_ENABLE
	{"lcd_task_0",           9,     1024,     32   },
	{"lcd_task_1",           9,     1024,     32   },
	{"te_task",             10,     1024,     32   },
#endif
#ifdef D_LVGL_ENABLE
	{"ui",                  21,      768,    256   },
#endif
#ifdef CONFIG_AUDIO_ENABLE
	{"audio_server",        16,      512,     64   },
	{"audio_decoder",       30,     1024,     64   },
	{"audio_mix",           28,      512,      0   },
	{"audio_encoder",       12,      384,     64   },
	{"speex_encoder",       10,     1024,      0   },
	{"mp3_encoder",         13,      768,      0   },
	{"jla_encoder",         13,      768,      0   },
	{"aac_encoder",         13,      768,      0   },
	{"opus_encoder",        10,     1536,      0   },
	{"ogg_encoder",         10,     1536,      0   },
	{"vir_dev_task",        13,      256,      0   },
	{"amr_encoder",         16,     1024,      0   },
	{"cvsd_encoder",        13,      512,      0   },
	{"vad_encoder",         16,     1024,      0   },
	{"aec_encoder",         13,     1024,      0   },
	{"dns_encoder",         13,      512,      0   },
	{"msbc_encoder",        13,      256,      0   },
	{"sbc_encoder",         13,      512,      0   },
	{"adpcm_encoder",       13,      512,      0   },
	{"echo_deal",           11,     1024,     32   },
#endif
	{"usb_server",          20,     1024,     64   },
#if CPU_CORE_NUM > 1
	{"#C0usb_msd0",          1,      512,    128   },
#else
	{"usb_msd0",             1,      512,    128   },
#endif
	{"usb_msd1",             1,      512,    128   },
	{"uac_sync",            20,      512,      0   },
	{"uac_play",             7,      768,      0   },
	{"uac_record",           7,      768,     32   },
#ifdef CONFIG_NET_ENABLE
	{"tcpip_thread",        16,      800,      0   },
#endif
#ifdef CONFIG_WIFI_ENABLE
	{"tasklet",             10,     1400,      0   },
	{"RtmpMlmeTask",        17,      700,      0   },
	{"RtmpCmdQTask",        17,      300,      0   },
	{"wl_rx_irq_thread",     5,      256,      0   },
#endif
#ifdef CONFIG_VIDEO_ENABLE
	{"video_server",        26,      800,   1024   },
	{"vpkg_server",         26,      512,    512   },
	{"jpg_dec",             27,     1024,     32   },
	{"video0_rec0",         25,      512,    512   },
	{"video_dec_server",    27,     1024,   1024   },
#endif
#ifdef CONFIG_BT_ENABLE
#if CPU_CORE_NUM > 1
	{"#C0btencry",          16,      512,    128   },
	{"#C0btctrler",         19,      512,    384   },
	{"#C0btstack",          18,     1024,    384   },
#else
	{"btencry",             14,      512,    128   },
	{"btctrler",            19,      512,    384   },
	{"btstack",             18,      768,    384   },
#endif
#endif
	{0, 0, 0, 0, 0},
};

//*********************************************************************************//
//                    SDK 事件 → 事件总线转发                                       //
//*********************************************************************************//

/**
 * @brief 接收 SDK 应用状态通知并输出诊断，不执行额外状态迁移。
 * @param[in] app SDK 应用对象，当前未使用。
 * @param[in] state SDK 应用状态。
 * @param[in] it SDK 意图对象，当前未使用。
 * @return 当前始终返回 0。
 */
static int d_app_state_machine(struct application *app, enum app_state state, struct intent *it)
{
	LOGD(TAG, "State: %d", state);
	return 0;
}

/**
 * @brief 将 SD0/SD1 插拔先登记到存储事实，再通知事件总线。
 * @param[in] dev SDK 借用的设备事件，只在当前处理期间读取。
 * @return dev 或其 arg 为空时为 false；其他输入为 true，不代表某个业务已经处理完成。
 * @note 介质代次先更新，异步通知可能丢失；消费者仍须查询存储快照，不能依靠通知作为唯一事实。
 */
static int d_app_device_event_handler(struct device_event *dev)
{
	if ((dev == NULL) || (dev->arg == NULL)) {
		return false;
	}

	if (!strcmp(dev->arg, "sd0") || !strcmp(dev->arg, "sd1")) {
		switch (dev->event) {
		case DEVICE_EVENT_IN:
			d_storage_sd_notify((uint8_t)(((const char *)dev->arg)[2] - '0'), 1U);
			LOGI(TAG, "SD 插入: %s", dev->arg);
			d_event_post(EVT_SD_INSERTED, NULL, 0);
			break;
		case DEVICE_EVENT_OUT:
			d_storage_sd_notify((uint8_t)(((const char *)dev->arg)[2] - '0'), 0U);
			LOGI(TAG, "SD 拔出: %s", dev->arg);
			d_event_post(EVT_SD_REMOVED, NULL, 0);
			break;
		}
	}
	return true;
}

#if defined(CONFIG_NET_ENABLE) && defined(CONFIG_MBEDTLS_ENABLE)
/**
 * @brief 确认 SDK NTP 已成功并校验时间范围，再设置密码工具的时间基准。
 * @return 0 表示本次已同步；-1 表示 NTP 未就绪或时间不在支持范围。
 */
static int d_app_sync_crypto_time_from_sdk(void)
{
	int64_t epoch_sec;
	if (ntp_client_get_time_status() != 1U) return -1;
	epoch_sec = (int64_t)time(NULL);
	if (epoch_sec < 1704067200LL || epoch_sec > INT32_MAX) return -1;
	d_crypto_set_ntp_time((unsigned long long)epoch_sec);
	return 0;
}
#endif

#ifdef CONFIG_NET_ENABLE
/**
 * @brief 将 SDK 网络事件转发到总线，并连接配网完成及 NTP 校时链路。
 * @param[in] net SDK 借用的网络事件，只在当前处理期间读取。
 * @return net 或其 arg 为空时为 false；其他输入为 true，不代表 Wi-Fi 或 AI 已就绪。
 * @note 总线 NTP 通知仍按 SDK 成功事件发出；密码时间是否可用由校时状态接口另行确认。
 */
static int d_app_net_event_handler(struct net_event *net)
{
	if ((net == NULL) || (net->arg == NULL)) {
		return false;
	}

	if (!strcmp(net->arg, "net")) {
		switch (net->event) {
		case NET_EVENT_CONNECTED:
			LOGI(TAG, "网络连接");
			d_event_post(EVT_WIFI_CONNECTED, NULL, 0);
			break;
		case NET_EVENT_DISCONNECTED:
			LOGI(TAG, "网络断开");
			d_event_post(EVT_WIFI_DISCONNECTED, NULL, 0);
			break;
		case NET_CONNECT_TIMEOUT_NOT_FOUND_SSID:
			LOGW(TAG, "SSID 未找到");
			d_event_post(EVT_WIFI_SSID_NOT_FOUND, NULL, 0);
			break;
		case NET_CONNECT_ASSOCIAT_FAIL:
			LOGW(TAG, "WiFi 连接失败");
			d_event_post(EVT_WIFI_CONNECT_FAIL, NULL, 0);
			break;
		case NET_EVENT_SMP_CFG_FINISH:
			LOGI(TAG, "BLE 配网完成");
#if D_FEATURE_BLE_PROVISION
			{
				config_network_connect();
			}
#endif
			break;
		case NET_NTP_GET_TIME_SUCC:
#ifdef CONFIG_MBEDTLS_ENABLE
			if (d_app_sync_crypto_time_from_sdk() == 0) {
				LOGI(TAG, "NTP 时间已同步到 CRYPTO");
			} else {
				LOGW(TAG, "NTP 时间同步到 CRYPTO 失败");
			}
#endif
			d_event_post(EVT_NTP_SYNCED, NULL, 0);
			break;
		}
	}
	return true;
}
#endif

/**
 * @brief 按 SDK 系统事件类型分派按键、设备、网络及 BLE 事件。
 * @param[in] app SDK 应用对象，当前未使用。
 * @param[in] sys_event SDK 保证有效的系统事件；载荷按类型解释，不在此保存。
 * @return 按键被消费或设备/网络适配接受时返回 true；其他路径返回 false。
 */
static int d_app_event_handler(struct application *app, struct sys_event *sys_event)
{
	switch (sys_event->type) {
#if D_FEATURE_KEY_SERVICE
	case SYS_KEY_EVENT:
		{
			struct key_event *key;

			key = (struct key_event *)sys_event->payload;
			if ((key != NULL) && (d_key_service_dispatch(key) != 0)) {
				return true;
			}
		}
		break;
#endif
	case SYS_DEVICE_EVENT:
		return d_app_device_event_handler((struct device_event *)sys_event->payload);
#ifdef CONFIG_NET_ENABLE
	case SYS_NET_EVENT:
		return d_app_net_event_handler((struct net_event *)sys_event->payload);
#endif
#if D_FEATURE_BLE_PROVISION
	case SYS_BT_EVENT:
		{
			ble_demo_bt_event_handler(sys_event);
		}
		break;
#endif
	default:
		break;
	}
	return false;
}

//*********************************************************************************//
//                              SDK 应用程序注册                          //
//*********************************************************************************//

static const struct application_operation d_app_ops = {
	.state_machine  = d_app_state_machine,
	.event_handler  = d_app_event_handler,
};

REGISTER_APPLICATION(d_app) = {
	.name   = "d_app",
	.ops    = &d_app_ops,
	.state  = APP_STA_DESTROY,
};

//*********************************************************************************//
//                              app_main() — 显式初始化入口                        //
//*********************************************************************************//

/**
 * @brief 应用程序主入口
 *
 * 初始化顺序（严格控制依赖）：
 *   Phase 0: C++ 环境 / 日志
 *   Phase 1: 事件总线（所有模块通信基础）
 *   Phase 2: BSP 层（LCD/Touch/Storage）
 *   Phase 3: app/d_service_startup.c 中的显式服务启动顺序
 *   Phase 4: 模块系统 + UI 框架
 *   Phase 5: SDK 应用框架启动
 */
void app_main()
{
#if D_QWEN_FACTORY_TEST_BUILD
    qwen_factory_start();
    return;
#endif
	d_err_t init_err;
	d_service_start_result_t service_result = {0};
	int is_event_bus_ready = 0;
	int is_lcd_task_ready = 1;

	s_app_startup.optional_failure_count = 0U;
	s_app_startup.has_startup_error = 0U;
#ifdef D_LVGL_ENABLE
	s_app_startup.ui_result = D_ERR_NOT_READY;
	s_app_startup.is_ui_result_published = 0U;
	s_app_startup.is_ui_result_consumed = 0U;
	s_app_startup.is_ui_init_pending = 0U;
	s_app_startup.is_initial_summary_reported = 0U;
#endif
#ifdef CONFIG_CXX_SUPPORT
	cpp_run_init();
#endif

	printf("\r\n\r\n");
	printf("============================================\r\n");
	printf("  TUDA_Agent\r\n");
	printf("  Build: %s %s\r\n", __DATE__, __TIME__);
	printf("============================================\r\n\r\n");
	d_config_report_print();
#if D_DEBUG_AUTH_CODE_READBACK
	d_auth_code_probe_run();
#endif

	/* ═══════════════════════════════════════════════════════════ */
	/*  Phase 1: 事件总线初始化（必须最先）                        */
	/* ═══════════════════════════════════════════════════════════ */
	LOGI(TAG, "[Phase 1] 事件总线初始化");
	init_err = d_event_bus_init();
	if (init_err == D_OK) {
		is_event_bus_ready = 1;
	} else {
		LOGE(TAG, "事件总线初始化失败: %d", init_err);
		s_app_startup.has_startup_error = 1U;
	}

	/* ═══════════════════════════════════════════════════════════ */
	/*  Phase 2: BSP 层                                           */
	/* ═══════════════════════════════════════════════════════════ */
	LOGI(TAG, "[Phase 2] BSP 初始化");

	/* 存储设备 */
	d_storage_init();


	/* LCD + 触摸 */
#ifdef D_LCD_ENABLE
	init_err = d_display_init();
	if (init_err == D_OK) {
		LOGI(TAG, "  ✓ LCD BSP 任务");
	} else {
		LOGE(TAG, "LCD BSP 初始化失败: %d", init_err);
		is_lcd_task_ready = 0;
		s_app_startup.has_startup_error = 1U;
	}
#endif

	/* ═══════════════════════════════════════════════════════════ */
	/*  Phase 3: 服务模块显式初始化                                */
	/* ═══════════════════════════════════════════════════════════ */
	LOGI(TAG, "[Phase 3] 服务模块初始化");
	if (is_event_bus_ready != 0) {
		service_result = d_service_start_all();
		if (service_result.required_failure_count != 0U) {
			LOGE(TAG,
			     "必需服务初始化失败: count=%u first=%d",
			     (unsigned int)service_result.required_failure_count,
			     service_result.first_required_error);
			s_app_startup.has_startup_error = 1U;
		}
		if (service_result.optional_failure_count != 0U) {
			LOGW(TAG,
			     "可选服务降级: count=%u first=%d",
			     (unsigned int)service_result.optional_failure_count,
			     service_result.first_optional_error);
		}
	} else {
		LOGE(TAG, "跳过服务初始化: 事件总线未就绪");
	}
	s_app_startup.optional_failure_count = service_result.optional_failure_count;

	/* ═══════════════════════════════════════════════════════════ */
	/*  Phase 4: UI 框架 + 模块系统                               */
	/* ═══════════════════════════════════════════════════════════ */
#ifdef D_LVGL_ENABLE
	LOGI(TAG, "[Phase 4] UI 框架初始化");
	if ((is_lcd_task_ready != 0) && (is_event_bus_ready != 0)) {
		d_lvgl_init_snapshot_t ui_snapshot = {0};
		d_lvgl_init_spec_t ui_spec = {
			.bootstrap = d_ui_startup_run,
			.update = d_ui_startup_update,
#if (D_UI_TARGET == D_UI_PRODUCT) && D_DEBUG_UI_PERFORMANCE
			.cycle_complete_cb = product_ui_perf_cycle,
#endif
			.font_fallback_tail = d_ui_startup_font_fallback_tail(),
			.complete_cb = d_app_on_lvgl_init_complete,
			.complete_context = NULL,
#if D_BOARD_IS_GC9307 && (D_UI_TARGET == D_UI_PRODUCT)
			.worker_priority = D_PRODUCT_UI_TASK_PRIORITY,
#endif
		};
		d_err_t snapshot_err;

		init_err = d_lvgl_init(&ui_spec);
		snapshot_err = d_lvgl_get_init_snapshot(&ui_snapshot);
		if (snapshot_err != D_OK) {
			LOGE(TAG, "读取 LVGL 初始化快照失败: %d", snapshot_err);
			OS_ENTER_CRITICAL();
			s_app_startup.is_ui_init_pending = 0U;
			s_app_startup.has_startup_error = 1U;
			OS_EXIT_CRITICAL();
		} else if (ui_snapshot.is_result_published != 0U) {
			d_app_publish_ui_result(ui_snapshot.result);
			(void)d_app_finalize_ui_result(0);
		} else if ((ui_snapshot.state == D_LVGL_INIT_LATE_PENDING) ||
		           (ui_snapshot.state == D_LVGL_INIT_STARTING)) {
			if (d_app_mark_ui_init_pending() != 0) {
				LOGW(TAG,
				     "LVGL 同步等待已分离: wait_result=%d，后台初始化仍在继续",
				     init_err);
			} else {
				/* 完成回调已先于在途标记登记结果，此处接着消费，不能再标成等待。 */
				(void)d_app_finalize_ui_result(0);
			}
		} else {
			LOGE(TAG, "LVGL 初始化失败: %d", init_err);
			OS_ENTER_CRITICAL();
			s_app_startup.is_ui_init_pending = 0U;
			s_app_startup.has_startup_error = 1U;
			OS_EXIT_CRITICAL();
		}
	} else if (is_lcd_task_ready == 0) {
		LOGE(TAG, "跳过 LVGL 初始化: LCD BSP 未就绪");
		s_app_startup.has_startup_error = 1U;
	} else {
		LOGE(TAG, "跳过 LVGL 初始化: 事件总线未就绪");
	}
#endif

	/* ═══════════════════════════════════════════════════════════ */
	/*  Phase 5: SDK 应用框架启动                                  */
	/* ═══════════════════════════════════════════════════════════ */
	LOGI(TAG, "[Phase 5] SDK 框架启动");
	struct intent it;
	init_intent(&it);
	it.name = "d_app";
	it.action = ACTION_DO_NOTHING;
	start_app(&it);

#ifdef D_LVGL_ENABLE
	/* 接纳在第五阶段完成前到达的 UI 结果。 */
	(void)d_app_finalize_ui_result(0);
#endif
	d_app_log_startup_summary();
#ifdef D_LVGL_ENABLE
	OS_ENTER_CRITICAL();
	s_app_startup.is_initial_summary_reported = 1U;
	OS_EXIT_CRITICAL();
	/* 补查首次结果接纳与摘要发布之间到达的完成回调，避免遗漏终态。 */
	(void)d_app_finalize_ui_result(1);
#endif
}
