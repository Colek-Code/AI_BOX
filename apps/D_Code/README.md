# D_Code 开发指南

适用 AC791N SDK 1.2.14、pi32v2/r3 工具链，默认 GC9307/HYN 板和 Product UI。

## 配置

| 文件 | 用途 |
| --- | --- |
| [board_config.h](config/board_config.h) | 板型、产品默认方向 |
| [app_config.h](config/app_config.h) | 功能开关和根界面 |
| [deployment_config.h](config/deployment_config.h) | Wi-Fi 和可选平台参数 |
| [qwen_config.h](config/qwen_config.h) | 接入模式、激活来源、设备名和对话参数 |
| [ota_config.h](config/ota_config.h) | OTA 地址、版本和数据保留设置 |
| [runtime_config.h](config/runtime_config.h) | 日志与运行选项 |

先选择板型和功能，再填写网络、Qwen 及 OTA 参数，修改后重新编译。
GC9307/HYN 的 Product UI 支持 `D_SCREEN_DEFAULT_ROTATION_DEGREES` 为 `0` 或 `180`，已保存的用户方向优先。

BOX 的 PB2 由 [d_board.c](board/wl82/d_board.c) 初始化为低电平；DAC 自动 MUTE 关闭，
[d_sdk_audio_config.h](config/internal/sdk/d_sdk_audio_config.h) 中 MUTE 端口为 `0xff`。

## 编译与烧录

Windows 工具链默认目录为 `C:/JL/pi32/bin`。在工程根目录运行：

```powershell
.\.vscode\winmk.bat ac791n_D_Code_elf
```

VS Code 提供同名任务：

| 任务 | 行为 |
| --- | --- |
| `ac791n_D_Code_elf` | 只编译、链接 |
| `ac791n_D_Code_keep_vm` | 编译并烧录，保留 VM |
| `ac791n_D_Code` | 编译并烧录，清除 VM |

默认 ELF 输出为 `cpu/wl82/tools/sdk.elf`。保留设备设置时选择 `keep_vm`；无参数 `make` 和 `all` 会进入 SDK 下载流程。

### 本地烧录输入

- **Key**：工程附带 `cpu/wl82/tools/YT-BOX.key`，`download.c` 的 `KEY_FILE` 默认引用它；更换芯片授权时使用与目标匹配的 Key。
- **出厂记录**：启用出厂设备名，或半托管使用 PROGRAMMER 来源时，读取可选的 `cpu/wl82/tools/activation-code.txt`；存在时校验并写入，缺失时不追加写码。空文件、格式错误或读取失败会报错。
- **格式**：使用 UTF-8 单行文本，兼容旧版 16 字符平台码和 `Q1|<16字符激活码>|<设备名>`；新设备名允许 1～32 个字母、数字、下划线或连字符。

出厂记录和网络凭据保存在本地。`keep_vm` 控制 VM，出厂记录由独立烧录输入控制。

## 接入与功能

Qwen 普通编译直接链接 [libqwen_access.a](modules/integrations/qwen/lib/libqwen_access.a)。
在 `qwen_config.h` 选择接入模式，默认半托管。两种模式均须[联系销售开通 License 并确认应用配置](../../README.md#服务开通)。半托管使用匹配的出厂身份向拓达取票；全托管使用已完成云端配置的 AppId 和该设备的注册凭据直接向阿里取票。两种模式的固件均不携带长期模型 API Key。
默认配置使用出厂身份，首次接入需写入有效记录，或按实际接入方式调整激活来源和设备名策略。
接口说明见 [Qwen 接入](modules/integrations/qwen/README.md)。

本地音乐使用 FAT32 TF 卡，支持 MP3/WAV；插卡后扫描，选曲后播放，离页和熄屏继续播放。
对话或闹钟结束后需手动恢复音乐。引脚和接口见 [本地音乐](modules/services/local_music/README.md)。

## OTA

在 `ota_config.h` 配置 HTTPS 发布地址和版本，通过 [D_code_OTA.bat](D_code_OTA.bat) 生成升级包。
该入口会调用含有线下载的 SDK 流程，先准备匹配 Key 并核对提示中的 VM 策略；按提示确认后在菜单选择 `2` 生成 OTA 文件。
OTA 的 `D_OTA_KEEP_VM` 与有线下载的 `keep_vm` 分别配置。

## 代码组织

```text
D_Code/
├─ app/         启动与服务装配
├─ board/       板级初始化
├─ config/      配置与解析
├─ framework/   页面、事件、LVGL 公共机制
├─ platform/    显示、触摸、音量和存储适配
├─ modules/
│  ├─ applications/  产品界面与应用
│  ├─ services/      音乐、网络、OTA、电源等服务
│  ├─ integrations/  Qwen、小乐等集成
│  └─ diagnostics/   可选诊断模块
├─ build/       构建与打包脚本
└─ tools/       开发与分发工具
```

界面通过服务／平台接口访问设备，服务自行管理任务和资源。
新增固件源文件登记到 [d_firmware_sources.def](build/d_firmware_sources.def)。
持久化索引已分配：Wi-Fi 24/25、外观 26、番茄钟 27/28、闹钟 29/30、Qwen 16/31/32/33；新增数据前核对占用。

普通编译使用已生成资源。资源修改与工具依赖见 [界面资源](modules/applications/product_ui/assets/README.md)。
