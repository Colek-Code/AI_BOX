#ifndef LX_AEC_LIMIT_H
#define LX_AEC_LIMIT_H
/*
 * 注意：MIC 和 ref 数据需要为 PCM 格式，采样率为 16K。
 */

/*
 * AEC 初始化函数
 * @param mpo_dp 默认值为 6
 * @param callback 输出算法后的音频数据
 *              回调函数签名：void Callback(void *data, int len, int type)
 *              - data: 算法后的音频数据
 *              - len: 数据长度
 *              - type: 识别指令（0 表示未识别，>= 1 表示识别结果）
 * @return 初始化结果
 */
int lx_aec_init(int mpo_dp, void *callback);

/*
 * AEC 释放函数
 * @return 释放结果
 */
int lx_aec_free(void);

/*
 * 原始 MIC 麦克风数据输入
 * @param buffer 音频数据缓冲区
 * @param length 数据长度
 * @return 处理结果
 */
int lx_aec_mic_data(char *buffer, int length);

/*
 * 原始 ref 回声数据输入
 * @param buffer 音频数据缓冲区
 * @param length 数据长度
 * @return 处理结果
 */
int lx_aec_ref_data(char *buffer, int length);

#endif // LX_AEC_LIMIT_H
