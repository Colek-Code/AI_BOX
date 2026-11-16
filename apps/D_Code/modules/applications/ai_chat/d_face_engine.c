/**
 * @file d_face_engine.c
 * @brief 表情渲染引擎 v3 - 基于 Stack-chan 表情模型的 C/LVGL 适配实现
 *        8种情绪、5种装饰器、2种脸型（simple/dog）
 *        平滑插值、嘴巴状态机、情绪衰减、说话动画
 *        使用 LVGL Canvas + lv_draw_rect/arc/line/triangle 绘制
 */

#include "config/app_config.h"

#if (D_FEATURE_AI_CHAT) && defined(D_LVGL_ENABLE)

#include "d_face_engine.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define LOG_TAG "[FACE] "

/* 2π × 1000 常量 */
#define TWO_PI_MILLI  6283

/**
 * @brief 按百分比向目标做整数插值，非零差至少前进一步避免停滞。
 * @param[in] current 当前值。
 * @param[in] target 目标值。
 * @param[in] speed 本次使用的百分比速度，调用链使用配置常量。
 * @return 本轮新的整数值。
 */

static int lerp_int(int current, int target, int speed)
{
    int diff = target - current;
    if (diff == 0) return current;
    int step = diff * speed / 100;
    if (step == 0) step = (diff > 0) ? 1 : -1;
    return current + step;
}

/* 平滑绘制参数 */
static struct {
    int eye_open[2];
    int eye_gaze_x[2];
    int eye_gaze_y[2];
    int mouth_w;
    int mouth_h;
    int breath_y;
    int eyelid_h[2];
} s_smooth;

/*==================== 内部状态 ====================*/

static lv_obj_t      *s_canvas = NULL;
static face_context_t  s_ctx;
static face_context_t  s_last_ctx;
static face_type_t     s_face_type = FACE_TYPE_SIMPLE;

/* Canvas 缓冲区 */
static uint8_t s_canvas_buf[LV_CANVAS_BUF_SIZE(FACE_CANVAS_W, FACE_CANVAS_H, 16, 1)] __attribute__((aligned(4)));

/*==================== 装饰器状态 ====================*/

#define DECO_SLOT_MAX  2

static struct {
    face_deco_t type;
    int time_ms;     /* 动画时间累计 */
    /* 粒子/元素状态（按需使用） */
    int px[4];       /* X坐标 */
    int py[4];       /* Y坐标 */
    int pr[4];       /* 半径/大小 */
} s_deco[DECO_SLOT_MAX];

/**
 * @brief 在半开区间生成伪随机整数。
 * @param[in] min_val 包含的下界。
 * @param[in] max_val 不包含的上界。
 * @return max_val 大于 min_val 时落在 [min_val,max_val)，否则直接返回 min_val。
 */

static int rand_between(int min_val, int max_val)
{
    if (max_val <= min_val) return min_val;
    return min_val + (rand() % (max_val - min_val));
}

/**
 * @brief 把毫弧度角度转换为放大一百倍的正弦近似。
 * @param[in] angle_milli 千分之一弧度单位角度。
 * @return 截断为整数的 sin(angle_milli/1000)×100。
 */
static int sin100(int angle_milli)
{
    float rad = (float)angle_milli / 1000.0f;
    return (int)(sinf(rad) * 100.0f);
}

/**
 * @brief 把整数值限定在调用方给定闭区间。
 * @param[in] val 候选值。
 * @param[in] lo 包含的下界。
 * @param[in] hi 包含的上界，调用方保证不小于 lo。
 * @return 钳制后的值。
 */
static int clamp(int val, int lo, int hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/*==================== 眨眼动画 ====================*/

static struct {
    int is_blinking;
    int next_toggle;
    int count;
} s_blink;

/**
 * @brief 按闭眼前快后慢的分段曲线计算眼睛开度。
 * @param[in] count 当前眨眼阶段经过时间。
 * @param[in] duration 正的阶段总时长。
 * @return 正常阶段内二十至一百的开度百分比。
 */
static int blink_ease(int count, int duration)
{
    int frac_x100 = count * 100 / duration;
    int result;
    if (frac_x100 < 25) {
        result = 100 - frac_x100 * 4;
    } else {
        int t = frac_x100 - 25;
        result = t * t * 16 / 900;
    }
    return 20 + result * 80 / 100;
}

/**
 * @brief 初始化睁眼状态及下一次随机切换时刻。
 */
static void blink_init(void)
{
    s_blink.is_blinking = 0;
    s_blink.next_toggle = rand_between(BLINK_OPEN_MIN, BLINK_OPEN_MAX);
    s_blink.count = 0;
}

/**
 * @brief 推进眨眼相位并将开度系数乘到两眼目标开度。
 * @param[in] interval_ms 本轮经过的毫秒数。
 */
static void blink_update(int interval_ms)
{
    int eye_open = 100;
    if (s_blink.is_blinking) {
        eye_open = blink_ease(s_blink.count, s_blink.next_toggle);
    }
    s_blink.count += interval_ms;
    if (s_blink.count >= s_blink.next_toggle) {
        s_blink.is_blinking = !s_blink.is_blinking;
        s_blink.count = 0;
        s_blink.next_toggle = s_blink.is_blinking
            ? rand_between(BLINK_CLOSE_MIN, BLINK_CLOSE_MAX)
            : rand_between(BLINK_OPEN_MIN, BLINK_OPEN_MAX);
    }
    s_ctx.eyes[0].open = s_ctx.eyes[0].open * eye_open / 100;
    s_ctx.eyes[1].open = s_ctx.eyes[1].open * eye_open / 100;
}

/*==================== 呼吸动画 ====================*/

static struct {
    int time_ms;
} s_breath;

/**
 * @brief 将呼吸相位计数清零。
 */
static void breath_init(void) { s_breath.time_ms = 0; }

/**
 * @brief 推进周期呼吸偏移，炎热情绪使用更快周期及更大幅度。
 * @param[in] interval_ms 本轮经过的毫秒数。
 */
static void breath_update(int interval_ms)
{
    /* HOT 情绪时呼吸加速（周期减半） */
    int duration = (s_ctx.emotion == FACE_EMOTION_HOT) ? BREATH_DURATION / 2 : BREATH_DURATION;
    s_breath.time_ms = (s_breath.time_ms + interval_ms) % duration;
    int angle_milli = (int)(2.0f * 3.14159f * 1000.0f * s_breath.time_ms / duration);
    int raw = sin100(angle_milli);
    /* HOT 时呼吸幅度加大 */
    int amplitude = (s_ctx.emotion == FACE_EMOTION_HOT) ? 5 : 3;
    s_ctx.breath_offset = raw * amplitude / 100;
}

/*==================== 微视动画 ====================*/

static struct {
    int next_toggle;
    int target_x;
    int target_y;
} s_saccade;

/**
 * @brief 初始化微视目标及随机更新倒计时。
 */
static void saccade_init(void)
{
    s_saccade.next_toggle = rand_between(SACCADE_UPDATE_MIN, SACCADE_UPDATE_MAX);
    s_saccade.target_x = 0;
    s_saccade.target_y = 0;
}

/**
 * @brief 到期时选择小范围新注视偏移，并把相同目标应用到两眼。
 * @param[in] interval_ms 本轮经过的毫秒数。
 */
static void saccade_update(int interval_ms)
{
    s_saccade.next_toggle -= interval_ms;
    if (s_saccade.next_toggle < 0) {
        s_saccade.target_x = rand_between(-SACCADE_GAIN, SACCADE_GAIN + 1);
        s_saccade.target_y = rand_between(-SACCADE_GAIN, SACCADE_GAIN + 1);
        s_saccade.next_toggle = rand_between(SACCADE_UPDATE_MIN, SACCADE_UPDATE_MAX);
    }
    s_ctx.eyes[0].gaze_x = s_saccade.target_x;
    s_ctx.eyes[0].gaze_y = s_saccade.target_y;
    s_ctx.eyes[1].gaze_x = s_saccade.target_x;
    s_ctx.eyes[1].gaze_y = s_saccade.target_y;
}

/*==================== 颤抖效果（COLD 情绪用） ====================*/

static struct {
    int offset_x;
    int toggle_ms;
} s_shiver;

/**
 * @brief 仅寒冷情绪随机改变横向颤抖偏移，其他情绪清零。
 * @param[in] interval_ms 本轮经过的毫秒数。
 */
static void shiver_update(int interval_ms)
{
    if (s_ctx.emotion != FACE_EMOTION_COLD) {
        s_shiver.offset_x = 0;
        return;
    }
    s_shiver.toggle_ms -= interval_ms;
    if (s_shiver.toggle_ms <= 0) {
        s_shiver.offset_x = rand_between(-2, 3);
        s_shiver.toggle_ms = 80 + rand_between(0, 60);
    }
}

/*==================== 嘴巴动画状态机 ====================*/

static struct {
    mouth_state_t state;
    int time_ms;
    int remaining_ms;
    int toggle_timer;
    int is_open;
} s_mouth;

/**
 * @brief 清空嘴巴动画并进入空闲状态。
 */
static void mouth_init(void)
{
    memset(&s_mouth, 0, sizeof(s_mouth));
    s_mouth.state = MOUTH_STATE_IDLE;
}

/**
 * @brief 按空闲、思考或有限说话状态计算并钳制嘴巴目标开度。
 * @param[in] interval_ms 本轮经过的毫秒数。
 */
static void mouth_update(int interval_ms)
{
    s_mouth.time_ms += interval_ms;
    int target_open = 0;

    switch (s_mouth.state) {
    case MOUTH_STATE_IDLE: {
        int breath_factor = sin100((int)(2.0f * 3.14159f * 1000.0f * s_breath.time_ms / BREATH_DURATION));
        target_open = 3 + breath_factor * 3 / 100;
        break;
    }
    case MOUTH_STATE_THINKING: {
        int phase = s_mouth.time_ms % THINKING_PERIOD;
        int angle = (int)(2.0f * 3.14159f * 1000.0f * phase / THINKING_PERIOD);
        int wave = sin100(angle);
        target_open = 12 + wave * 12 / 100;
        break;
    }
    case MOUTH_STATE_SPEAKING: {
        s_mouth.remaining_ms -= interval_ms;
        if (s_mouth.remaining_ms <= 0) {
            s_mouth.state = MOUTH_STATE_IDLE;
            s_mouth.time_ms = 0;
            target_open = 0;
            break;
        }
        s_mouth.toggle_timer -= interval_ms;
        if (s_mouth.toggle_timer <= 0) {
            s_mouth.is_open = !s_mouth.is_open;
            s_mouth.toggle_timer = SPEAKING_TOGGLE_MS + rand_between(-50, 80);
        }
        target_open = s_mouth.is_open ? rand_between(35, 75) : rand_between(5, 15);
        break;
    }
    }
    s_ctx.mouth_open = clamp(target_open, 0, 100);
}

/*==================== 情绪衰减 + 装饰器自动绑定 ====================*/

static struct {
    face_emotion_t target;
    int decay_timer;
    int decaying;
} s_emotion;

/**
 * @brief 把目标情绪及衰减状态初始化为中性。
 */
static void emotion_init(void)
{
    s_emotion.target = FACE_EMOTION_NEUTRAL;
    s_emotion.decay_timer = 0;
    s_emotion.decaying = 0;
}

/**
 * @brief 按目标情绪更新槽零的装饰器，变化时初始化其粒子。
 * @param[in] emo 目标情绪枚举，未映射情绪使用 NONE。
 * @note 不修改留给手动控制的槽一。
 */
static void emotion_auto_deco(face_emotion_t emo)
{
    face_deco_t deco = FACE_DECO_NONE;
    switch (emo) {
    case FACE_EMOTION_HAPPY:   deco = FACE_DECO_HEART;      break;
    case FACE_EMOTION_ANGRY:   deco = FACE_DECO_ANGRY_MARK; break;
    case FACE_EMOTION_SAD:     deco = FACE_DECO_PALE;       break;
    case FACE_EMOTION_SLEEPY:  deco = FACE_DECO_BUBBLE;     break;
    case FACE_EMOTION_COLD:    deco = FACE_DECO_SWEAT;      break;
    default: break;
    }
    /* 只自动设 slot 0，slot 1 留给手动 */
    if (s_deco[0].type != deco) {
        s_deco[0].type = deco;
        s_deco[0].time_ms = 0;
        /* 初始化粒子位置 */
        for (int i = 0; i < 4; i++) {
            s_deco[0].px[i] = rand_between(5, 35);
            s_deco[0].py[i] = rand_between(5, 35);
            s_deco[0].pr[i] = rand_between(3, 7);
        }
    }
}

/**
 * @brief 累计非中性情绪的衰减倒计时，到期恢复中性及默认装饰器。
 * @param[in] interval_ms 本轮经过的毫秒数。
 */
static void emotion_update(int interval_ms)
{
    if (s_emotion.target != FACE_EMOTION_NEUTRAL && !s_emotion.decaying) {
        s_emotion.decay_timer -= interval_ms;
        if (s_emotion.decay_timer <= 0) {
            s_emotion.decaying = 1;
        }
    }
    if (s_emotion.decaying) {
        s_emotion.target = FACE_EMOTION_NEUTRAL;
        s_emotion.decaying = 0;
        emotion_auto_deco(FACE_EMOTION_NEUTRAL);
    }
    s_ctx.emotion = s_emotion.target;
}

/**
 * @brief 建立眼睛、嘴巴、呼吸及眼睑的初始平滑绘制值。
 */

static void smooth_init(void)
{
    s_smooth.eye_open[0] = 100;
    s_smooth.eye_open[1] = 100;
    s_smooth.eye_gaze_x[0] = 0;
    s_smooth.eye_gaze_x[1] = 0;
    s_smooth.eye_gaze_y[0] = 0;
    s_smooth.eye_gaze_y[1] = 0;
    s_smooth.mouth_w = MOUTH_MIN_W;
    s_smooth.mouth_h = MOUTH_MIN_H;
    s_smooth.breath_y = 0;
    s_smooth.eyelid_h[0] = 0;
    s_smooth.eyelid_h[1] = 0;
}

/**
 * @brief 按配置速度插值眼睛及嘴巴，叠加情绪眼睑差异并同步呼吸位移。
 */
static void smooth_update(void)
{
    s_smooth.eye_open[0] = lerp_int(s_smooth.eye_open[0], s_ctx.eyes[0].open, LERP_SPEED_FAST);
    s_smooth.eye_open[1] = lerp_int(s_smooth.eye_open[1], s_ctx.eyes[1].open, LERP_SPEED_FAST);

    s_smooth.eye_gaze_x[0] = lerp_int(s_smooth.eye_gaze_x[0], s_ctx.eyes[0].gaze_x, LERP_SPEED_NORMAL);
    s_smooth.eye_gaze_y[0] = lerp_int(s_smooth.eye_gaze_y[0], s_ctx.eyes[0].gaze_y, LERP_SPEED_NORMAL);
    s_smooth.eye_gaze_x[1] = lerp_int(s_smooth.eye_gaze_x[1], s_ctx.eyes[1].gaze_x, LERP_SPEED_NORMAL);
    s_smooth.eye_gaze_y[1] = lerp_int(s_smooth.eye_gaze_y[1], s_ctx.eyes[1].gaze_y, LERP_SPEED_NORMAL);

    int open_ratio = s_ctx.mouth_open;
    int target_h = MOUTH_MIN_H + (MOUTH_MAX_H - MOUTH_MIN_H) * open_ratio / 100;
    int target_w = MOUTH_MIN_W + (MOUTH_MAX_W - MOUTH_MIN_W) * (100 - open_ratio) / 100;
    s_smooth.mouth_w = lerp_int(s_smooth.mouth_w, target_w, LERP_SPEED_FAST);
    s_smooth.mouth_h = lerp_int(s_smooth.mouth_h, target_h, LERP_SPEED_FAST);

    s_smooth.breath_y = s_ctx.breath_offset;

    for (int i = 0; i < 2; i++) {
        int target_lid = EYELID_H * (100 - s_smooth.eye_open[i]) / 100;
        switch (s_ctx.emotion) {
        case FACE_EMOTION_SLEEPY:
            target_lid = EYELID_H / 2 + target_lid / 2;
            break;
        case FACE_EMOTION_DOUBTFUL:
            /* 疑惑：右眼(side=1)额外遮挡，形成不对称 */
            if (i == 1) target_lid = target_lid + EYELID_H / 4;
            break;
        case FACE_EMOTION_HOT:
            /* 炎热：眼睑收回（眼睛张大） */
            target_lid = target_lid / 2;
            break;
        case FACE_EMOTION_COLD:
            /* 寒冷：眼睛缩小 */
            target_lid = target_lid + EYELID_H / 5;
            break;
        default:
            break;
        }
        s_smooth.eyelid_h[i] = lerp_int(s_smooth.eyelid_h[i], target_lid, LERP_SPEED_SLOW);
    }
}

/**
 * @brief 用圆角矩形原语绘制不透明实心圆，非正半径忽略。
 * @param[in,out] layer 当前 Canvas 的借用绘制层。
 * @param[in] cx 圆心横坐标。
 * @param[in] cy 圆心纵坐标。
 * @param[in] r 半径像素。
 * @param[in] color 填充色。
 */

static void draw_filled_circle(lv_layer_t *layer, int cx, int cy, int r, lv_color_t color)
{
    if (r <= 0) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.border_width = 0;
    lv_area_t area = { cx - r, cy - r, cx + r, cy + r };
    lv_draw_rect(layer, &dsc, &area);
}

/**
 * @brief 在指定像素矩形绘制不透明填充，非正尺寸忽略。
 * @param[in,out] layer 当前借用绘制层。
 * @param[in] x 左上横坐标。
 * @param[in] y 左上纵坐标。
 * @param[in] w 宽度像素。
 * @param[in] h 高度像素。
 * @param[in] color 填充色。
 */
static void draw_filled_rect(lv_layer_t *layer, int x, int y, int w, int h, lv_color_t color)
{
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = 0;
    dsc.border_width = 0;
    lv_area_t area = { x, y, x + w - 1, y + h - 1 };
    lv_draw_rect(layer, &dsc, &area);
}

/**
 * @brief 绘制不透明圆角矩形，非正尺寸忽略。
 * @param[in,out] layer 当前借用绘制层。
 * @param[in] x 左上横坐标。
 * @param[in] y 左上纵坐标。
 * @param[in] w 宽度像素。
 * @param[in] h 高度像素。
 * @param[in] radius 圆角像素半径。
 * @param[in] color 填充色。
 */
static void draw_rounded_rect(lv_layer_t *layer, int x, int y, int w, int h, int radius, lv_color_t color)
{
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = radius;
    dsc.border_width = 0;
    lv_area_t area = { x, y, x + w - 1, y + h - 1 };
    lv_draw_rect(layer, &dsc, &area);
}

/**
 * @brief 绘制不透明且两端圆角的线段。
 * @param[in,out] layer 当前借用绘制层。
 * @param[in] x1 起点横坐标。
 * @param[in] y1 起点纵坐标。
 * @param[in] x2 终点横坐标。
 * @param[in] y2 终点纵坐标。
 * @param[in] width 线宽像素。
 * @param[in] color 线色。
 */
static void draw_line(lv_layer_t *layer, int x1, int y1, int x2, int y2, int width, lv_color_t color)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.p1.x = x1; dsc.p1.y = y1;
    dsc.p2.x = x2; dsc.p2.y = y2;
    lv_draw_line(layer, &dsc);
}

/**
 * @brief 用三个顶点绘制不透明填充三角形。
 * @param[in,out] layer 当前借用绘制层。
 * @param[in] x1 第一点横坐标。
 * @param[in] y1 第一点纵坐标。
 * @param[in] x2 第二点横坐标。
 * @param[in] y2 第二点纵坐标。
 * @param[in] x3 第三点横坐标。
 * @param[in] y3 第三点纵坐标。
 * @param[in] color 填充色。
 */
static void draw_triangle(lv_layer_t *layer, int x1, int y1, int x2, int y2, int x3, int y3, lv_color_t color)
{
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.p[0].x = x1; dsc.p[0].y = y1;
    dsc.p[1].x = x2; dsc.p[1].y = y2;
    dsc.p[2].x = x3; dsc.p[2].y = y3;
    lv_draw_triangle(layer, &dsc);
}

/**
 * @brief 绘制不透明圆角端点圆弧，非正半径忽略。
 * @param[in,out] layer 当前借用绘制层。
 * @param[in] cx 圆心横坐标。
 * @param[in] cy 圆心纵坐标。
 * @param[in] radius 圆弧半径。
 * @param[in] start_deg 起始角度，单位度。
 * @param[in] end_deg 终止角度，单位度。
 * @param[in] width 弧线宽度。
 * @param[in] color 弧线颜色。
 */
static void draw_arc_stroke(lv_layer_t *layer, int cx, int cy, int radius, int start_deg, int end_deg, int width, lv_color_t color)
{
    if (radius <= 0) return;
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.opa = LV_OPA_COVER;
    dsc.center.x = cx;
    dsc.center.y = cy;
    dsc.radius = radius;
    dsc.start_angle = start_deg;
    dsc.end_angle = end_deg;
    dsc.rounded = 1;
    lv_draw_arc(layer, &dsc);
}

/**
 * @brief 按眼别及平滑注视、呼吸、颤抖状态绘制眼睛圆形，脸型和情绪调整半径。
 * @param[in,out] layer 当前借用绘制层。
 * @param[in] side 0 左眼，1 右眼。
 */

static void draw_eye_simple(lv_layer_t *layer, int side)
{
    int cx = (side == 0) ? EYE_LEFT_CX : EYE_RIGHT_CX;
    int cy = (side == 0) ? EYE_LEFT_CY : EYE_RIGHT_CY;
    int by = s_smooth.breath_y;
    int sx = s_shiver.offset_x;  /* COLD 颤抖 */
    /* dog face 眼睛更大（radius=10 vs 8） */
    int r = (s_face_type == FACE_TYPE_DOG) ? DOG_EYE_RADIUS : EYE_RADIUS;

    /* HOT 情绪眼睛略微放大 */
    if (s_ctx.emotion == FACE_EMOTION_HOT) r += 2;
    /* COLD 情绪眼睛略微缩小 */
    if (s_ctx.emotion == FACE_EMOTION_COLD) r -= 1;

    draw_filled_circle(layer,
        cx + s_smooth.eye_gaze_x[side] + sx,
        cy + s_smooth.eye_gaze_y[side] + by,
        r, s_ctx.fg_color);
}

/**
 * @brief 用背景色覆盖眼睛上部，按情绪形成倾斜、笑眼或不对称眼睑。
 * @param[in,out] layer 当前借用绘制层。
 * @param[in] side 0 左眼，1 右眼。
 */
static void draw_eyelid_simple(lv_layer_t *layer, int side)
{
    int cx = (side == 0) ? EYE_LEFT_CX : EYE_RIGHT_CX;
    int cy = (side == 0) ? EYE_LEFT_CY : EYE_RIGHT_CY;
    int by = s_smooth.breath_y;
    int sx = s_shiver.offset_x;
    int x = cx - EYELID_W / 2 + sx;
    int y = cy - EYELID_H / 2 + by;
    int lid_h = s_smooth.eyelid_h[side];

    switch (s_ctx.emotion) {
    case FACE_EMOTION_ANGRY:
    case FACE_EMOTION_SAD: {
        int total_h = (EYELID_H + lid_h) / 2;
        int h_inner, h_outer;
        if (s_ctx.emotion == FACE_EMOTION_ANGRY) {
            h_inner = total_h; h_outer = lid_h;
        } else {
            h_inner = lid_h; h_outer = total_h;
        }
        if (side == 0) {
            draw_filled_rect(layer, x, y, EYELID_W / 2, h_outer, s_ctx.bg_color);
            draw_filled_rect(layer, x + EYELID_W / 2, y, EYELID_W / 2, h_inner, s_ctx.bg_color);
        } else {
            draw_filled_rect(layer, x, y, EYELID_W / 2, h_inner, s_ctx.bg_color);
            draw_filled_rect(layer, x + EYELID_W / 2, y, EYELID_W / 2, h_outer, s_ctx.bg_color);
        }
        break;
    }
    case FACE_EMOTION_HAPPY:
        if (lid_h > 0) {
            draw_filled_rect(layer, x, y, EYELID_W, lid_h * 6 / 10, s_ctx.bg_color);
        }
        draw_filled_rect(layer, x, y + EYELID_H * 6 / 10, EYELID_W, EYELID_H * 4 / 10, s_ctx.bg_color);
        break;
    case FACE_EMOTION_DOUBTFUL:
        /* 疑惑：右眼额外遮挡（通过 smooth 已处理），普通绘制 */
        if (lid_h > 0) {
            draw_filled_rect(layer, x, y, EYELID_W, lid_h, s_ctx.bg_color);
        }
        break;
    default:
        /* NEUTRAL / SLEEPY / COLD / HOT 都用普通眼睑 */
        if (lid_h > 0) {
            draw_filled_rect(layer, x, y, EYELID_W, lid_h, s_ctx.bg_color);
        }
        break;
    }
}

/**
 * @brief 按平滑宽高及呼吸／颤抖位移绘制简单脸圆角嘴。
 * @param[in,out] layer 当前借用绘制层。
 */
static void draw_mouth_simple(lv_layer_t *layer)
{
    int w = s_smooth.mouth_w;
    int h = s_smooth.mouth_h;
    int sx = s_shiver.offset_x;
    int x = MOUTH_CX - w / 2 + sx;
    int y = MOUTH_CY - h / 2 + s_smooth.breath_y;

    int radius;
    if (h <= 6) radius = h / 2;
    else if (h <= 20) radius = 6;
    else radius = 10;

    draw_rounded_rect(layer, x, y, w, h, radius, s_ctx.fg_color);
}

/*==================== Dog Face 绘制（移植自 dog-face.ts） ====================*/

/* 狗脸眉毛参数 */
#define EYEBROW_W  16
#define EYEBROW_H   5

/**
 * @brief 用两半矩形近似狗脸倾斜眉毛，情绪决定内外侧高度。
 * @param[in,out] layer 当前借用绘制层。
 * @param[in] side 0 左眉，1 右眉。
 */
static void draw_eyebrow(lv_layer_t *layer, int side)
{
    int cx = (side == 0) ? EYE_LEFT_CX : EYE_RIGHT_CX;
    int cy = (side == 0) ? EYE_LEFT_CY : EYE_RIGHT_CY;
    int by = s_smooth.breath_y;
    int direction = (side == 0) ? 1 : -1;
    int open_offset = s_smooth.eye_open[side] * 2 / 100;

    /* 眉毛位置：眼睛上方 */
    int bx = cx + 4 * direction;
    int b_y = cy - 20 - open_offset + by;

    /* 根据情绪调整倾斜 */
    int tilt = 0;
    if (s_ctx.emotion == FACE_EMOTION_ANGRY) {
        tilt = 3 * direction;  /* 怒眉：内高外低 */
    } else if (s_ctx.emotion == FACE_EMOTION_SAD) {
        tilt = -3 * direction; /* 悲眉：内低外高 */
    }

    /* 用倾斜的圆角矩形近似椭圆眉毛 */
    int left_y = b_y - tilt;
    int right_y = b_y + tilt;
    /* 分两半画出倾斜效果 */
    draw_filled_rect(layer, bx - EYEBROW_W / 2, left_y, EYEBROW_W / 2, EYEBROW_H, s_ctx.fg_color);
    draw_filled_rect(layer, bx, right_y, EYEBROW_W / 2, EYEBROW_H, s_ctx.fg_color);
}

/**
 * @brief 用圆与倒三角组合狗脸鼻子并同步呼吸位移。
 * @param[in,out] layer 当前借用绘制层。
 */
static void draw_nose(lv_layer_t *layer)
{
    int by = s_smooth.breath_y;
    int nose_cx = MOUTH_CX;
    int nose_cy = DOG_MOUTH_CY - 20 + by;

    /* 鼻子：倒三角 + 上方圆 */
    draw_filled_circle(layer, nose_cx, nose_cy - 4, 5, s_ctx.fg_color);
    draw_triangle(layer,
        nose_cx - 5, nose_cy - 2,
        nose_cx + 5, nose_cy - 2,
        nose_cx, nose_cy + 6,
        s_ctx.fg_color);
}

/**
 * @brief 用两段弧近似狗脸 W 形嘴，开度足够时追加舌部区域。
 * @param[in,out] layer 当前借用绘制层。
 */
static void draw_mouth_dog(lv_layer_t *layer)
{
    int by = s_smooth.breath_y;
    int sx = s_shiver.offset_x;
    int open_ratio = s_ctx.mouth_open;
    int w = 50 + (60 - 50) * open_ratio / 100;
    int h = 8 + (24 - 8) * open_ratio / 100;
    int mcx = MOUTH_CX + sx;
    int mcy = DOG_MOUTH_CY + by;  /* dog face 嘴巴位置上移 */

    /* 嘴巴上半部：两段向下弧（W形），用两条弧线 */
    /* 左半弧 */
    draw_arc_stroke(layer, mcx - w / 4, mcy - 6, w / 3, 0, 180, 2, s_ctx.fg_color);
    /* 右半弧 */
    draw_arc_stroke(layer, mcx + w / 4, mcy - 6, w / 3, 0, 180, 2, s_ctx.fg_color);

    /* 张嘴时画舌头区域 */
    if (h > 16) {
        int tongue_w = w * 3 / 4;
        int tongue_h = h - 16;
        int tx = mcx - tongue_w / 2;
        int ty = mcy + 4;
        draw_rounded_rect(layer, tx, ty, tongue_w, tongue_h, tongue_h / 2, s_ctx.fg_color);
    }
}

/*==================== 装饰器绘制 ====================*/

/* 装饰器绘制区域（右上角 40x40） */
#define DECO_X  265
#define DECO_Y  10
#define DECO_SIZE 40

/**
 * @brief 推进单个合法装饰器槽的动画，并绘制心形、怒筋、汗滴、竖线或气泡。
 * @param[in,out] layer 当前借用绘制层。
 * @param[in] slot 装饰器槽下标，越界或 NONE 时忽略。
 * @param[in] interval_ms 本轮经过的毫秒数。
 */
static void deco_update_and_draw(lv_layer_t *layer, int slot, int interval_ms)
{
    if (slot < 0 || slot >= DECO_SLOT_MAX) return;
    if (s_deco[slot].type == FACE_DECO_NONE) return;

    s_deco[slot].time_ms += interval_ms;
    int t = s_deco[slot].time_ms;
    int dx = (slot == 0) ? DECO_X : 15;  /* slot 0 右上，slot 1 左上 */
    int dy = DECO_Y;

    switch (s_deco[slot].type) {

    case FACE_DECO_HEART: {
        /* 心形：2个圆+三角形，脉动缩放 */
        int pulse = sin100((int)(TWO_PI_MILLI * (t % 1000) / 1000));
        int scale = 100 + pulse / 4;  /* 75~125% */
        int r = 6 * scale / 100;
        int cx = dx + DECO_SIZE / 2;
        int cy = dy + DECO_SIZE / 2;
        /* 左上圆 */
        draw_filled_circle(layer, cx - r + 1, cy - r / 2, r, s_ctx.fg_color);
        /* 右上圆 */
        draw_filled_circle(layer, cx + r - 1, cy - r / 2, r, s_ctx.fg_color);
        /* 下三角 */
        draw_triangle(layer,
            cx - 2 * r + 1, cy,
            cx + 2 * r - 1, cy,
            cx, cy + 2 * r,
            s_ctx.fg_color);
        break;
    }

    case FACE_DECO_ANGRY_MARK: {
        /* 十字怒筋：# 交叉线 + 脉动 */
        int pulse = sin100((int)(TWO_PI_MILLI * (t % 800) / 800));
        int offset = pulse / 50;  /* ±2 */
        int cx = dx + DECO_SIZE / 2;
        int cy = dy + DECO_SIZE / 2;
        int s = 10 + offset;
        /* 四条交叉弧线 */
        draw_line(layer, cx - s, cy - 2, cx + 2, cy + s, 2, s_ctx.fg_color);
        draw_line(layer, cx + s, cy - 2, cx - 2, cy + s, 2, s_ctx.fg_color);
        draw_line(layer, cx - s, cy + 2, cx + 2, cy - s, 2, s_ctx.fg_color);
        draw_line(layer, cx + s, cy + 2, cx - 2, cy - s, 2, s_ctx.fg_color);
        break;
    }

    case FACE_DECO_SWEAT: {
        /* 汗滴：圆+三角，循环下落 */
        int period = 3000;
        int phase = t % period;
        int fall_y = phase * 20 / period;  /* 下落 0~20 像素 */
        int cx = dx + DECO_SIZE / 2;
        int cy = dy + 8 + fall_y;
        int r = 4;
        /* 水滴形：上三角+下圆 */
        draw_triangle(layer, cx - 3, cy, cx + 3, cy, cx, cy - 8, s_ctx.fg_color);
        draw_filled_circle(layer, cx, cy + 1, r, s_ctx.fg_color);
        break;
    }

    case FACE_DECO_PALE: {
        /* 竖线组 ||| 下滑 */
        int period = 3000;
        int phase = t % period;
        int fall_y = phase * 15 / period;
        int bx = dx + 10;
        int b_y = dy + 5 + fall_y;
        draw_line(layer, bx,      b_y, bx,      b_y + 15, 2, s_ctx.fg_color);
        draw_line(layer, bx + 8,  b_y + 3, bx + 8,  b_y + 18, 2, s_ctx.fg_color);
        draw_line(layer, bx + 16, b_y, bx + 16, b_y + 12, 2, s_ctx.fg_color);
        break;
    }

    case FACE_DECO_BUBBLE: {
        /* 浮动气泡：4个圆上浮 */
        for (int i = 0; i < 4; i++) {
            int speed = 10 + s_deco[slot].pr[i] * 2;
            s_deco[slot].py[i] += speed * interval_ms / 1000;
            if (s_deco[slot].py[i] > DECO_SIZE) {
                s_deco[slot].py[i] = 0;
                s_deco[slot].px[i] = rand_between(5, DECO_SIZE - 5);
                s_deco[slot].pr[i] = rand_between(2, 5);
            }
            int bx2 = dx + s_deco[slot].px[i];
            int by2 = dy + DECO_SIZE - s_deco[slot].py[i];
            /* 空心圆效果：画圆弧 */
            draw_arc_stroke(layer, bx2, by2, s_deco[slot].pr[i], 0, 360, 1, s_ctx.fg_color);
        }
        break;
    }

    default:
        break;
    }
}

/**
 * @brief 清空 Canvas 背景，绘制当前脸型及装饰器后提交完整绘制层。
 * @param[in] interval_ms 本轮装饰器动画推进毫秒数，首次创建为零。
 * @note 只有 Canvas 已存在时操作；静态像素缓冲始终由本模块持有。
 */

static void render_face(int interval_ms)
{
    if (!s_canvas) return;

    lv_canvas_fill_bg(s_canvas, s_ctx.bg_color, LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);

    if (s_face_type == FACE_TYPE_DOG) {
        /* 狗脸：眼睛 + 眉毛 + 鼻子 + 弧线嘴 + 眼睑 */
        draw_eye_simple(&layer, 0);
        draw_eye_simple(&layer, 1);
        draw_eyebrow(&layer, 0);
        draw_eyebrow(&layer, 1);
        draw_nose(&layer);
        draw_mouth_dog(&layer);
        draw_eyelid_simple(&layer, 0);
        draw_eyelid_simple(&layer, 1);
    } else {
        /* 简单脸 */
        draw_eye_simple(&layer, 0);
        draw_eye_simple(&layer, 1);
        draw_mouth_simple(&layer);
        draw_eyelid_simple(&layer, 0);
        draw_eyelid_simple(&layer, 1);
    }

    /* 装饰器（叠加绘制） */
    for (int i = 0; i < DECO_SLOT_MAX; i++) {
        deco_update_and_draw(&layer, i, interval_ms);
    }

    lv_canvas_finish_layer(s_canvas, &layer);
}

/**
 * @brief 重置静态动画上下文并创建使用静态 RGB565 缓冲的单实例 Canvas。
 */

lv_obj_t *face_engine_create(lv_obj_t *parent)
{
    printf(LOG_TAG "Creating face engine v3 (%dx%d)\n", FACE_CANVAS_W, FACE_CANVAS_H);

    memset(&s_ctx, 0, sizeof(s_ctx));
    memset(&s_last_ctx, 0, sizeof(s_last_ctx));
    memset(&s_deco, 0, sizeof(s_deco));
    memset(&s_shiver, 0, sizeof(s_shiver));

    s_ctx.eyes[0].open = 100;
    s_ctx.eyes[1].open = 100;
    s_ctx.mouth_open = 0;
    s_ctx.breath_offset = 0;
    s_ctx.emotion = FACE_EMOTION_NEUTRAL;
    s_ctx.fg_color = lv_color_white();
    s_ctx.bg_color = lv_color_black();
    s_face_type = FACE_TYPE_SIMPLE;

    srand((unsigned int)lv_tick_get());
    blink_init();
    breath_init();
    saccade_init();
    mouth_init();
    emotion_init();
    smooth_init();

    s_canvas = lv_canvas_create(parent);
    if (!s_canvas) {
        printf(LOG_TAG "Failed to create canvas!\n");
        return NULL;
    }

    lv_canvas_set_buffer(s_canvas, s_canvas_buf, FACE_CANVAS_W, FACE_CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(s_canvas, LV_ALIGN_TOP_MID, 0, 0);

    render_face(0);
    printf(LOG_TAG "Face engine v3 created OK\n");
    return s_canvas;
}

/**
 * @brief 依次推进情绪、眨眼、呼吸、微视、颤抖及嘴巴，再插值并绘制完整帧。
 */
void face_engine_update(uint32_t interval_ms)
{
    if (!s_canvas) return;

    s_ctx.eyes[0].open = 100;
    s_ctx.eyes[1].open = 100;

    emotion_update(interval_ms);
    blink_update(interval_ms);
    breath_update(interval_ms);
    saccade_update(interval_ms);
    shiver_update(interval_ms);
    mouth_update(interval_ms);

    smooth_update();
    render_face(interval_ms);
}

/**
 * @brief 设定目标情绪及衰减期限，并同步自动装饰器。
 */
void face_engine_set_emotion(face_emotion_t emotion)
{
    if (emotion >= FACE_EMOTION_COUNT) return;
    s_emotion.target = emotion;
    s_emotion.decay_timer = EMOTION_DECAY_TIME;
    s_emotion.decaying = 0;
    emotion_auto_deco(emotion);
}

/**
 * @brief 切换嘴巴动画状态并重置相位及开合标志，相同状态不操作。
 */
void face_engine_set_mouth_state(mouth_state_t state)
{
    if (s_mouth.state == state) return;
    s_mouth.state = state;
    s_mouth.time_ms = 0;
    s_mouth.toggle_timer = 0;
    s_mouth.is_open = 0;
}

/**
 * @brief 启动 SPEAKING_DURATION 毫秒的随机开合嘴巴动画，随后自动回空闲。
 */
void face_engine_start_speaking(void)
{
    s_mouth.state = MOUTH_STATE_SPEAKING;
    s_mouth.time_ms = 0;
    s_mouth.remaining_ms = SPEAKING_DURATION;
    s_mouth.toggle_timer = 0;
    s_mouth.is_open = 1;
}

/**
 * @brief 设置下次绘制使用的前景及背景色。
 */
void face_engine_set_theme(lv_color_t fg, lv_color_t bg)
{
    s_ctx.fg_color = fg;
    s_ctx.bg_color = bg;
}

/**
 * @brief 设置一个装饰器槽并重置该槽动画及粒子位置。
 */
void face_engine_set_decorator(int slot, face_deco_t deco)
{
    if (slot < 0 || slot >= DECO_SLOT_MAX) return;
    if (deco >= FACE_DECO_COUNT) return;
    s_deco[slot].type = deco;
    s_deco[slot].time_ms = 0;
    for (int i = 0; i < 4; i++) {
        s_deco[slot].px[i] = rand_between(5, 35);
        s_deco[slot].py[i] = rand_between(0, 30);
        s_deco[slot].pr[i] = rand_between(2, 6);
    }
}

/**
 * @brief 清除两个装饰器槽及其累计时间，后续情绪变化仍可重新装配槽零。
 */
void face_engine_clear_decorators(void)
{
    for (int i = 0; i < DECO_SLOT_MAX; i++) {
        s_deco[i].type = FACE_DECO_NONE;
        s_deco[i].time_ms = 0;
    }
}

/**
 * @brief 保存脸型选择，后续绘制时 DOG 使用狗脸，其余使用简单脸。
 */
void face_engine_set_face_type(face_type_t type)
{
    s_face_type = type;
}

/**
 * @brief 只读借用当前静态表情上下文。
 */
const face_context_t *face_engine_get_context(void)
{
    return &s_ctx;
}

#endif /* D_FEATURE_AI_CHAT && D_LVGL_ENABLE */
