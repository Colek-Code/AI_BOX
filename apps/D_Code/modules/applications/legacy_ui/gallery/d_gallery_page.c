/**
 * @file d_gallery_page.c
 * @brief 相册模块 - 实现
 * @note  预览SD卡PHOTO目录中的照片
 *        左右滑动切换图片，上滑返回主页面
 *        底部删除按钮
 */

#include "config/app_config.h"

#if D_FEATURE_GALLERY

#include "d_gallery_page.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include "lvgl.h"
#include <string.h>

/* JPEG解码相关头文件 */
#include "asm/jpeg_codec.h"
#include "fs/fs.h"
#include "platform/storage/d_storage.h"

#define LOG_TAG "[GALLERY] "

//*********************************************************************************//
//                              配置常量                                            //
//*********************************************************************************//

#define SCR_W               320
#define SCR_H               480

/* 照片最大分辨率（支持640x480或480x640） */
#define MAX_PHOTO_W         640
#define MAX_PHOTO_H         640

#define MAX_PHOTOS          10
#define PHOTO_DIR           CONFIG_ROOT_PATH "PHOTO"

//*********************************************************************************//
//                              内部状态                                            //
//*********************************************************************************//

typedef struct {
    char path[128];
} photo_info_t;

static struct {
    photo_info_t photos[MAX_PHOTOS];
    int count;
    int current;
    lv_obj_t *canvas;
    lv_obj_t *label_index;
    lv_obj_t *btn_delete;
    lv_obj_t *no_photo_label;
    u8 *rgb_buf;
    u8 *yuv_buf;
    int is_active;
    int page_type;
    volatile int delete_in_progress;
} s_gallery = {0};

//*********************************************************************************//
//                              前向声明                                            //
//*********************************************************************************//

static void gallery_show_image(int index);

//*********************************************************************************//
//                              文件扫描                                            //
//*********************************************************************************//

/**
 * @brief 扫描零至九的 JPG／jpg 文件，复制有限照片路径表并更新数量。
 * @return 找到的照片数。
 * @note 不扫描 IMG_0010 及更高编号；当前存入路径固定使用大写扩展名，即使存在性检查由小写文件命中。
 */
static int gallery_scan_photos(void)
{
    int count = 0;
    FILE *fp;
    char test_path[128];

    printf(LOG_TAG "Scanning: %s\n", PHOTO_DIR);

    memset(s_gallery.photos, 0, sizeof(s_gallery.photos));

    for (int i = 0; i < MAX_PHOTOS; i++) {
        snprintf(test_path, sizeof(test_path), "%s/IMG_%04d.JPG", PHOTO_DIR, i);
        fp = fopen(test_path, "rb");
        if (!fp) {
            snprintf(test_path, sizeof(test_path), "%s/IMG_%04d.jpg", PHOTO_DIR, i);
            fp = fopen(test_path, "rb");
        }

        if (fp) {
            fclose(fp);
            snprintf(s_gallery.photos[count].path, sizeof(s_gallery.photos[count].path),
                     "%s/IMG_%04d.JPG", PHOTO_DIR, i);
            count++;
        }
    }

    s_gallery.count = count;
    printf(LOG_TAG "Found %d photos\n", count);

    return count;
}

//*********************************************************************************//
//                              YUV 到 RGB 转换                                     //
//*********************************************************************************//

/**
 * @brief 把完整平面 YUV420 用整数最近邻映射到 320×480 RGB565。
 * @param[in] y_buf 完整源亮度平面。
 * @param[in] u_buf 完整源 U 平面，420 采样布局。
 * @param[in] v_buf 完整源 V 平面，420 采样布局。
 * @param[out] rgb_buf 至少 SCR_W×SCR_H 个 u16 像素的输出。
 * @param[in] src_w 调用方保证正的源宽度。
 * @param[in] src_h 调用方保证正的源高度。
 * @note 函数不单独校验源／目标容量，调用方必须提供与格式匹配的完整平面。
 */
static void yuv420_to_rgb565_scale(u8 *y_buf, u8 *u_buf, u8 *v_buf,
                                    u16 *rgb_buf, int src_w, int src_h)
{
    printf(LOG_TAG "YUV420->RGB565: %dx%d -> %dx%d\n", src_w, src_h, SCR_W, SCR_H);

    for (int dst_y = 0; dst_y < SCR_H; dst_y++) {
        for (int dst_x = 0; dst_x < SCR_W; dst_x++) {
            int src_x = (dst_x * src_w) / SCR_W;
            int src_y = (dst_y * src_h) / SCR_H;

            if (src_x >= src_w) src_x = src_w - 1;
            if (src_y >= src_h) src_y = src_h - 1;

            u8 y = y_buf[src_y * src_w + src_x];

            int uv_x = src_x / 2;
            int uv_y = src_y / 2;
            int uv_stride = (src_w + 1) / 2;

            u8 u = u_buf[uv_y * uv_stride + uv_x];
            u8 v = v_buf[uv_y * uv_stride + uv_x];

            int y_val = y;
            int u_val = u - 128;
            int v_val = v - 128;

            int r = y_val + ((1403 * v_val) >> 10);
            int g = y_val - ((344 * u_val + 714 * v_val) >> 10);
            int b = y_val + ((1772 * u_val) >> 10);

            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);

            rgb_buf[dst_y * SCR_W + dst_x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }
}

/**
 * @brief 把完整平面 YUV420 顺时针旋转九十度并用整数最近邻映射到 320×480 RGB565。
 * @param[in] y_buf 完整源亮度平面。
 * @param[in] u_buf 完整源 U 平面，420 采样布局。
 * @param[in] v_buf 完整源 V 平面，420 采样布局。
 * @param[out] rgb_buf 至少 SCR_W×SCR_H 个 u16 像素的输出。
 * @param[in] src_w 调用方保证正的源宽度。
 * @param[in] src_h 调用方保证正的源高度。
 * @note 函数不单独校验源／目标容量，调用方必须提供与格式匹配的完整平面。
 */
static void yuv420_to_rgb565_rotate90(u8 *y_buf, u8 *u_buf, u8 *v_buf,
                                       u16 *rgb_buf, int src_w, int src_h)
{
    /* 旋转后的尺寸：宽=原高，高=原宽 */
    int rot_w = src_h;  /* 旋转后的宽度 = 原高度 */
    int rot_h = src_w;  /* 旋转后的高度 = 原宽度 */

    printf(LOG_TAG "YUV420->RGB565 rotate90: %dx%d -> (%dx%d) -> %dx%d\n",
           src_w, src_h, rot_w, rot_h, SCR_W, SCR_H);

    /* UV采样的stride */
    int uv_stride = (src_w + 1) / 2;

    for (int dst_y = 0; dst_y < SCR_H; dst_y++) {
        for (int dst_x = 0; dst_x < SCR_W; dst_x++) {
            /* 使用整数除法映射到旋转后的最近邻坐标。 */
            int rot_x = (dst_x * rot_w) / SCR_W;
            int rot_y = (dst_y * rot_h) / SCR_H;

            /* 边界保护 */
            if (rot_x >= rot_w) rot_x = rot_w - 1;
            if (rot_y >= rot_h) rot_y = rot_h - 1;

            /* 顺时针旋转90度: (rot_x, rot_y) -> (src_x, src_y)
             * 旋转公式: src_x = rot_y, src_y = rot_w - 1 - rot_x
             * 其中 rot_w = src_h
             */
            int src_x = rot_y;
            int src_y = rot_w - 1 - rot_x;

            /* 再次边界保护 */
            if (src_x < 0) src_x = 0;
            if (src_x >= src_w) src_x = src_w - 1;
            if (src_y < 0) src_y = 0;
            if (src_y >= src_h) src_y = src_h - 1;

            /* Y: 全分辨率 */
            u8 y = y_buf[src_y * src_w + src_x];

            /* UV: 1/2采样 */
            int uv_x = src_x / 2;
            int uv_y = src_y / 2;

            u8 u = u_buf[uv_y * uv_stride + uv_x];
            u8 v = v_buf[uv_y * uv_stride + uv_x];

            /* YUV -> RGB */
            int y_val = y;
            int u_val = u - 128;
            int v_val = v - 128;

            int r = y_val + ((1403 * v_val) >> 10);
            int g = y_val - ((344 * u_val + 714 * v_val) >> 10);
            int b = y_val + ((1772 * u_val) >> 10);

            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);

            rgb_buf[dst_y * SCR_W + dst_x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }
}

/**
 * @brief 把完整平面 YUV422 用整数最近邻映射到 320×480 RGB565。
 * @param[in] y_buf 完整源亮度平面。
 * @param[in] u_buf 完整源 U 平面，422 采样布局。
 * @param[in] v_buf 完整源 V 平面，422 采样布局。
 * @param[out] rgb_buf 至少 SCR_W×SCR_H 个 u16 像素的输出。
 * @param[in] src_w 调用方保证正的源宽度。
 * @param[in] src_h 调用方保证正的源高度。
 * @note 函数不单独校验源／目标容量，调用方必须提供与格式匹配的完整平面。
 */
static void yuv422_to_rgb565_scale(u8 *y_buf, u8 *u_buf, u8 *v_buf,
                                    u16 *rgb_buf, int src_w, int src_h)
{
    printf(LOG_TAG "YUV422->RGB565: %dx%d -> %dx%d\n", src_w, src_h, SCR_W, SCR_H);

    for (int dst_y = 0; dst_y < SCR_H; dst_y++) {
        for (int dst_x = 0; dst_x < SCR_W; dst_x++) {
            int src_x = (dst_x * src_w) / SCR_W;
            int src_y = (dst_y * src_h) / SCR_H;

            if (src_x >= src_w) src_x = src_w - 1;
            if (src_y >= src_h) src_y = src_h - 1;

            u8 y = y_buf[src_y * src_w + src_x];

            int uv_x = src_x / 2;
            int uv_stride = (src_w + 1) / 2;

            u8 u = u_buf[src_y * uv_stride + uv_x];
            u8 v = v_buf[src_y * uv_stride + uv_x];

            int y_val = y;
            int u_val = u - 128;
            int v_val = v - 128;

            int r = y_val + ((1403 * v_val) >> 10);
            int g = y_val - ((344 * u_val + 714 * v_val) >> 10);
            int b = y_val + ((1772 * u_val) >> 10);

            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);

            rgb_buf[dst_y * SCR_W + dst_x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }
}

/**
 * @brief 把完整平面 YUV422 顺时针旋转九十度并用整数最近邻映射到 320×480 RGB565。
 * @param[in] y_buf 完整源亮度平面。
 * @param[in] u_buf 完整源 U 平面，422 采样布局。
 * @param[in] v_buf 完整源 V 平面，422 采样布局。
 * @param[out] rgb_buf 至少 SCR_W×SCR_H 个 u16 像素的输出。
 * @param[in] src_w 调用方保证正的源宽度。
 * @param[in] src_h 调用方保证正的源高度。
 * @note 函数不单独校验源／目标容量，调用方必须提供与格式匹配的完整平面。
 */
static void yuv422_to_rgb565_rotate90(u8 *y_buf, u8 *u_buf, u8 *v_buf,
                                       u16 *rgb_buf, int src_w, int src_h)
{
    int rot_w = src_h;
    int rot_h = src_w;
    int uv_stride = (src_w + 1) / 2;

    printf(LOG_TAG "YUV422->RGB565 rotate90: %dx%d -> (%dx%d) -> %dx%d\n",
           src_w, src_h, rot_w, rot_h, SCR_W, SCR_H);

    for (int dst_y = 0; dst_y < SCR_H; dst_y++) {
        for (int dst_x = 0; dst_x < SCR_W; dst_x++) {
            int rot_x = (dst_x * rot_w) / SCR_W;
            int rot_y = (dst_y * rot_h) / SCR_H;

            if (rot_x >= rot_w) rot_x = rot_w - 1;
            if (rot_y >= rot_h) rot_y = rot_h - 1;

            int src_x = rot_y;
            int src_y = rot_w - 1 - rot_x;

            if (src_x < 0) src_x = 0;
            if (src_x >= src_w) src_x = src_w - 1;
            if (src_y < 0) src_y = 0;
            if (src_y >= src_h) src_y = src_h - 1;

            u8 y = y_buf[src_y * src_w + src_x];

            int uv_x = src_x / 2;

            u8 u = u_buf[src_y * uv_stride + uv_x];
            u8 v = v_buf[src_y * uv_stride + uv_x];

            int y_val = y;
            int u_val = u - 128;
            int v_val = v - 128;

            int r = y_val + ((1403 * v_val) >> 10);
            int g = y_val - ((344 * u_val + 714 * v_val) >> 10);
            int b = y_val + ((1772 * u_val) >> 10);

            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);

            rgb_buf[dst_y * SCR_W + dst_x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
    }
}

//*********************************************************************************//
//                              图片解码                                            //
//*********************************************************************************//

/**
 * @brief 同步读取有限 JPEG，按原采样格式解码到模块 YUV，再缩放／旋转到全屏 RGB565。
 * @param[in] path 本次借用的完整照片路径。
 * @return 成功为 0；开文件 -1、文件长度 -2、分配 -3、短读 -4、信息 -5、尺寸 -6、解码 -7。
 * @note 临时 JPEG 缓冲在返回前释放，RGB／YUV 由页面模块持有。现有 YUV 分配只按 640×640 的420上界，保留原格式的422／444可能超容量，当前未按格式复核；444后续也沿用422转换，不能宣称全采样格式安全支持。
 */
static int gallery_decode_jpeg(const char *path)
{
    FILE *fp = NULL;
    u8 *jpg_buf = NULL;
    int file_size;
    int ret;
    struct jpeg_image_info img_info = {0};
    struct jpeg_decode_req dec_req = {0};
    int img_w, img_h, yuv_type;
    u8 *y_buf, *u_buf, *v_buf;

    printf(LOG_TAG "=== Decode: %s ===\n", path);

    /* 打开文件 */
    fp = fopen(path, "rb");
    if (!fp) {
        printf(LOG_TAG "ERROR: Cannot open\n");
        return -1;
    }

    /* 获取文件大小 */
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 500 * 1024) {
        printf(LOG_TAG "ERROR: Invalid size %d\n", file_size);
        fclose(fp);
        return -2;
    }

    /* 分配并读取JPEG数据 */
    jpg_buf = (u8 *)malloc(file_size);
    if (!jpg_buf) {
        fclose(fp);
        return -3;
    }

    ret = fread(jpg_buf, 1, file_size, fp);
    fclose(fp);
    fp = NULL;

    if (ret != file_size) {
        free(jpg_buf);
        return -4;
    }

    /* 获取图片信息 */
    img_info.input.data.buf = jpg_buf;
    img_info.input.data.len = file_size;
    ret = jpeg_decode_image_info(&img_info);
    if (ret != 0) {
        printf(LOG_TAG "ERROR: get info failed %d\n", ret);
        free(jpg_buf);
        return -5;
    }

    img_w = img_info.width;
    img_h = img_info.height;

    /* 根据采样格式确定YUV类型 */
    switch (img_info.sample_fmt) {
    case JPG_SAMP_FMT_YUV444:
        yuv_type = 1;
        break;
    case JPG_SAMP_FMT_YUV420:
        yuv_type = 4;
        break;
    case JPG_SAMP_FMT_YUV422:
    default:
        yuv_type = 2;
        break;
    }

    printf(LOG_TAG "Image: %dx%d, fmt=%d, yuv_type=%d\n", img_w, img_h, img_info.sample_fmt, yuv_type);

    /* 检查尺寸 */
    if (img_w > MAX_PHOTO_W || img_h > MAX_PHOTO_H) {
        printf(LOG_TAG "ERROR: Too large\n");
        free(jpg_buf);
        return -6;
    }

    /* 设置YUV缓冲区指针 */
    int pix = img_w * img_h;
    y_buf = s_gallery.yuv_buf;
    u_buf = y_buf + pix;
    v_buf = u_buf + pix / yuv_type;  /* 色度平面容量：YUV420 为亮度的四分之一，422 为二分之一，444 与亮度相同。 */

    /* 配置解码参数 - 使用DEFAULT保持原始格式 */
    memset(&dec_req, 0, sizeof(dec_req));
    dec_req.input_type = JPEG_INPUT_TYPE_DATA;
    dec_req.output_type = JPEG_DECODE_TYPE_DEFAULT;  /* 保持原始格式 */
    dec_req.input.data.buf = jpg_buf;
    dec_req.input.data.len = file_size;
    dec_req.buf_y = y_buf;
    dec_req.buf_u = u_buf;
    dec_req.buf_v = v_buf;
    dec_req.buf_width = img_w;
    dec_req.buf_height = img_h;
    dec_req.out_width = img_w;
    dec_req.out_height = img_h;
    dec_req.bits_mode = BITS_MODE_UNCACHE;
    dec_req.dec_query_mode = FALSE;

    /* 执行解码 */
    printf(LOG_TAG "Decoding...\n");
    ret = jpeg_decode_one_image(&dec_req);
    if (ret != 0) {
        printf(LOG_TAG "ERROR: decode failed %d\n", ret);
        free(jpg_buf);
        return -7;
    }

    free(jpg_buf);
    printf(LOG_TAG "Decode OK\n");

    /* 判断是否需要旋转：
     * 屏幕是竖屏(320x480)，如果图片是横屏(宽>高)，需要旋转90度
     */
    int need_rotate = (img_w > img_h) ? 1 : 0;
    printf(LOG_TAG "Need rotate: %s (img %dx%d, screen %dx%d)\n",
           need_rotate ? "YES" : "NO", img_w, img_h, SCR_W, SCR_H);

    /* YUV -> RGB565 转换 */
    if (yuv_type == 4) {  /* YUV420 */
        if (need_rotate) {
            yuv420_to_rgb565_rotate90(y_buf, u_buf, v_buf, (u16 *)s_gallery.rgb_buf, img_w, img_h);
        } else {
            yuv420_to_rgb565_scale(y_buf, u_buf, v_buf, (u16 *)s_gallery.rgb_buf, img_w, img_h);
        }
    } else {  /* 此分支沿用422转换，输入也可能是444；格式处理限制见函数合同。 */
        if (need_rotate) {
            yuv422_to_rgb565_rotate90(y_buf, u_buf, v_buf, (u16 *)s_gallery.rgb_buf, img_w, img_h);
        } else {
            yuv422_to_rgb565_scale(y_buf, u_buf, v_buf, (u16 *)s_gallery.rgb_buf, img_w, img_h);
        }
    }

    return 0;
}

//*********************************************************************************//
//                              UI 辅助函数                                         //
//*********************************************************************************//

/**
 * @brief 按当前照片计数更新一基索引，空表显示零／零。
 */
static void update_index_label(void)
{
    if (!s_gallery.label_index) return;

    char text[32];
    if (s_gallery.count > 0) {
        snprintf(text, sizeof(text), "%d/%d", s_gallery.current + 1, s_gallery.count);
    } else {
        snprintf(text, sizeof(text), "0/0");
    }
    lv_label_set_text(s_gallery.label_index, text);
}

/**
 * @brief 删除旧空提示、清空现有画布并创建无照片说明，再更新索引。
 */
static void show_no_photo_hint(void)
{
    if (s_gallery.no_photo_label) {
        lv_obj_delete(s_gallery.no_photo_label);
        s_gallery.no_photo_label = NULL;
    }

    if (s_gallery.canvas && s_gallery.rgb_buf) {
        memset(s_gallery.rgb_buf, 0, SCR_W * SCR_H * 2);
        lv_canvas_set_buffer(s_gallery.canvas, s_gallery.rgb_buf, SCR_W, SCR_H, LV_COLOR_FORMAT_RGB565);
    }

    if (s_gallery.canvas) {
        lv_obj_t *parent = lv_obj_get_parent(s_gallery.canvas);
        if (parent) {
            s_gallery.no_photo_label = lv_label_create(parent);
            lv_label_set_text(s_gallery.no_photo_label, "No photos\n\nTake some photos first!");
            lv_obj_set_style_text_color(s_gallery.no_photo_label, lv_color_hex(0x888888), 0);
            lv_obj_set_style_text_font(s_gallery.no_photo_label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_align(s_gallery.no_photo_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(s_gallery.no_photo_label);
        }
    }

    update_index_label();
}

/**
 * @brief 删除已存在的空照片说明并清空其引用。
 */
static void hide_no_photo_hint(void)
{
    if (s_gallery.no_photo_label) {
        lv_obj_delete(s_gallery.no_photo_label);
        s_gallery.no_photo_label = NULL;
    }
}

/**
 * @brief 同步解码指定有效下标，失败以灰背景代替，最后绑定 RGB 缓冲并更新当前索引。
 * @param[in] index 有限照片表中的下标，越界忽略。
 * @note 失败不把错误图片当成功解码，但当前选择仍移到目标下标。
 */
static void gallery_show_image(int index)
{
    if (index < 0 || index >= s_gallery.count) {
        return;
    }

    printf(LOG_TAG "Show %d/%d: %s\n", index + 1, s_gallery.count, s_gallery.photos[index].path);

    hide_no_photo_hint();

    int ret = gallery_decode_jpeg(s_gallery.photos[index].path);
    if (ret != 0) {
        printf(LOG_TAG "Decode failed %d\n", ret);
        /* 显示灰色背景 */
        if (s_gallery.rgb_buf) {
            u16 gray = 0x7BEF;
            for (int i = 0; i < SCR_W * SCR_H; i++) {
                ((u16 *)s_gallery.rgb_buf)[i] = gray;
            }
        }
    }

    if (s_gallery.canvas && s_gallery.rgb_buf) {
        lv_canvas_set_buffer(s_gallery.canvas, s_gallery.rgb_buf, SCR_W, SCR_H, LV_COLOR_FORMAT_RGB565);
    }

    s_gallery.current = index;
    update_index_label();
}

//*********************************************************************************//
//                              删除功能                                            //
//*********************************************************************************//

/**
 * @brief 防止重复进入后同步删除当前文件，成功重扫并显示相邻项或空状态。
 * @note 删除失败恢复按钮及本地进行标志；当前目录及文件副作用沿用旧页面显式删除操作，不在注释整理中执行。
 */
static void gallery_delete_current(void)
{
    if (s_gallery.delete_in_progress) {
        return;
    }

    if (s_gallery.count == 0 || s_gallery.current < 0) {
        return;
    }

    s_gallery.delete_in_progress = 1;

    if (s_gallery.btn_delete) {
        lv_obj_add_state(s_gallery.btn_delete, LV_STATE_DISABLED);
    }

    const char *path = s_gallery.photos[s_gallery.current].path;
    printf(LOG_TAG "Delete: %s\n", path);

    int ret = fdelete_by_name(path);
    if (ret != 0) {
        printf(LOG_TAG "Delete failed %d\n", ret);
        s_gallery.delete_in_progress = 0;
        if (s_gallery.btn_delete) {
            lv_obj_remove_state(s_gallery.btn_delete, LV_STATE_DISABLED);
        }
        return;
    }

    os_time_dly(5);

    int old_current = s_gallery.current;
    gallery_scan_photos();

    if (s_gallery.count == 0) {
        s_gallery.current = 0;
        show_no_photo_hint();
    } else if (old_current >= s_gallery.count) {
        s_gallery.current = s_gallery.count - 1;
        gallery_show_image(s_gallery.current);
    } else {
        gallery_show_image(s_gallery.current);
    }

    s_gallery.delete_in_progress = 0;
    if (s_gallery.btn_delete) {
        lv_obj_remove_state(s_gallery.btn_delete, LV_STATE_DISABLED);
    }
}

/**
 * @brief 把删除按钮动作同步交给当前照片删除路径。
 * @param[in] e 点击事件，当前未读取载荷。
 */
static void on_delete_btn_click(lv_event_t *e)
{
    (void)e;
    gallery_delete_current();
}

//*********************************************************************************//
//                              手势处理                                            //
//*********************************************************************************//

/**
 * @brief 左／右切换有限照片，上滑向页面管理器请求返回首页。
 * @param[in] gesture_type 旧页面手势编号，3 左、4 右、1 上。
 * @return 识别并处理为 1，边界或不支持的手势为 0。
 */
static int gallery_gesture_handler(int gesture_type)
{
    switch (gesture_type) {
    case 3:  /* 左滑 -> 下一张 */
        if (s_gallery.current < s_gallery.count - 1) {
            gallery_show_image(s_gallery.current + 1);
            return 1;
        }
        break;

    case 4:  /* 右滑 -> 上一张 */
        if (s_gallery.current > 0) {
            gallery_show_image(s_gallery.current - 1);
            return 1;
        }
        break;

    case 1:  /* 上滑 -> 返回主页 */
        page_manager_back_to_main();
        return 1;
    }

    return 0;
}

//*********************************************************************************//
//                              页面生命周期回调                                    //
//*********************************************************************************//

/**
 * @brief 分配页面期 YUV／RGB 缓冲并创建 Canvas、删除操作及索引。
 * @param[in,out] parent 页面管理器拥有的根容器。
 * @return 父对象或必要缓冲分配失败为 -1，其他路径返回 0；当前未逐项报告所有 LVGL 子对象失败。
 * @note RGB 分配失败回收先前 YUV；缓冲容量及采样格式限制见 decode 合同，不提供独立后台解码。
 */
static int gallery_page_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return -1;
    }

    printf(LOG_TAG "=== Create ===\n");

    s_gallery.current = 0;
    s_gallery.count = 0;
    s_gallery.delete_in_progress = 0;
    s_gallery.no_photo_label = NULL;

    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* 分配YUV缓冲区 (最大640x640 YUV = 614400字节) */
    int yuv_size = MAX_PHOTO_W * MAX_PHOTO_H * 3 / 2;
    s_gallery.yuv_buf = (u8 *)malloc(yuv_size);
    if (!s_gallery.yuv_buf) {
        printf(LOG_TAG "ERROR: YUV buffer\n");
        return -1;
    }

    /* 分配RGB565缓冲区 */
    int rgb_size = SCR_W * SCR_H * 2;
    s_gallery.rgb_buf = (u8 *)malloc(rgb_size);
    if (!s_gallery.rgb_buf) {
        free(s_gallery.yuv_buf);
        s_gallery.yuv_buf = NULL;
        return -1;
    }
    memset(s_gallery.rgb_buf, 0, rgb_size);

    /* 创建画布 */
    s_gallery.canvas = lv_canvas_create(parent);
    lv_obj_set_pos(s_gallery.canvas, 0, 0);
    lv_canvas_set_buffer(s_gallery.canvas, s_gallery.rgb_buf, SCR_W, SCR_H, LV_COLOR_FORMAT_RGB565);

    /* 底部操作栏 */
    lv_obj_t *bottom_bar = lv_obj_create(parent);
    lv_obj_set_size(bottom_bar, SCR_W, 50);
    lv_obj_set_pos(bottom_bar, 0, SCR_H - 50);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);
    lv_obj_set_style_radius(bottom_bar, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar, 0, 0);
    lv_obj_remove_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* 删除按钮 */
    s_gallery.btn_delete = lv_btn_create(bottom_bar);
    lv_obj_set_size(s_gallery.btn_delete, 80, 36);
    lv_obj_align(s_gallery.btn_delete, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_gallery.btn_delete, lv_color_hex(0xC62828), 0);
    lv_obj_set_style_radius(s_gallery.btn_delete, 8, 0);
    lv_obj_add_event_cb(s_gallery.btn_delete, on_delete_btn_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *del_label = lv_label_create(s_gallery.btn_delete);
    lv_label_set_text(del_label, LV_SYMBOL_TRASH " Delete");
    lv_obj_set_style_text_color(del_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(del_label, &lv_font_montserrat_12, 0);
    lv_obj_center(del_label);

    /* 索引标签 */
    s_gallery.label_index = lv_label_create(parent);
    lv_obj_set_style_text_color(s_gallery.label_index, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_gallery.label_index, &lv_font_montserrat_14, 0);
    lv_obj_align(s_gallery.label_index, LV_ALIGN_TOP_RIGHT, -10, 10);

    /* 提示文字 */
    lv_obj_t *hint_label = lv_label_create(parent);
    lv_label_set_text(hint_label, "Swipe left/right");
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_12, 0);
    lv_obj_align(hint_label, LV_ALIGN_TOP_LEFT, 10, 10);

    gallery_scan_photos();

    if (s_gallery.count > 0) {
        update_index_label();
    } else {
        show_no_photo_hint();
    }

    printf(LOG_TAG "Created\n");
    return 0;
}

/**
 * @brief 标记活跃并重新扫描照片，有照片时同步显示首项，否则呈现空提示。
 */
static void gallery_page_enter(void)
{
    printf(LOG_TAG "=== Enter ===\n");

    s_gallery.is_active = 1;
    s_gallery.delete_in_progress = 0;

    gallery_scan_photos();

    if (s_gallery.count > 0) {
        hide_no_photo_hint();
        s_gallery.current = 0;
        gallery_show_image(0);
    } else {
        show_no_photo_hint();
    }
}

/**
 * @brief 只撤销本地活跃标志，不释放图片缓冲。
 */
static void gallery_page_exit(void)
{
    s_gallery.is_active = 0;
    printf(LOG_TAG "Exit\n");
}

/**
 * @brief 释放模块持有的 RGB 及 YUV 缓冲并清空它们的指针。
 * @note 页面对象由管理器删除；本函数没有独立绘制／IO 并发排空，须遵守调用方页面生命周期顺序。
 */
static void gallery_page_destroy(void)
{
    printf(LOG_TAG "Destroy\n");

    if (s_gallery.rgb_buf) {
        free(s_gallery.rgb_buf);
        s_gallery.rgb_buf = NULL;
    }
    if (s_gallery.yuv_buf) {
        free(s_gallery.yuv_buf);
        s_gallery.yuv_buf = NULL;
    }
}

//*********************************************************************************//
//                              模块注册                                            //
//*********************************************************************************//

static const page_ops_t s_gallery_page_ops = {
    .create  = gallery_page_create,
    .enter   = gallery_page_enter,
    .exit    = gallery_page_exit,
    .destroy = gallery_page_destroy,
    .update  = NULL,
    .gesture = gallery_gesture_handler,
};

static page_desc_t s_gallery_page_desc = {
    .page_type  = 0,
    .name       = "GalleryPage",
    .ops        = &s_gallery_page_ops,
    .container  = NULL,
    .is_created = 0,
    .is_active  = 0,
};

/**
 * @brief 保存页面类型并注册静态旧相册描述。
 * @param[in] page_type 模块装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int gallery_register_page(int page_type)
{
    s_gallery.page_type = page_type;
    s_gallery_page_desc.page_type = page_type;
    return page_manager_register(&s_gallery_page_desc);
}

D_UI_MODULE_DEFINE(gallery, "Gallery", LV_SYMBOL_IMAGE, gallery_register_page, NULL);

#endif /* D_FEATURE_GALLERY */
