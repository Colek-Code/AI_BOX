/**
 * @file d_camera_page.c
 * @brief 相机模块 - 实现
 * @note  模块化设计，可独立开关
 *        全屏摄像头预览 + LVGL UI 叠加
 *        底部拍照按钮
 */

#include "config/app_config.h"

#if D_FEATURE_CAMERA

#include "d_camera_page.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include "lcd_data_driver.h"
#include "get_yuv_data.h"
#include "lvgl.h"
#include <string.h>

/* 拍照功能相关头文件 */
#include "asm/jpeg_codec.h"    /* JPEG编码API */
#include "fs/fs.h"             /* 文件操作 */
#include "platform/storage/d_storage.h" /* SD卡状态检查 */

#define LOG_TAG "[CAMERA_MODULE] "

//*********************************************************************************//
//                              布局常量                                            //
//*********************************************************************************//

#define SCR_W           320
#define SCR_H           480

#define BOTTOM_BAR_H    80
#define BUTTON_W        80
#define BUTTON_H        80

#define MAX_PHOTOS      10     /* 最大照片数量，用于索引扫描 */

//*********************************************************************************//
//                              内部状态                                            //
//*********************************************************************************//

static lv_obj_t *s_container = NULL;
static lv_obj_t *s_bottom_bar = NULL;
static lv_obj_t *s_capture_btn = NULL;
static int s_is_active = 0;
static int s_camera_initialized = 0;  /* 摄像头是否已启动 */
static int s_camera_page_type = 0;    /* 由模块系统分配 */

//*********************************************************************************//
//                              拍照功能状态                                         //
//*********************************************************************************//

static volatile int s_capture_request = 0;   /* 拍照请求标志：1=请求拍照 */
static int s_photo_index = -1;                /* 照片序号计数器，-1表示未初始化 */

//*********************************************************************************//
//                              照片索引初始化                                       //
//*********************************************************************************//

/**
 * @brief 扫描零至 MAX_PHOTOS 减一的大小写扩展名，优先取最大已有下标后一项。
 * @return 候选下标；扫描范围满且无空位时仍返回 MAX_PHOTOS，不保证它尚未存在。
 * @note 不扫描范围以上文件，连续满目录拍照可能重复同名；此函数并不提供无覆盖或原子文件名预留。
 */
static int find_next_photo_index(void)
{
    int max_index = -1;
    FILE *fp;
    char test_path[128];

    printf(LOG_TAG "Scanning existing photos to find next index...\n");

    /* 扫描所有MAX_PHOTOS位置 */
    for (int i = 0; i < MAX_PHOTOS; i++) {
        /* 先尝试大写扩展名 */
        snprintf(test_path, sizeof(test_path),
                 CONFIG_ROOT_PATH"PHOTO/IMG_%04d.JPG", i);

        fp = fopen(test_path, "rb");
        if (!fp) {
            /* 再尝试小写扩展名 */
            snprintf(test_path, sizeof(test_path),
                     CONFIG_ROOT_PATH"PHOTO/IMG_%04d.jpg", i);
            fp = fopen(test_path, "rb");
        }

        if (fp) {
            fclose(fp);
            if (i > max_index) {
                max_index = i;
            }
        }
    }

    int next_index = (max_index >= 0) ? (max_index + 1) : 0;

    /* 如果索引超出范围，从头寻找空位 */
    if (next_index >= MAX_PHOTOS) {
        for (int i = 0; i < MAX_PHOTOS; i++) {
            snprintf(test_path, sizeof(test_path),
                     CONFIG_ROOT_PATH"PHOTO/IMG_%04d.JPG", i);
            fp = fopen(test_path, "rb");
            if (!fp) {
                snprintf(test_path, sizeof(test_path),
                         CONFIG_ROOT_PATH"PHOTO/IMG_%04d.jpg", i);
                fp = fopen(test_path, "rb");
            }
            if (!fp) {
                next_index = i;
                break;
            } else {
                fclose(fp);
            }
        }
    }

    printf(LOG_TAG "Next photo index: %d (max existing: %d)\n", next_index, max_index);

    return next_index;
}

/**
 * @brief 尝试创建 PHOTO 目录并打印结果。
 * @return 始终返回 0，只表示已经尝试，不能据此认定目录成功存在。
 * @note 当前把目录已存在及其他创建错误统一记录，调用方仍依赖随后 fopen 的实际结果。
 */
static int ensure_photo_dir_exists(void)
{
    /* 使用 fmk_dir 创建目录（如果已存在会返回错误，忽略即可） */
    int ret = fmk_dir(CONFIG_ROOT_PATH, "/PHOTO", 0);
    if (ret == 0) {
        printf(LOG_TAG "Created PHOTO directory\n");
    } else {
        printf(LOG_TAG "PHOTO directory exists or error\n");
    }

    return 0;
}

//*********************************************************************************//
//                              拍照功能实现                                         //
//*********************************************************************************//

/**
 * @brief 在旧 YUV 回调路径同步编码 JPEG 并写入 SD，结束时关闭文件及释放局部编码缓冲。
 * @param[in] yuv_buf 本次回调借用的完整平面 YUV420 图像，调用方保证容量匹配宽高。
 * @param[in] width 正图像宽度。
 * @param[in] height 正图像高度。
 * @return 完整编码及写入为 0；参数 -1、SD 未就绪 -2、分配 -3、编码 -4、开文件 -5、短写 -6。
 * @note 执行同步编码及文件 IO，会阻塞本次数据回调；代码不提供固定耗时保证，也不保证目录满后文件名不会重复。失败可能留下部分目标文件。
 */
static int capture_save_photo(u8 *yuv_buf, int width, int height)
{
    int ret = 0;
    int jpg_len = 0;
    int yuv_size = 0;
    u8 *jpg_buf = NULL;
    FILE *fd = NULL;
    char file_path[128];

    printf(LOG_TAG "=== capture_save_photo START ===\n");
    printf(LOG_TAG "YUV buffer: %p, size: %dx%d\n", yuv_buf, width, height);

    /* 步骤1：参数检查 */
    if (!yuv_buf || width <= 0 || height <= 0) {
        printf(LOG_TAG "ERROR: Invalid parameters!\n");
        ret = -1;
        goto exit_cleanup;
    }

    /* 步骤2：检查SD卡是否就绪 */
    printf(LOG_TAG "Step 1: Checking SD card status...\n");
    if (d_storage_device_ready() < 0) {
        printf(LOG_TAG "ERROR: SD card not ready!\n");
        ret = -2;
        goto exit_cleanup;
    }
    printf(LOG_TAG "SD card is ready.\n");

    /* 步骤3：分配JPEG编码缓冲区 */
    printf(LOG_TAG "Step 2: Allocating JPEG buffer...\n");
    #define JPEG_BUF_SIZE (100 * 1024)  /* 100KB，足够640x480图片 */
    jpg_buf = (u8 *)malloc(JPEG_BUF_SIZE);
    if (!jpg_buf) {
        printf(LOG_TAG "ERROR: Failed to allocate JPEG buffer (%d bytes)!\n", JPEG_BUF_SIZE);
        ret = -3;
        goto exit_cleanup;
    }
    printf(LOG_TAG "JPEG buffer allocated: %d bytes at %p\n", JPEG_BUF_SIZE, jpg_buf);

    /* 步骤5：配置JPEG编码参数 */
    printf(LOG_TAG "Step 4: Configuring JPEG encoder...\n");
    yuv_size = width * height * 3 / 2;  /* YUV420大小 */
    printf(LOG_TAG "YUV420 data size: %d bytes\n", yuv_size);

    struct jpeg_encode_req enc_req = {0};
    enc_req.format = JPG_SAMP_FMT_YUV420;  /* YUV420格式 */
    enc_req.q = 5;                          /* 编码质量：0-13，5为中等质量 */
    enc_req.width = width;
    enc_req.height = height;
    enc_req.y = yuv_buf;                           /* Y分量地址 */
    enc_req.u = yuv_buf + width * height;          /* U分量地址 */
    enc_req.v = enc_req.u + width * height / 4;    /* V分量地址 */
    enc_req.data.buf = jpg_buf;                    /* JPEG输出缓冲区 */
    enc_req.data.len = JPEG_BUF_SIZE;              /* 缓冲区大小 */

    printf(LOG_TAG "JPEG params: format=YUV420, quality=%d, %dx%d\n",
           enc_req.q, enc_req.width, enc_req.height);

    /* 步骤6：执行JPEG编码 */
    printf(LOG_TAG "Step 5: Encoding JPEG...\n");
    ret = jpeg_encode_one_image(&enc_req);
    if (ret != 0) {
        printf(LOG_TAG "ERROR: JPEG encode failed! ret=%d\n", ret);
        ret = -4;
        goto exit_cleanup;
    }

    jpg_len = enc_req.data.len;  /* 编码后的实际长度 */
    printf(LOG_TAG "JPEG encode SUCCESS! size=%d bytes (%d KB)\n", jpg_len, jpg_len / 1024);

    /* 步骤7：生成文件名并保存 */
    printf(LOG_TAG "Step 6: Saving to SD card...\n");

    /* 每次重新扫描有限索引范围；范围已满时，不能据此保证不覆盖范围外的同名文件。 */
    s_photo_index = find_next_photo_index();

    /* 确保PHOTO目录存在 */
    ensure_photo_dir_exists();

    snprintf(file_path, sizeof(file_path),
             CONFIG_ROOT_PATH"PHOTO/IMG_%04d.JPG", s_photo_index);
    printf(LOG_TAG "Target file: %s\n", file_path);

    fd = fopen(file_path, "wb");
    if (!fd) {
        printf(LOG_TAG "ERROR: Failed to create file!\n");
        ret = -5;
        goto exit_cleanup;
    }

    ret = fwrite(jpg_buf, 1, jpg_len, fd);
    if (ret != jpg_len) {
        printf(LOG_TAG "ERROR: Write failed! expected=%d, actual=%d\n", jpg_len, ret);
        ret = -6;
        goto exit_cleanup;
    }

    fclose(fd);
    fd = NULL;

    /* 步骤8：更新照片计数器 */
    s_photo_index++;
    printf(LOG_TAG "Photo saved! Next index: %d\n", s_photo_index);

    ret = 0;  /* 成功 */

exit_cleanup:
    /* 清理资源 */
    printf(LOG_TAG "Step 7: Cleanup...\n");
    if (fd) {
        fclose(fd);
    }
    if (jpg_buf) {
        free(jpg_buf);
        printf(LOG_TAG "JPEG buffer freed.\n");
    }

    if (ret == 0) {
        printf(LOG_TAG "=== capture_save_photo SUCCESS ===\n");
    } else {
        printf(LOG_TAG "=== capture_save_photo FAILED (err=%d) ===\n", ret);
    }

    return ret;
}

//*********************************************************************************//
//                              YUV 数据回调                                        //
//*********************************************************************************//

/**
 * @brief 页面活跃时消费一次拍照意图，再把当前借用 YUV 交给叠加系统。
 * @param[in] yuv_buf SDK 本次回调借用的 YUV 缓冲。
 * @param[in] len SDK 传入的图像字节数。
 * @param[in] width 图像宽度。
 * @param[in] height 图像高度。
 * @note 清除拍照标志后才处理，避免重复触发；当前仅检查非空及正长度，完整 YUV 尺寸容量仍依赖上游合同。
 */
static void camera_yuv_callback(u8 *yuv_buf, u32 len, int width, int height)
{
    if (!s_is_active) return;

    /* 检测拍照请求 */
    if (s_capture_request) {
        printf(LOG_TAG ">>> Photo request detected in YUV callback <<<\n");
        printf(LOG_TAG "YUV info: buf=%p, len=%u, %dx%d\n", yuv_buf, len, width, height);

        /* 清除请求标志（在处理前清除，避免重复触发） */
        s_capture_request = 0;

        /* 执行拍照（在回调中直接处理，会短暂阻塞显示） */
        if (yuv_buf && len > 0) {
            capture_save_photo(yuv_buf, width, height);
        } else {
            printf(LOG_TAG "ERROR: Invalid YUV data, capture aborted!\n");
        }
    }

    /* 将 YUV 数据发送到叠加系统 */
    camera_send_data_ready(yuv_buf, len, width, height);
}

//*********************************************************************************//
//                              按钮事件回调                                        //
//*********************************************************************************//

/**
 * @brief 只设置单次拍照意图，编码与写入留给下一次活跃 YUV 回调。
 * @param[in] e 按钮事件，当前未读取载荷。
 */
static void on_capture_btn_click(lv_event_t *e)
{
    (void)e;

    printf(LOG_TAG "========================================\n");
    printf(LOG_TAG "Capture button clicked!\n");
    printf(LOG_TAG "========================================\n");

    /* 设置拍照请求标志，YUV回调中会检测并执行 */
    s_capture_request = 1;

    printf(LOG_TAG "Photo request set, waiting for next frame...\n");
}

//*********************************************************************************//
//                              页面生命周期回调                                    //
//*********************************************************************************//

/**
 * @brief 创建旧相机叠加页的底部拍照栏及按钮。
 * @param[in,out] parent 页面管理器拥有的父容器。
 * @return 父对象为空返回 -1，其余返回 0；当前未逐项报告子对象分配失败。
 */
static int camera_page_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return -1;
    }

    s_container = parent;

    /* 设置背景色（全黑，摄像头数据会覆盖） */
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* 底部操作栏 */
    s_bottom_bar = lv_obj_create(parent);
    lv_obj_set_size(s_bottom_bar, SCR_W, BOTTOM_BAR_H);
    lv_obj_set_pos(s_bottom_bar, 0, SCR_H - BOTTOM_BAR_H);
    lv_obj_set_style_bg_color(s_bottom_bar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_bottom_bar, LV_OPA_50, 0); /* 半透明背景 */
    lv_obj_set_style_border_width(s_bottom_bar, 0, 0);
    lv_obj_set_style_radius(s_bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(s_bottom_bar, 0, 0);
    lv_obj_remove_flag(s_bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* 拍照按钮（圆形按钮） */
    s_capture_btn = lv_btn_create(s_bottom_bar);
    lv_obj_set_size(s_capture_btn, BUTTON_W, BUTTON_H);
    lv_obj_center(s_capture_btn);
    lv_obj_set_style_bg_color(s_capture_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(s_capture_btn, BUTTON_W / 2, 0); /* 圆形 */
    lv_obj_set_style_shadow_width(s_capture_btn, 0, 0);
    lv_obj_set_style_border_width(s_capture_btn, 4, 0);
    lv_obj_set_style_border_color(s_capture_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(s_capture_btn, lv_color_hex(0xAAAAAA), LV_STATE_PRESSED);

    /* 内部小圆 */
    lv_obj_t *inner_circle = lv_obj_create(s_capture_btn);
    lv_obj_set_size(inner_circle, BUTTON_W - 16, BUTTON_H - 16);
    lv_obj_center(inner_circle);
    lv_obj_set_style_bg_color(inner_circle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(inner_circle, (BUTTON_W - 16) / 2, 0);
    lv_obj_set_style_border_width(inner_circle, 0, 0);
    lv_obj_set_style_pad_all(inner_circle, 0, 0);
    lv_obj_remove_flag(inner_circle, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(s_capture_btn, on_capture_btn_click, LV_EVENT_CLICKED, NULL);

    /* 提示文字 */
    lv_obj_t *hint_label = lv_label_create(s_bottom_bar);
    lv_label_set_text(hint_label, "Swipe up to go back");
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_12, 0);
    lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 0, 5);

    printf(LOG_TAG "Camera page created\n");
    return 0;
}

/**
 * @brief 标记页面活跃，必要时启动 YUV 回调并固定等待，再配置 UI_CAMERA 叠加模式。
 * @note 固定等待及本地 initialized 标志不是 SDK 异步完成或传感器成功证明。
 */
static void camera_page_enter(void)
{
    s_is_active = 1;
    printf(LOG_TAG "Camera page enter\n");

    /* 初始化摄像头（首次进入时） */
    if (!s_camera_initialized) {
        printf(LOG_TAG "First time init camera...\n");

        /* 先切换到 CAMERA 模式，让摄像头数据流启动 */
        set_lcd_show_data_mode(CAMERA);

        /* 初始化 YUV 数据回调，启动摄像头数据流 */
        get_yuv_init(camera_yuv_callback);

        /* 等待摄像头初始化完成（异步，需要等待几帧） */
        os_time_dly(20);  /* 等待 200ms */

        s_camera_initialized = 1;
        printf(LOG_TAG "Camera initialized\n");
    }

    /* 切换到 UI_CAMERA 模式（合成摄像头和UI数据）
     * UI_CAMERA模式: data_mode=UI_CAMERA, compose_status=1
     * 这样可以在摄像头画面上叠加底部按钮等UI元素
     */
    set_lcd_show_data_mode(UI_CAMERA);

    /* 设置合成模式（模式0：整图合成，透明色过滤） */
    set_compose_mode(0, 0, 0, 0);

    /* 通知页面管理器进入叠加模式 */
}

/**
 * @brief 先撤销页面活跃，调用外部 YUV uninit，再回到普通 UI 模式。
 * @note 外部 void uninit 的真实资源完成合同不由本页提供；不把本地标志清零扩展为独立硬件验收。
 */
static void camera_page_exit(void)
{
    s_is_active = 0;
    printf(LOG_TAG "Camera page exit - step 1: set active=0\n");

    /* 先停止摄像头数据流（停止 YUV 回调），避免在模式切换期间产生数据 */
    printf(LOG_TAG "Camera page exit - step 2: stopping camera\n");
    extern void get_yuv_uninit(void);
    get_yuv_uninit();
    s_camera_initialized = 0;  /* 标记摄像头已停止，下次进入需要重新初始化 */
    printf(LOG_TAG "Camera page exit - step 3: camera stopped\n");

    /* 切换回普通 UI 模式（这会清除 TE 任务的状态） */
    printf(LOG_TAG "Camera page exit - step 4: switching to UI mode\n");
    set_lcd_show_data_mode(UI);
    printf(LOG_TAG "Camera page exit - step 5: UI mode set\n");

    /* 通知页面管理器退出叠加模式 */
    printf(LOG_TAG "Camera page exit - step 6: exiting overlay mode\n");
    printf(LOG_TAG "Camera page exit - done\n");
}

/**
 * @brief 保留页面周期入口，相机数据由 YUV 回调推进。
 * @param[in] elapsed_ms 本轮间隔，当前未使用。
 */
static void camera_page_update(uint32_t elapsed_ms)
{
    (void)elapsed_ms;
    /* 摄像头数据由回调自动处理，这里不需要额外逻辑 */
}

//*********************************************************************************//
//                              模块注册                                            //
//*********************************************************************************//

/* 页面操作回调 */
static const page_ops_t s_camera_page_ops = {
    .create  = camera_page_create,
    .enter   = camera_page_enter,
    .exit    = camera_page_exit,
    .destroy = NULL,
    .update  = camera_page_update,
    .gesture = NULL,
    .flags   = 0,
};

/* 页面描述符 */
static page_desc_t s_camera_page_desc = {
    .page_type  = 0,  /* 将由模块系统分配 */
    .name       = "CameraPage",
    .ops        = &s_camera_page_ops,
    .container  = NULL,
    .is_created = 0,
    .is_active  = 0,
};

/**
 * @brief 保存模块分配的页面类型并注册静态页面描述。
 * @param[in] page_type 装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int camera_register_page(int page_type)
{
    s_camera_page_type = page_type;
    s_camera_page_desc.page_type = page_type;
    return page_manager_register(&s_camera_page_desc);
}

/**
 * @brief 启动时仅扫描有限照片索引，不启动相机数据流。
 * @return 完成扫描尝试后返回 D_OK，不代表 SD 或相机硬件已确认就绪。
 */
static d_err_t camera_module_init(void)
{
    /* 开机时不初始化摄像头，等待用户进入相机页面时再初始化 */
    printf(LOG_TAG "Camera module init (camera will be initialized on first use)\n");

    /* 初始化照片索引，扫描已有照片找到下一个可用索引 */
    if (s_photo_index < 0) {
        s_photo_index = find_next_photo_index();
    }

    return D_OK;
}

/* 使用自动注册宏定义模块 */
D_UI_MODULE_DEFINE(camera, "Camera", LV_SYMBOL_VIDEO, camera_register_page, camera_module_init);

/**
 * @brief 读取旧相机页本地初始化标志，具体证明边界见头文件。
 */
int camera_page_is_initialized(void)
{
    return s_camera_initialized;
}

#endif /* D_FEATURE_CAMERA */
