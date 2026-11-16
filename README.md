# AI BOX

基于杰理 AC791N SDK 1.2.14 的触屏语音终端固件，使用 C 和 LVGL 开发。
应用源码位于 [apps/D_Code](apps/D_Code/README.md)，工程包含 SDK、预编译库和界面资源。

## 功能

- 分页桌面、下拉控制中心、设置和 0°／180° 屏幕翻转。
- Qwen 语音对话、字幕和小乐离线唤醒。
- TF 本地音乐列表、播放／暂停、切歌及音频交接。
- 日历、日程、本地闹钟和番茄钟。
- 相机预览、计算器和 HTTPS OTA。

## 服务开通

**OTA 升级服务器**和 **Qwen 会话 License** 均需联系我司销售，取得对应使用资格后方可开通使用。

## 默认硬件

| 项目 | 配置 |
| --- | --- |
| 芯片 | AC791N |
| Flash / SDRAM | 16 MB / 8 MB |
| 显示与触摸 | GC9307 / HYN，320 × 240 逻辑界面 |
| 本地存储 | TF 卡，FAT32 |
| 功放 MUTE | PB2，输出低电平，关闭 DAC 自动 MUTE |

板型和默认方向在 [board_config.h](apps/D_Code/config/board_config.h) 中选择。

## 获取源码

安装 Git 和 Git LFS 后执行：

```powershell
git lfs install
git clone https://github.com/Colek-Code/AI_BOX.git
cd AI_BOX
git lfs pull
```

SDK 归档和字体使用 Git LFS，编译前需下载实际文件。

## 编译与烧录

Windows 下安装匹配 SDK 的杰理 pi32v2/r3 工具链，默认目录为 `C:/JL/pi32/bin`。
用 VS Code 打开工程并运行构建任务，默认任务只编译和链接。

| 任务 | 行为 |
| --- | --- |
| `ac791n_D_Code_elf` | 编译、链接，生成 ELF |
| `ac791n_D_Code_keep_vm` | 编译并烧录，保留 VM |
| `ac791n_D_Code` | 编译并烧录，清除 VM |

在工程根目录使用 PowerShell 编译：

```powershell
.\.vscode\winmk.bat ac791n_D_Code_elf
```

工程附带 `YT-BOX.key`，烧录时须与目标芯片匹配。配置、出厂身份和 OTA 见 [D_Code 开发指南](apps/D_Code/README.md)。
芯片样品及下载支持入口：[样品渠道](https://item.taobao.com/item.htm?ft=t&id=1008161308862)。

## 代码入口

| 目录 | 内容 |
| --- | --- |
| `apps/D_Code/app` | 启动与服务装配 |
| `apps/D_Code/config` | 板型、功能、接入和运行配置 |
| `apps/D_Code/framework` | 页面、事件和 LVGL 公共机制 |
| `apps/D_Code/board`、`platform` | 板级和驱动适配 |
| `apps/D_Code/modules` | 应用、服务和集成模块 |
| `cpu`、`include_lib`、`lib` | SDK 与预编译依赖 |

模块说明：[Qwen](apps/D_Code/modules/integrations/qwen/README.md) · [本地音乐](apps/D_Code/modules/services/local_music/README.md) · [界面资源](apps/D_Code/modules/applications/product_ui/assets/README.md)。

项目许可证见 [LICENSE](LICENSE)。SDK、字体和第三方库沿用各自许可证；历法来源见 [NOTICE](apps/D_Code/modules/applications/product_ui/features/time/calendar_data/NOTICE.md)。
