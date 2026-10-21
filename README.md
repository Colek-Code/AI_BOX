# AI_BOX

AI BOX 是基于杰理 AC791N 的开源固件项目，集成触控界面、Qwen 语音对话、
万年历、番茄钟、闹钟和相机预览，支持通过 Wi-Fi 进行 OTA 升级。

仓库包含应用源码、配套 SDK 和构建工具，主程序位于 [apps/D_Code](apps/D_Code/)。

## 功能

| 功能 | 说明 |
| --- | --- |
| 触控界面 | 桌面分页、下拉控制中心、系统设置、壁纸和亮度调节 |
| 语音对话 | Qwen 语音交互、流式字幕、小乐本地唤醒和音量控制 |
| 万年历 | 1900—2099 年公农历、节气、节日和年月查询 |
| 番茄钟 | 专注与休息计时、自动切换、完成统计和本地提示音 |
| 本地闹钟 | 最多 16 条，支持单休、双休、大小周及 5 分钟稍后提醒 |
| 计算器 | 基础四则运算 |
| 相机 | 实时画面预览 |
| 系统升级 | HTTPS OTA 在线升级 |

音乐、相册和日程表仍在开发，AI 语音创建本地闹钟尚未接入。
手动校时仅在本次开机期间有效，本地闹钟需要设备保持开机。

## 硬件

开发板资料：[AC7916A 开源板](https://qcnvkam9afij.feishu.cn/docx/FiuxdDPiYoK0y3xO6wkcqHatnpe)。

当前工程默认配置：

| 项目 | 配置 |
| --- | --- |
| 芯片系列 | 杰理 AC791N |
| Flash | 16 MB |
| SDRAM | 8 MB |
| 屏幕 | GC9307，逻辑分辨率 320 × 240 |
| 触摸 | HYN / CST8xxT |

其他板型的配置方式见 [使用说明](apps/D_Code/README.md)。

## 下载

直接下载 [完整源码 ZIP](https://github.com/Colek-Code/AI_BOX/archive/refs/heads/main.zip)，
解压到较短的英文路径即可。

也可以安装 Git 和 Git LFS 后克隆：

```powershell
git lfs install
git clone https://github.com/Colek-Code/AI_BOX.git
cd AI_BOX
git lfs pull
```

## 快速开始

1. 安装杰理 Windows 开发工具链和 Python 3.10 或更新版本。
2. 在 [board_config.h](apps/D_Code/config/board_config.h) 中选择板型。
3. 在 [app_config.h](apps/D_Code/config/app_config.h) 中选择界面和功能。
4. 在 [deployment_config.h](apps/D_Code/config/deployment_config.h) 和 [qwen_config.h](apps/D_Code/config/qwen_config.h) 中填写自己的 Wi-Fi 与平台参数。
5. 按 [使用说明](apps/D_Code/README.md#编译与烧录) 编译和烧录。

`ac791n_D_Code` 编译并烧录时擦除 VM；`ac791n_D_Code_keep_vm` 保留 VM。

## 文档

- [配置、编译、烧录与 OTA](apps/D_Code/README.md)
- [Product UI 资源说明](apps/D_Code/modules/applications/product_ui/assets/README.md)
- [历法数据来源与许可](apps/D_Code/modules/applications/product_ui/features/time/calendar_data/NOTICE.md)
- [开发板与 SDK 文档](https://doc.yunthinker.com/#/docs/AC791N/%E5%BC%80%E5%8F%91%E6%9D%BF%E4%BB%8B%E7%BB%8D/1)

## 社区

技术交流QQ群：593243414

## 许可与致谢

项目许可证见 [LICENSE](LICENSE)。第三方代码、字体和二进制库遵循各自的许可证与授权条件。

感谢杰理 SDK 及 [AC791N 开源工程](https://gitcode.com/yunthinker/fw-AC791N_SDK) 提供的基础支持。

## 芯片与下载支持

请通过AC791N [系列芯片样品购买](https://item.taobao.com/item.htm?ft=t&id=1008161308862) 渠道购买样品，其他渠道芯片可能会出现key不一致报错，无法下载程序等问题，且无法提供相关
技术支持。
