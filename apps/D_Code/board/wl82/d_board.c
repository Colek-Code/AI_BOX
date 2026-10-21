#include "config/app_config.h"
#include "system/includes.h"
#include "device/includes.h"
#include "device/iic.h"
#include "asm/includes.h"
#ifdef CONFIG_AUDIO_ENABLE
#include "server/audio_dev.h"
#endif
#ifdef CONFIG_VIDEO_ENABLE
#include "video_ioctl.h"
#include "video/video.h"
#endif
#if TCFG_USB_SLAVE_ENABLE || TCFG_USB_HOST_ENABLE
#include "otg.h"
#include "usb_host.h"
#include "usb_storage.h"
#endif
#ifdef D_LCD_ENABLE
#include "asm/emi.h"
#include "asm/spi.h"
#include "platform/screen/lcd/d_lcd_compat.h"
#endif

// *INDENT-OFF*

#if defined CONFIG_BT_ENABLE || defined CONFIG_WIFI_ENABLE
#include "wifi/wifi_connect.h"
const struct wifi_calibration_param wifi_calibration_param = {
	.xosc_l = 0xb,
	.xosc_r = 0xb,
	.pa_trim_data = {1, 7, 4, 7, 11, 1, 7},
	.mcs_dgain = {
		45, 45, 45, 42,
		60, 60, 75, 70, 62, 52, 50, 38,
		62, 80, 70, 62, 50, 48, 40, 36,
	},
};
#endif

UART2_PLATFORM_DATA_BEGIN(uart2_data)
	.baudrate = 1000000,
	.port = PORT_REMAP,
	.output_channel = OUTPUT_CHANNEL0,
	.tx_pin = IO_PORTB_03,
	.rx_pin = -1,
	.max_continue_recv_cnt = 1024,
	.idle_sys_clk_cnt = 500000,
	.clk_src = PLL_48M,
	.flags = UART_DEBUG,
UART2_PLATFORM_DATA_END();

#if defined(D_LCD_ENABLE) && defined(D_LCD_BACKLIGHT_PWM_ENABLE)
PWM_PLATFORM_DATA_BEGIN(pwm_lcd_bl_data)
#if (D_BOARD_IS_GC9307)
	.port = IO_PORTC_04, /* GC9307 active-high backlight enable / PWM */
#else
	.port = IO_PORTB_08, /* EMI ILI948x active-low backlight PWM */
#endif
	.pwm_ch = PWM_TIMER2_OPCH2,
	.freq = 20000,
#ifdef D_LCD_BL_ACTIVE_LOW
	.duty = 100,
#else
	.duty = 80, /* active-high: start near default product brightness */
#endif
	.point_bit = 1,
PWM_PLATFORM_DATA_END()
#endif


#if defined(D_LCD_ENABLE) && defined(D_LCD_EMI_ILI948X_ENABLE)
// EMI 接口配置（用于 LCD）
#define EMI_BAUD		EMI_BAUD_DIV8
static const struct emi_platform_data emi_data = {
    .bits_mode      = EMI_8BITS_MODE,
    .baudrate       = EMI_BAUD,
    .colection      = EMI_FALLING_COLT,
    .time_out       = 1 * 1000,
    .th             = EMI_TWIDTH_NO_HALF,
    .ts             = 0,
    .tw             = EMI_BAUD,
    .data_bit_en    = 0,
};
#endif

// LCD 平台数据配置
/* D++: Profile-gated LCD pdata keeps EMI/ILI948x panel and GC9307 LCD-SPI panel isolated. */
#if defined(D_LCD_ENABLE) && defined(D_LCD_GC9307_SDK_LCD_ENABLE)
SPI1_PLATFORM_DATA_BEGIN(spi1_data)
	.clk    = 80000000, /* D++: GC9307 LCD-SPI validated at 80MHz on SPI1 port B. */
	.mode   = SPI_1WIRE_MODE,
	.port   = 'B', /* D++: SPI1 B group maps LCD CLK/MOSI to PC9/PC10 per AC7916A datasheet. */
	.attr   = SPI_SCLK_L_UPL_SMPH | SPI_UNIDIR_MODE,
SPI1_PLATFORM_DATA_END()
#endif

#if D_BOARD_IS_GC9307
static const struct lcd_platform_data lcd_pdata_gc9307_hyn = {
    .rst_pin = IO_PORTC_07,
    .cs_pin = IO_PORTC_05,
    .rs_pin = IO_PORTC_06,
    .bl_pin = IO_PORTC_04, /* D++: GC9307 backlight enable is rewired from 3.3V to active-high PC4. */
    .te_pin = IO_PORTC_08,
    .check_pin_D6 = -1,
    .check_pin_D7 = -1,
    .touch_int_pin = IO_PORTC_00,
    .touch_reset_pin = IO_PORTC_03,
    .spi_id = "spi1",
    .spi_clk_pin = IO_PORTC_09,
    .spi_mosi_pin = IO_PORTC_10,
    .lcd_if  = LCD_SPI,
};
#endif

#if D_BOARD_IS_EMI_ILI948X
static const struct lcd_platform_data lcd_pdata_emi_ili948x = {
    .rst_pin = IO_PORTB_00,
    .cs_pin = -1,
    .rs_pin = IO_PORTC_09,
    .bl_pin = IO_PORTB_08,
    .te_pin = IO_PORTC_00,
    .check_pin_D6 = IO_PORTC_07,
    .check_pin_D7 = IO_PORTC_08,
    .touch_int_pin = IO_PORTB_04,
    .touch_reset_pin = IO_PORTB_00,
    .spi_id = NULL,
    .spi_clk_pin = -1,
    .spi_mosi_pin = -1,
    .lcd_if  = LCD_EMI,
};
#endif

#if D_BOARD_IS_GC9307 && !D_CAMERA_PREVIEW_ENABLE
/* Camera-off GC9307 keeps the original touch device name and physical pins. */
#define TCFG_IIC0_CLK			IO_PORTC_01
#define TCFG_IIC0_DAT			IO_PORTC_02
#else
#define TCFG_IIC0_CLK			IO_PORTH_00
#define TCFG_IIC0_DAT			IO_PORTH_01
#endif
SW_IIC_PLATFORM_DATA_BEGIN(sw_iic0_data)
	.clk_pin = TCFG_IIC0_CLK,
	.dat_pin = TCFG_IIC0_DAT,
    .sw_iic_delay = 50,
SW_IIC_PLATFORM_DATA_END()

#if D_CAMERA_PREVIEW_ENABLE
SW_IIC_PLATFORM_DATA_BEGIN(sw_iic1_data)
    .clk_pin = IO_PORTC_01,
    .dat_pin = IO_PORTC_02,
    .sw_iic_delay = 50,
SW_IIC_PLATFORM_DATA_END()

#if defined(CONFIG_AUDIO_ENABLE) && \
    (CONFIG_AUDIO_ENC_SAMPLE_SOURCE == AUDIO_ENC_SAMPLE_SOURCE_PLNK0)
#error "Camera XCLK/RESET PH2/PH3 conflict with PLNK0; keep analog MIC for Product preview"
#endif
#endif

#if TCFG_SD0_ENABLE
//sd0
#define SD_PLATFORM_DATA_BEGIN() SD0_PLATFORM_DATA_BEGIN(sd0_data)
#define SD_PLATFORM_DATA_END() SD0_PLATFORM_DATA_END()
#define SD_CLK_DETECT_FUNC		sdmmc_0_clk_detect
#elif TCFG_SD1_ENABLE
//sd1
#define SD_PLATFORM_DATA_BEGIN() SD1_PLATFORM_DATA_BEGIN(sd1_data)
#define SD_PLATFORM_DATA_END() SD1_PLATFORM_DATA_END()
#define SD_CLK_DETECT_FUNC		sdmmc_1_clk_detect
#endif

#if TCFG_SD0_ENABLE || TCFG_SD1_ENABLE

SD_PLATFORM_DATA_BEGIN()
	.port 					= TCFG_SD_PORTS,
	.priority 				= 3,
	.data_width 			= TCFG_SD_DAT_WIDTH,
	.speed 					= TCFG_SD_CLK,
	.detect_mode 			= TCFG_SD_DET_MODE,
#if (TCFG_SD_DET_MODE == SD_CLK_DECT)
	.detect_func 			= SD_CLK_DETECT_FUNC,
#elif (TCFG_SD_DET_MODE == SD_IO_DECT)
	.detect_func 			= sdmmc_0_io_detect,
#else
	.detect_func 			= NULL,
#endif
#if TCFG_SD_POWER_ENABLE
    .power                  = sd_set_power,
#endif
SD_PLATFORM_DATA_END()

#endif

#if TCFG_ADKEY_ENABLE

#define ADKEY_UPLOAD_R  22

#define ADC_VDDIO (0x3FF)
#define ADC_09   (0x3FF)
#define ADC_08   (0x3FF)
#define ADC_07   (0x3FF * 150 / (150 + ADKEY_UPLOAD_R))
#define ADC_06   (0x3FF * 62  / (62  + ADKEY_UPLOAD_R))
#define ADC_05   (0x3FF * 36  / (36  + ADKEY_UPLOAD_R))
#define ADC_04   (0x3FF * 22  / (22  + ADKEY_UPLOAD_R))
#define ADC_03   (0x3FF * 13  / (13  + ADKEY_UPLOAD_R))
#define ADC_02   (0x3FF * 75  / (75  + ADKEY_UPLOAD_R * 10))
#define ADC_01   (0x3FF * 3   / (3   + ADKEY_UPLOAD_R))
#define ADC_00   (0)

#define ADKEY_V_9      	((ADC_09 + ADC_VDDIO)/2)
#define ADKEY_V_8 		((ADC_08 + ADC_09)/2)
#define ADKEY_V_7 		((ADC_07 + ADC_08)/2)
#define ADKEY_V_6 		((ADC_06 + ADC_07)/2)
#define ADKEY_V_5 		((ADC_05 + ADC_06)/2)
#define ADKEY_V_4 		((ADC_04 + ADC_05)/2)
#define ADKEY_V_3 		((ADC_03 + ADC_04)/2)
#define ADKEY_V_2 		((ADC_02 + ADC_03)/2)
#define ADKEY_V_1 		((ADC_01 + ADC_02)/2)
#define ADKEY_V_0 		((ADC_00 + ADC_01)/2)

const struct adkey_platform_data adkey_data = {
    .enable     = 1,
	.adkey_pin  = IO_PORTB_01,
    .extern_up_en = 1,
	.ad_channel = 3,
    .ad_value = {
        ADKEY_V_0,
        ADKEY_V_1,
        ADKEY_V_2,
        ADKEY_V_3,
        ADKEY_V_4,
        ADKEY_V_5,
        ADKEY_V_6,
        ADKEY_V_7,
        ADKEY_V_8,
        ADKEY_V_9,
    },
    .key_value = {
        KEY_POWER,
        KEY_CANCLE,
        KEY_PHOTO,
        KEY_OK,
        KEY_VOLUME_INC,
        KEY_VOLUME_DEC,
        KEY_MODE,
        KEY_ENC,
        NO_KEY,
        NO_KEY,
    },
};

#ifdef D_BOARD_ADKEY_DEBUG_ENABLE
static u8 d_board_adkey_map_value(u16 ad_value)
{
	u8 idx;

	for (idx = 0; idx < ARRAY_SIZE(adkey_data.ad_value); idx++) {
		if ((ad_value <= adkey_data.ad_value[idx]) && (adkey_data.ad_value[idx] < 0x3ffL)) {
			return adkey_data.key_value[idx];
		}
	}

	return NO_KEY;
}

static u8 d_board_read_adkey_gpio_level(void)
{
	u8 gpio_level;

	gpio_latch_en(adkey_data.adkey_pin, 0);
	gpio_direction_input(adkey_data.adkey_pin);
	gpio_set_die(adkey_data.adkey_pin, 1);
	gpio_set_pull_down(adkey_data.adkey_pin, 0);
	if (adkey_data.extern_up_en) {
		gpio_set_pull_up(adkey_data.adkey_pin, 0);
	} else {
		gpio_set_pull_up(adkey_data.adkey_pin, 1);
	}
	delay_us(30);
	gpio_level = (u8)gpio_read(adkey_data.adkey_pin);
	gpio_set_die(adkey_data.adkey_pin, 0);
	return gpio_level;
}

static u16 d_board_read_adkey_raw_adc(void)
{
	u16 raw_ad_value;

	gpio_latch_en(adkey_data.adkey_pin, 0);
	gpio_direction_input(adkey_data.adkey_pin);
	gpio_set_die(adkey_data.adkey_pin, 0);
	gpio_set_pull_up(adkey_data.adkey_pin, 0);
	gpio_set_pull_down(adkey_data.adkey_pin, 0);
	delay_us(3000);
	JL_ADC->CON = (0xf << 12) | (adkey_data.ad_channel << 8) | (1 << 6) | (1 << 4) | (1 << 3) | (6 << 0);
	while (!(JL_ADC->CON & BIT(7)));
	raw_ad_value = (u16)JL_ADC->RES;
	JL_ADC->CON = 0;
	JL_ADC->CON = 0;
	JL_ADC->CON = 0;
	return raw_ad_value;
}

static void d_board_adkey_debug(void *priv)
{
	static u16 last_ad = 0xffff;
	static u8 last_key = NO_KEY;
	u16 ad_value = (u16)adc_get_value(adkey_data.ad_channel);
	u16 ad_diff = (last_ad > ad_value) ? (last_ad - ad_value) : (ad_value - last_ad);
	u8 key_value = d_board_adkey_map_value(ad_value);
	(void)priv;

	if ((last_ad != 0xffff) && (ad_diff < 12U) && (key_value == last_key)) {
		return;
	}

	last_ad = ad_value;
	last_key = key_value;
	printf("[BOARD_DEBUG] adkey adc=%u map=%u\n", (unsigned)ad_value, (unsigned)key_value);
}
#endif

static void d_board_prepare_adkey_pin(void)
{
	gpio_latch_en(adkey_data.adkey_pin, 0);
	gpio_direction_input(adkey_data.adkey_pin);
	gpio_set_die(adkey_data.adkey_pin, 0);
	gpio_set_pull_down(adkey_data.adkey_pin, 0);
	if (adkey_data.extern_up_en) {
		gpio_set_pull_up(adkey_data.adkey_pin, 0);
		return;
	}
	gpio_set_pull_up(adkey_data.adkey_pin, 1);
}

#endif

#ifdef CONFIG_AUDIO_ENABLE
static const struct dac_platform_data dac_data = {
    .sw_differ = 0,
    .pa_auto_mute = 1,
    .pa_mute_port = TCFG_DAC_MUTE_PORT,
    .pa_mute_value = TCFG_DAC_MUTE_VALUE,
    .differ_output = TCFG_DAC_DIFF_OUTPUT,
    .hw_channel = TCFG_DAC_HW_CHANNEL,
    .ch_num = TCFG_DAC_CH_NUM,
    .vcm_init_delay_ms = 1000,
    .mute_delay_ms = TCFG_DAC_MUTE_DELAY_MS,
#ifdef CONFIG_DEC_ANALOG_VOLUME_ENABLE
    .fade_enable = 1,
    .fade_delay_ms = 40,
#endif
#ifdef CONFIG_AEC_USE_PLAY_MUSIC_ENABLE
    .sr_points = 960,               //48kHz DAC每次中断的采样点数，与audio_irq_sr_points=320配合
#endif
};
static const struct adc_platform_data adc_data = {
    .mic_channel = LADC_CH_MIC1_P | LADC_CH_MIC3_P,
    .linein_channel = LADC_CH_AUX0 | LADC_CH_AUX2,
    .mic_ch_num = 2,
    .linein_ch_num = 2,
    .all_channel_open = 1,
    .isel = 2,
    .dump_num = 480,
};
static const struct iis_platform_data iis0_data = {
    .channel_in = BIT(1),
    .channel_out = BIT(0),
    .port_sel = IIS_PORTA,
    .data_width = 0,
    .mclk_output = 0,
    .slave_mode = 0,
    .dump_points_num = 320,
};
static void plnk0_port_remap_cb(void)
{
    extern int gpio_plnk_rx_input(u32 gpio, u8 index, u8 data_sel);
    gpio_plnk_rx_input(IO_PORTH_02, 0, 0);
    gpio_output_channle(IO_PORTH_03, CH0_PLNK0_SCLK_OUT);
    JL_IOMAP->CON3 |= BIT(18);
}
static void plnk0_port_unremap_cb(void)
{
    JL_IOMAP->CON3 &= ~BIT(18);
    gpio_clear_output_channle(IO_PORTH_03, CH0_PLNK0_SCLK_OUT);
    gpio_set_die(IO_PORTH_02, 0);
}
static const struct plnk_platform_data plnk0_data = {
    .hw_channel = PLNK_CH_MIC_L,
    .clk_out = 1,
    .port_remap_cb = plnk0_port_remap_cb,
    .port_unremap_cb = plnk0_port_unremap_cb,
    .sample_edge = 0,
    .share_data_io = 1,
    .high_gain = 1,
    .dc_cancelling_filter = 14,
    .dump_points_num = 640,
};
static const struct spdif_platform_data spdif_data = {
    .spdif_port = SPDIF_IN_PORT_D,
    .width = 16,
    .width_24_to_16 = 0,
};
static const struct audio_pf_data audio_pf_d = {
    .adc_pf_data = &adc_data,
    .dac_pf_data = &dac_data,
    .iis0_pf_data = &iis0_data,
    .plnk0_pf_data = &plnk0_data,
    .spdif_pf_data = &spdif_data,
};
static const struct audio_platform_data audio_data = {
    .private_data = (void *)&audio_pf_d,
};
#endif /* CONFIG_AUDIO_ENABLE */

//*********************************************************************************//
//                                摄像头模块配置                                  //
//*********************************************************************************//
#ifdef CONFIG_VIDEO_ENABLE
static const struct camera_platform_data camera0_data = {
    .xclk_gpio      = IO_PORTH_02,
    .reset_gpio     = IO_PORTH_03,
    .online_detect  = NULL,
    .pwdn_gpio      = -1,
    .power_value    = 0,
    .interface      = SEN_INTERFACE0,
    .dvp = {
        .pclk_gpio   = IO_PORTA_08,
        .hsync_gpio  = IO_PORTA_09,
        .vsync_gpio  = IO_PORTA_10,
        .group_port  = ISC_GROUPA,
        .data_gpio   = {
            IO_PORTA_07,
            IO_PORTA_06,
            IO_PORTA_05,
            IO_PORTA_04,
            IO_PORTA_03,
            IO_PORTA_02,
            IO_PORTA_01,
            IO_PORTA_00,
            -1,
            -1,
        },
    }
};

static const struct video_subdevice_data video0_subdev_data[] = {
    { VIDEO_TAG_CAMERA, (void *)&camera0_data },
};

static const struct video_platform_data video0_data = {
    .data = video0_subdev_data,
    .num = ARRAY_SIZE(video0_subdev_data),
};
#endif
//**********************************END*******************************************//

#if TCFG_USB_SLAVE_ENABLE || TCFG_USB_HOST_ENABLE
static const struct otg_dev_data otg_data = {
    .usb_dev_en = 0x03,
#if TCFG_USB_SLAVE_ENABLE
    .slave_online_cnt = 10,
    .slave_offline_cnt = 10,
#endif
#if TCFG_USB_HOST_ENABLE
    .host_online_cnt = 10,
    .host_offline_cnt = 10,
#endif
    .detect_mode = OTG_HOST_MODE | OTG_SLAVE_MODE | OTG_CHARGE_MODE,
    .detect_time_interval = 50,
};
#endif

const struct rtc_init_config rtc_init_data = {
    .rtc_clk_sel = RTC_CLK_SEL_INSIDE,
    .rtc_power_sel = RTCVDD_SUPPLY_OUTSIDE,
};

static const struct low_power_param power_param = {
	.config         = TCFG_LOWPOWER_LOWPOWER_SEL,
	.btosc_disable  = TCFG_LOWPOWER_BTOSC_DISABLE,
	.vddiom_lev     = TCFG_LOWPOWER_VDDIOM_LEVEL,
	.vddiow_lev     = TCFG_LOWPOWER_VDDIOW_LEVEL,
	.vdc14_dcdc     = TRUE,
	.vdc14_lev      = VDC14_VOL_SEL_LEVEL,
	.sysvdd_lev     = SYSVDD_VOL_SEL_LEVEL,
	.vlvd_enable    = TRUE,
	.vlvd_value     = VLVD_SEL_25V,
};

/*
 * Soft poweroff wakeup: PB1 is the ADKEY/power key shared pin.
 * The EMI board keeps the legacy runtime wakeup path. Other boards expose the
 * shared pin only when the power-control service is enabled.
 */
#if D_BOARD_IS_EMI_ILI948X || \
    (D_FEATURE_POWER_CONTROL)
#define PORT_WAKEUP_IO			IO_PORTB_01
#define PORT_WAKEUP_NUM			(PORT_WAKEUP_IO / IO_GROUP_NUM)

static const struct port_wakeup soft_poweroff_port0 = {
    .edge       = FALLING_EDGE,
    .attribute  = BLUETOOTH_RESUME,
    .iomap      = PORT_WAKEUP_IO,
    .low_power	= POWER_SLEEP_WAKEUP | POWER_OFF_WAKEUP,
};

static const struct long_press lpres_poweroff_port = {
    .enable = TRUE,
    .use_sec4 = TRUE,
    .edge = FALLING_EDGE,
    .iomap = PORT_WAKEUP_IO,
};

static const struct sub_wakeup sub_wkup = {
    .attribute  = BLUETOOTH_RESUME,
};

static const struct charge_wakeup charge_wkup = {
    .attribute  = BLUETOOTH_RESUME,
};

static const struct wakeup_param soft_poweroff_wk_param = {
    .port[PORT_WAKEUP_NUM] = &soft_poweroff_port0,
    .sub = &sub_wkup,
    .charge = &charge_wkup,
    .lpres = &lpres_poweroff_port,
};

#if D_BOARD_IS_EMI_ILI948X
static const struct port_wakeup runtime_port0 = {
    .edge       = FALLING_EDGE,
    .attribute  = BLUETOOTH_RESUME,
    .iomap      = PORT_WAKEUP_IO,
    .low_power	= POWER_SLEEP_WAKEUP | POWER_OFF_WAKEUP,
};

static const struct long_press lpres_runtime_port = {
    .enable = FALSE,
    .use_sec4 = TRUE,
    .edge = FALLING_EDGE,
    .iomap = PORT_WAKEUP_IO,
};

static const struct wakeup_param runtime_wk_param = {
    .port[PORT_WAKEUP_NUM] = &runtime_port0,
    .sub = &sub_wkup,
    .charge = &charge_wkup,
    .lpres = &lpres_runtime_port,
};
#endif

static void d_board_apply_wakeup_param(const struct wakeup_param *wakeup_param)
{
    if (wakeup_param == NULL) {
        return;
    }

    power_wakeup_init(wakeup_param);
    {
        extern void clean_wakeup_source_port(void);
        clean_wakeup_source_port();
    }
}

void d_board_prepare_soft_poweroff_wakeup(void)
{
    d_board_apply_wakeup_param(&soft_poweroff_wk_param);
}
#else
void d_board_prepare_soft_poweroff_wakeup(void)
{
}
#endif /* !MINIMAL || POWER_CTRL */

REGISTER_DEVICES(device_table) = {
	{"uart2", &uart_dev_ops, (void *)&uart2_data},
	{"iic0",  &iic_dev_ops, (void *)&sw_iic0_data },
#if D_CAMERA_PREVIEW_ENABLE
    {"iic1", &iic_dev_ops, (void *)&sw_iic1_data},
#endif
#if defined(D_LCD_ENABLE) && defined(D_LCD_BACKLIGHT_PWM_ENABLE)
	{"pwm_lcd_bl", &pwm_dev_ops, (void *)&pwm_lcd_bl_data},
#endif
#if defined(D_LCD_ENABLE) && defined(D_LCD_EMI_ILI948X_ENABLE)
	{ "emi",   &emi_dev_ops, (void *)&emi_data },
#endif
#if defined(D_LCD_ENABLE) && defined(D_LCD_GC9307_SDK_LCD_ENABLE)
	{ "spi1", &spi_dev_ops, (void *)&spi1_data },
#endif
#ifdef CONFIG_AUDIO_ENABLE
	{"audio", &audio_dev_ops, (void *)&audio_data },
#endif
#ifdef CONFIG_VIDEO_ENABLE
	{ "video0.*",  &video_dev_ops, (void *)&video0_data },
#endif
#if TCFG_SD0_ENABLE
	{ "sd0",  &sd_dev_ops, (void *)&sd0_data },
#endif
#if TCFG_SD1_ENABLE
	{ "sd1",  &sd_dev_ops, (void *)&sd1_data },
#endif
#if TCFG_USB_SLAVE_ENABLE || TCFG_USB_HOST_ENABLE
	{ "otg", &usb_dev_ops, (void *)&otg_data},
#endif
#if TCFG_UDISK_ENABLE
    { "udisk0", &mass_storage_ops, NULL },
    { "udisk1", &mass_storage_ops, NULL },
#endif
	{"rtc", &rtc_dev_ops, NULL},
};

#ifdef CONFIG_DEBUG_ENABLE
void debug_uart_init()
{
	uart_init(&uart2_data);
}
#endif

void board_early_init()
{
	/* D++: Board bring-up logs are opt-in so normal LCD validation starts clean. */
#if D_DEBUG_BOARD_LOG
	printf("[BOARD_DEBUG] board_early_init start\n");
#endif
#ifdef CONFIG_AUDIO_ENABLE
	dac_early_init(0, dac_data.differ_output ? (dac_data.ch_num > 1 ? 0xf : 0x3): dac_data.hw_channel, 1000);
#endif
#if D_DEBUG_BOARD_LOG
	printf("[BOARD_DEBUG] calling devices_init\n");
#endif
	devices_init();
#if D_DEBUG_BOARD_LOG
	printf("[BOARD_DEBUG] devices_init done\n");
#endif
}
static void board_power_init(void)
{
	power_init(&power_param);
	power_keep_state(POWER_KEEP_RESET);
#if D_BOARD_IS_EMI_ILI948X
	d_board_apply_wakeup_param(&runtime_wk_param);
#endif
}

void board_init()
{
	board_power_init();
	rtc_early_init();
#if TCFG_ADKEY_ENABLE
	d_board_prepare_adkey_pin();
#endif
#if TCFG_ADKEY_ENABLE || (D_UI_TARGET == D_UI_PRODUCT)
	adc_init();
#endif
#if TCFG_ADKEY_ENABLE
	/* D++: Keep ADKEY diagnostics opt-in; the key driver still owns regular sampling. */
#ifdef D_BOARD_ADKEY_DEBUG_ENABLE
	printf("[BOARD_DEBUG] adkey sample idx=%u\n", (unsigned)adc_add_sample_ch(adkey_data.ad_channel));
#else
	adc_add_sample_ch(adkey_data.ad_channel);
#endif
	key_driver_init();
#ifdef D_BOARD_ADKEY_DEBUG_ENABLE
	printf("[BOARD_DEBUG] adkey init done\n");
	sys_timer_add(NULL, d_board_adkey_debug, 200);
#endif
#endif
}

#ifdef D_LCD_ENABLE
// 获取 LCD 平台数据
const struct lcd_platform_data* d_get_lcd_pdata(void)
{
#if D_BOARD_IS_GC9307
	return &lcd_pdata_gc9307_hyn;
#else
	return &lcd_pdata_emi_ili948x;
#endif
}
#endif
