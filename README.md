# AI_BOX

AI BOX 成品项目的开源固件工程。
初始源码基线来自 [AC791N 开源工程](https://gitcode.com/yunthinker/fw-AC791N_SDK)
的 `6e32a97f8952ffc8568258b3f94b1bda8ca2dc20` 版本，在本仓库独立维护。
当前导入保留该基线的硬件配置；成品板适配以本仓库后续配置和验证为准。

基于杰理 AC791N SDK 的模块化固件工程，集成触控桌面、Qwen 语音对话、
万年历、番茄钟、本地闹钟、相机预览和 HTTPS 双备份 OTA。

源码位于 `apps/D_Code/`，按应用、后台服务和硬件适配组织，支持通过配置选择板型、界面和功能组合。
仓库保留配套 SDK 和官方例程。

## 获取工程

先安装 Git 和 Git LFS，再获取源码及大文件：

```powershell
git lfs install
git clone https://github.com/Colek-Code/AI_BOX.git
cd AI_BOX
git lfs pull
```

部分 SDK 库、字体和预编译文件使用 Git LFS 保存。确认 `git lfs pull` 成功后再编译，
避免把 LFS 指针文件作为库或资源使用。编译工具链与硬件要求见下文。

## 凭据与公开发布

公开配置保留 Wi-Fi、Qwen 和其他平台的配置项，账户凭据使用空值或明确占位值。
使用联网功能前，需配置自己的网络与平台凭据。TLS 根证书、协议字段名和必要资源许可保留。

发布工具 `apps/D_Code/tools/public_release.py` 从本地文件生成单独的脱敏 Git 候选，
不回写本地凭据。发布前应核对脱敏报告和候选树；普通 `git add` 不会自动脱敏。
应用固件、链接中间产物和授权密钥文件不进入公开清单，需在本地配置凭据后重新构建。
SDK 库、编译工具和烧录入口仍随工程保留；若使用需要独立授权文件的芯片或下载配置，
应自行提供对应文件。

本机发布入口会安装仓库级 `pre-push` 检查，在上传 LFS 对象前核对待推送历史中的凭据、
构建产物和 GitHub 发布账户。脱敏提交后，本地配置可能显示为未提交修改，这是本地值
与公开占位值的差异；不要直接把这些差异加入公开提交。

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
| 系统升级 | 用户确认的 HTTPS 双备份 OTA，可配置 VM 保留或清空策略 |

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

旧 EMI 板型保留独立配置，不适用上述双备份布局。

## 快速开始

1. 安装杰理 Windows 开发工具链。
2. 在 [board_config.h](apps/D_Code/config/board_config.h) 中选择板型。
3. 在 [app_config.h](apps/D_Code/config/app_config.h) 中选择界面和功能。
4. 按 [D_Code 使用说明](apps/D_Code/README.md) 完成配置、编译和烧录。

`ac791n_D_Code` 编译并烧录时擦除 VM；`ac791n_D_Code_keep_vm` 保留 VM。

OTA 打包入口为 [D_code_OTA.bat](apps/D_Code/D_code_OTA.bat)，
步骤见 [OTA 升级包](apps/D_Code/README.md#ota-升级包)。

## 工程结构

```text
apps/D_Code/
├── app/             系统启动与服务接入
├── board/           板级配置和编译入口
├── config/          功能与部署配置
├── framework/       事件、页面和 UI 公共组件
├── platform/        显示、电源、存储等硬件适配
├── modules/
│   ├── applications/  页面与应用
│   ├── services/      后台服务
│   └── integrations/  云端与第三方组件
├── build/           构建与打包工具
└── tools/           开发与测试工具
```

## 文档

- [配置、编译、烧录与 OTA](apps/D_Code/README.md)
- [Product UI 资源说明](apps/D_Code/modules/applications/product_ui/assets/README.md)
- [历法数据来源与许可](apps/D_Code/modules/applications/product_ui/features/time/calendar_data/NOTICE.md)
- [开发板与 SDK 文档](https://doc.yunthinker.com/#/docs/AC791N/%E5%BC%80%E5%8F%91%E6%9D%BF%E4%BB%8B%E7%BB%8D/1)

第三方代码、字体和二进制库按各自的许可证及授权条件使用。

## 公开同步范围

公开工程保留产品源码、SDK、编译与烧录入口、已生成的固件资源及必要许可。
`project_doc/` 属于开发工作区的设计、测试和验收资料，不纳入公开同步。
从开发工程同步时应按选定文件清单更新，排除该目录；不要整仓覆盖或强制添加被忽略的文件。
提交前执行 `python -B apps/D_Code/tools/check_public_distribution.py` 核对公开边界。
正常编译使用仓内已生成的资源，不需要开发工作区的测试夹具或设计导出工具。

### 社区

技术交流QQ群：593243414

### 注意事项

请通过AC791N [系列芯片样品购买](https://item.taobao.com/item.htm?ft=t&id=1008161308862) 渠道购买样品，其他渠道芯片可能会出现key不一致报错，无法下载程序等问题，且无法提供相关
技术支持。
