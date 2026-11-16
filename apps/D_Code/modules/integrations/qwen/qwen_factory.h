/** @file qwen_factory.h @brief 独立激活码工厂写入固件入口。 */
#ifndef QWEN_FACTORY_H
#define QWEN_FACTORY_H
/**
 * @brief 配置产品凭据与激活存储后进入原厂产测工具主循环。
 * @note 该固件仅提供激活码用户选项，不执行云端 I/O；存储初始化失败则返回。
 */
void qwen_factory_start(void);
#endif
