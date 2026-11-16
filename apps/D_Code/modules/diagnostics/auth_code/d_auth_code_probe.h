/** @file d_auth_code_probe.h @brief 仅启动时运行的授权码合成标记诊断。 */
#ifndef D_AUTH_CODE_PROBE_H
#define D_AUTH_CODE_PROBE_H
/**
 * @brief 读取授权码并与两个合成标记比较，只记录诊断与匹配分类。
 * @note app_main 启动时调用一次；不写存储、不把读取内容或合成标记发往云端，结束后清空工作缓冲。
 */
void d_auth_code_probe_run(void);
#endif
