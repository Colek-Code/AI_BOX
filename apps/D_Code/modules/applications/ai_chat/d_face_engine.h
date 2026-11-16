/**
 * @file d_face_engine.h
 * @brief 表情渲染引擎 - 移植自 Stack-chan + 参考 RoboEyes
 *        支持平滑插值、情绪衰减、嘴巴动画状态机、说话动画
 *        使用 LVGL Canvas 绘制，纯 C 实现
 */

#ifndef __D_FACE_ENGINE_H__
#define __D_FACE_ENGINE_H__

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "lvgl.h"

/*==================== 配置宏 ====================*/

/* 表情画布尺寸（像素） */
#define FACE_CANVAS_W       320
#define FACE_CANVAS_H       200

/* 动画帧率（毫秒/帧） */
#define FACE_FRAME_INTERVAL 100     /* 每秒十帧的参考周期；调用方自行安排更新节奏。 */

/* 眨眼参数（毫秒） */
#define BLINK_OPEN_MIN      400
#define BLINK_OPEN_MAX      5000
#define BLINK_CLOSE_MIN     200
#define BLINK_CLOSE_MAX     400

/* 呼吸周期（毫秒） */
#define BREATH_DURATION     6000

/* 微视参数 */
#define SACCADE_UPDATE_MIN  300
#define SACCADE_UPDATE_MAX  2000
#define SACCADE_GAIN        3       /* 像素偏移量 */

/* 平滑插值速度（0~100，每帧移动百分比） */
#define LERP_SPEED_FAST     40      /* 快速过渡（眼睛大小、嘴巴） */
#define LERP_SPEED_NORMAL   25      /* 正常过渡（眼睛位置） */
#define LERP_SPEED_SLOW     12      /* 慢速过渡（情绪衰减） */

/* 情绪自动衰减时间（毫秒） */
#define EMOTION_DECAY_TIME  5000

/* 说话动画持续时间（毫秒） */
#define SPEAKING_DURATION   2000
#define SPEAKING_TOGGLE_MS  150     /* 嘴巴开合切换间隔 */

/* 思考动画周期（毫秒） */
#define THINKING_PERIOD     2000

/* 眼睛布局（匹配 simple-face.ts: left 90,93 / right 230,96） */
#define EYE_LEFT_CX          90
#define EYE_LEFT_CY          93
#define EYE_RIGHT_CX        230
#define EYE_RIGHT_CY         96
#define EYE_RADIUS            8      /* simple face 眼睛半径 */
#define DOG_EYE_RADIUS       10      /* dog face 眼睛半径（比 simple 大） */
#define EYELID_W             24
#define EYELID_H             24

/* 嘴巴布局（匹配 simple-face.ts: cx=160, cy=148; dog-face: cy=120） */
#define MOUTH_CX            160
#define MOUTH_CY            148      /* simple face 嘴巴Y */
#define DOG_MOUTH_CY        120      /* dog face 嘴巴Y（上移，给鼻子留空间） */
#define MOUTH_MIN_W          50
#define MOUTH_MAX_W          90
#define MOUTH_MIN_H           8      /* 匹配 TS minHeight=8 */
#define MOUTH_MAX_H          58

/*==================== 数据结构 ====================*/

/* 情绪枚举（参考 Stack-chan 定义的 8 种情绪） */
typedef enum {
    FACE_EMOTION_NEUTRAL = 0,
    FACE_EMOTION_ANGRY,
    FACE_EMOTION_SAD,
    FACE_EMOTION_HAPPY,
    FACE_EMOTION_SLEEPY,
    FACE_EMOTION_DOUBTFUL,  /* 疑惑：不对称眼睛 */
    FACE_EMOTION_COLD,      /* 寒冷：缩小+颤抖 */
    FACE_EMOTION_HOT,       /* 炎热：张大+快速呼吸 */
    FACE_EMOTION_COUNT,
} face_emotion_t;

/* 装饰器类型（移植自 decorator.ts） */
typedef enum {
    FACE_DECO_NONE = 0,
    FACE_DECO_HEART,        /* ❤️ 脉动心形 */
    FACE_DECO_ANGRY_MARK,   /* 💢 十字怒筋 */
    FACE_DECO_SWEAT,        /* 💧 汗滴下落 */
    FACE_DECO_PALE,         /* ||| 竞线下滑 */
    FACE_DECO_BUBBLE,       /* ○ 浮动气泡 */
    FACE_DECO_COUNT,
} face_deco_t;

/* 脸型（simple-face vs dog-face） */
typedef enum {
    FACE_TYPE_SIMPLE = 0,   /* 简单脸（默认） */
    FACE_TYPE_DOG,          /* 狗脸（有眉毛、弧形嘴、鼻子） */
} face_type_t;

/* 嘴巴动画状态 */
typedef enum {
    MOUTH_STATE_IDLE = 0,   /* 空闲：微小呼吸联动 */
    MOUTH_STATE_THINKING,   /* 思考：缓慢脉动 */
    MOUTH_STATE_SPEAKING,   /* 说话：快速随机开合 */
} mouth_state_t;

/* 单眼状态 */
typedef struct {
    int open;       /* 0~100，100=完全睁开 */
    int gaze_x;     /* 注视偏移X，像素 */
    int gaze_y;     /* 注视偏移Y，像素 */
} face_eye_t;

/* 表情上下文（整体状态） */
typedef struct {
    face_eye_t  eyes[2];        /* 0 为左眼，1 为右眼。 */
    int         mouth_open;     /* 0~100，0=闭嘴 */
    int         breath_offset;  /* 呼吸Y偏移，像素 */
    face_emotion_t emotion;
    lv_color_t  fg_color;       /* 前景色（眼睛/嘴巴） */
    lv_color_t  bg_color;       /* 背景色 */
} face_context_t;

/*==================== 公共接口 ====================*/

/**
 * @brief 重置静态动画上下文并创建使用静态 RGB565 缓冲的单实例 Canvas。
 * @param[in,out] parent 调用方提供的有效 LVGL 父对象。
 * @return 成功返回父对象拥有的 Canvas，创建失败为 NULL。
 * @note 仅 LVGL 线程使用；当前无重复创建清理或父对象删除失效监听，不能把静态句柄当作可独立释放的引擎实例。保留 Stack-chan 模型及 RoboEyes 参考归属。
 */
lv_obj_t *face_engine_create(lv_obj_t *parent);

/**
 * @brief 依次推进情绪、眨眼、呼吸、微视、颤抖及嘴巴，再插值并绘制完整帧。
 * @param[in] interval_ms 本轮经过的毫秒数，调用方须按 UI 动画节奏提供可由内部整数计数表示的间隔。
 * @note Canvas 尚未创建时忽略；本函数不自行按 FACE_FRAME_INTERVAL 限频，也不提供跨线程互斥。
 */
void face_engine_update(uint32_t interval_ms);

/**
 * @brief 设定目标情绪及衰减期限，并同步自动装饰器。
 * @param[in] emotion 合法情绪枚举；达到 COUNT 的值忽略，调用方仍须避免负枚举。
 * @note 非中性目标在后续 update 累计 EMOTION_DECAY_TIME 后恢复中性，不在调用时立即完成画面插值。
 */
void face_engine_set_emotion(face_emotion_t emotion);

/**
 * @brief 切换嘴巴动画状态并重置相位及开合标志，相同状态不操作。
 * @param[in] state 调用方保证合法的嘴巴状态。
 * @note 本接口不设置说话剩余期限；完整有限说话动画须调用 start_speaking。
 */
void face_engine_set_mouth_state(mouth_state_t state);

/**
 * @brief 启动 SPEAKING_DURATION 毫秒的随机开合嘴巴动画，随后自动回空闲。
 */
void face_engine_start_speaking(void);

/**
 * @brief 设置下次绘制使用的前景及背景色。
 * @param[in] fg 眼睛、嘴巴及装饰器前景色。
 * @param[in] bg 画布背景及眼睑遮挡色。
 */
void face_engine_set_theme(lv_color_t fg, lv_color_t bg);

/**
 * @brief 设置一个装饰器槽并重置该槽动画及粒子位置。
 * @param[in] slot 0 或 1 的槽下标，越界忽略。
 * @param[in] deco 合法装饰器枚举，NONE 关闭；调用方避免负值。
 * @note 槽零也由情绪自动管理，下一次情绪变化可能覆盖；槽一留给手动控制。
 */
void face_engine_set_decorator(int slot, face_deco_t deco);

/**
 * @brief 清除两个装饰器槽及其累计时间，后续情绪变化仍可重新装配槽零。
 */
void face_engine_clear_decorators(void);

/**
 * @brief 保存脸型选择，后续绘制时 DOG 使用狗脸，其余使用简单脸。
 * @param[in] type 期望使用的脸型枚举。
 */
void face_engine_set_face_type(face_type_t type);

/**
 * @brief 只读借用当前静态表情上下文。
 * @return 内部静态上下文指针，不由调用方修改或释放；后续 update 或设置操作可改变内容。
 */
const face_context_t *face_engine_get_context(void);

#endif /* D_LVGL_ENABLE */
#endif /* __D_FACE_ENGINE_H__ */
