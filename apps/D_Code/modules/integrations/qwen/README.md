# Qwen 接入

本模块使用 [libqwen_access.a](lib/libqwen_access.a)，适配 AC791N SDK 1.2.14、pi32v2/r3。
普通编译直接链接；公开接口与归档由 [binding](lib/libqwen_access.binding.json) 绑定。

## 配置

在 [qwen_config.h](../../../config/qwen_config.h) 设置接入模式、激活码来源、设备名及对话参数。
修改公开选项后重新编译主固件，接入库保持同一份；库内接入配置变更需使用对应的新库。

| 选项 | 用途 |
| --- | --- |
| 全托管 | 使用匹配的阿里设备身份接入 |
| 半托管 | 先激活平台，再完成设备注册、取 Token 和 WSS 连接 |
| UI / FACTORY / STATIC / PROGRAMMER | 分别为界面输入、厂测 VM、库内常量、烧录 Flash 记录，不会自动替换来源 |
| 出厂名 / MAC / 固定名 | 选择设备身份，须与云端注册模式匹配 |

## 出厂身份

默认使用出厂设备名。平台校验 Flash 原始记录的长度和 CRC，库解析业务字段。

| 记录 | 行为 |
| --- | --- |
| `Q1|<16字符激活码>|<设备名>` | 使用记录设备名，长度 1～32，允许字母、数字、下划线和连字符 |
| 合法旧版 16 字符单码 | 使用 MAC／固定名策略 |
| 缺失、损坏或未知格式 | 返回身份错误，停止依赖该身份的接入 |

Flash 记录读取适配 16 MB GC9307/HYN 板。全托管可使用出厂名，不会因此执行平台激活。
启用出厂设备名，或半托管使用 PROGRAMMER 来源时，PC 接入可选的 `cpu/wl82/tools/activation-code.txt`；存在时校验并写入，缺失时不追加写码。
一拖八业务记录可用 `apps/D_Code/tools/prepare_factory_records.ps1` 准备，再交原厂工具转换、部署。

## 接口

- `qwen_access_configure_product()`：产品配置装配入口。
- `qwen_access_configure(options, hooks)`：复制配置和回调，只允许一次成功初始化。
- `qwen_access_get_device_name(out, capacity)`：复制已准备的设备名，不触发 Flash 或网络操作。
- `qwen_access_identity_error()`：查询身份准备结果。

完整签名与返回合同见 [qwen_access.h](qwen_access.h)。认证、缓存和清理由 Qwen 工作任务驱动，
会话与音频使用服务接口。切换固件模式不会迁移云端同名设备。
