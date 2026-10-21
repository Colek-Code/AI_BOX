# v1.1.0
## 主要更新
1. SDK支持后付费链路接入
2. 支持json方式配置链路参数
3. 增加沉浸模式接口，适配agent接管后续对话逻辑
## 注意事项
使用后付费链路及全托管模式时，需在设备端写入API Key，开发者需自行确保API Key存储及应用安全。
## 版本升级注意事项
### SDK目录变化
最新SDK目录如下
├── ReleaseNote.md
├── include
│   ├── c_utils
│   │   ├── c_utils.h
│   │   └── ...
│   ├── c_mmi.h
│   ├── lib_c_license.h
│   ├── lib_c_sdk.h
│   └── ...
├── libs
│   ├── libc_mmi_cmd.a
│   ├── libqwen_sdk.a
│   └── ...
└── third_party
    ├── cJSON
    │   ├── cJSON.h
    │   └── libcjson.a
    └── tinycrypt
        ├── include
        │   └── ...
        └── libtinycrypt.a
### 链接库变化
原libaliyun_sdk.a拆分为libqwen_sdk.a和libc_license.a，仅当使用license模式接入时，需要链接libc_license.a。
原libc_mm_cmd.a和libc_toolcall.a整合为统一的工具调用库libc_mmi_cmd.a
### 函数变更
c_mm_cmd_register入参变更
由 int32_t c_mm_cmd_register(char *domain_name, char *cmd, c_multi_modal_func func)
  ○ typedef int32_t(*c_multi_modal_func)(cJSON *p_cjson);
变更为 int32_t c_mm_cmd_register(char *domain_name, char *cmd, c_mmi_cmd_func func)
  ○ typedef int32_t(*c_mmi_cmd_func)(char *param);
### 配置变化
去除c_license_disable函数接口，当加载libc_license.a时自动启用license模式，否则关闭license模式
1. 使用后付费模式接入
不加载libc_license.a，需要配置app_id、ws_id、api_key和device_name
2. 使用全托管模式接入
需要加载libc_license.a，需要配置app_id、app_secret、api_key和device_name，使用阿里云提供接入节点
3. 使用半托管模式
需要加载libc_license.a，需要配置app_id、app_secret和device_name，使用客户自定义接入节点
4. 配置api如下
* 配置app_id
	int32_t c_mmi_storage_set_app_id_str(char *app_id_str);
* 配置ws_id
	int32_t c_mmi_storage_set_ws_id(char *ws_id);
* 配置api_key
	int32_t c_mmi_storage_set_api_key(char *api_key);
* 配置app_secret
	int32_t c_license_set_app_secret_str(char *app_secret);
* 配置device_name
	int32_t c_mmi_set_device_name(char *device_name);
