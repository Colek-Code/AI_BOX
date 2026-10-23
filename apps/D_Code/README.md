# D_Code

基于杰理 AC791N SDK 的产品固件，包含 Product UI、万年历、番茄钟、本地闹钟、Qwen 语音、相机预览和 HTTPS OTA。

GC9307/HYN 配置：**16 MB Flash、8 MB SDRAM、320 × 240 显示分辨率**。

## 配置

| 文件 | 用途 |
| --- | --- |
| [config/board_config.h](config/board_config.h) | 板型、屏幕和触摸组合 |
| [config/app_config.h](config/app_config.h) | 主界面和功能开关 |
| [config/deployment_config.h](config/deployment_config.h) | Wi-Fi、云端参数、应用版本 |
| [config/qwen_config.h](config/qwen_config.h) | Qwen 鉴权模式和语音参数 |
| [config/ota_config.h](config/ota_config.h) | OTA 下载地址、SDK 升级序号、数据保留策略 |

功能开关使用 `0`（关闭）或 `1`（开启），修改配置后需重新编译。
旧 EMI 板型使用 8 MB Flash，不适用 GC9307 的双备份布局。

## 编译与烧录

Windows 下使用杰理工具链，默认安装路径为 `C:\JL\pi32`。

安装 Python 3.10 或更新版本。构建入口会自动识别 Windows 的 `py` 启动器或
`python3`／`python`，也可通过 `D_PYTHON` 指定解释器；路径含空格时需加引号。
工具链 `bin` 目录需包含 `clang.exe`、`llvm-ar.exe`、`llvm-nm.exe` 和 `llvm-bcrename.exe`。

工程下载方式见 [仓库首页](../../README.md#下载)。联网和云端功能需要填写自己的凭据。
可选的百度 BRTC 功能默认关闭；如需启用，请先补齐对应 SDK 的 `libbrtc.a`。

| IDE 任务／Make 目标 | 行为 |
| --- | --- |
| `ac791n_D_Code` | 编译并烧录，擦除 VM |
| `ac791n_D_Code_keep_vm` | 编译并烧录，保留 VM |

擦除 VM 会清除已保存的 Wi-Fi、注册凭据和应用设置。保留 VM 升级还需保持分区布局兼容。

仅编译时，在仓库根目录执行：

```powershell
.\tools\utils\make.exe -C apps/D_Code/board/wl82 firmware_elf
```

## OTA 升级包

发版前配置以下参数：

| 参数 | 配置文件 | 说明 |
| --- | --- | --- |
| `D_FIRMWARE_VERSION` | `config/deployment_config.h` | 应用版本 |
| `D_OTA_BOOT_VERSION` | `config/ota_config.h` | SDK 升级序号，每次发版递增，须大于设备当前序号 |
| `D_OTA_KEEP_VM` | `config/ota_config.h` | `1` 保留 VM，`0` 清空 VM |

1. 编译 D_Code。
2. 双击 [D_code_OTA.bat](D_code_OTA.bat)。
3. 核对窗口中的版本、升级序号和 VM 策略，输入 `RUN`。
4. 在 SDK 菜单中选择 **2：HTTP OTA、FTP 升级、虚拟 U 盘升级**。
5. 生成成功后，窗口直接显示文件夹和文件的完整路径，输入 `O` 可打开文件夹。

此流程包含有线下载，并采用 `D_OTA_KEEP_VM` 指定的 VM 策略。
文件保存在本工程目录下：

```text
apps\D_Code\build\out\product_ui_ota\packages\<本次生成时间>\upgrade\update-ota.ufw
```

将 `update-ota.ufw` 上传到固件服务器。设备端的 HTTPS 下载地址由 `config/ota_config.h` 中的 `D_OTA_RELEASE_URL` 配置；联网校时后，在“设置 → 系统升级”中检查并升级。

## 目录

```text
app/            系统启动、服务启动和界面注册
board/wl82/     编译入口和板级配置
config/         产品配置
framework/      事件、页面、UI 模块等公共组件
platform/       显示、电源、PWM、存储等硬件适配
modules/
  applications/ 页面与应用
  services/     后台设备服务
  integrations/ 云端及第三方 SDK
  diagnostics/  诊断与演示功能
build/          源码清单、打包工具和编译输出
tests/          测试代码
tools/          开发工具
```
