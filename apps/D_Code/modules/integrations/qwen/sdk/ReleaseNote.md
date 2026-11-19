# v1.3.1
## 主要更新
新增 duplex 模式端侧主动发起语音、打断语音输入支持
新增 return_search_result 配置，支持控制返回所有搜索结果
新增 c_mmi_set_us_language，支持设置上行语音识别语种
新增 c_mmi_set_ds_language，支持设置下行语音合成语种
新增 disable_memory 配置，支持控制长期记忆
新增对 task-failed 事件的解析
支持 c_visual 按需释放内存
支持 voice id 为空使用默认音色
mp3 格式支持 bit_rate 参数
新增 word_timestamp_enabled 配置，支持语音识别结果按字时间戳
新增 C_MMI_EVENT_LATENCY 事件，透出云端延迟统计
新增 C_MMI_EVENT_STATE_CHANGE 事件，major state 切换时回调通知应用层
新增 c_agent_image_start_v2 接口，支持传入生图 Agent 提示词自定义变量（语音直通场景 query 由系统注入 ASR 结果）

## 问题修复
修复 duplex 模式下 req2rsp 被错误取消的问题
修复 VQA URL 模式下首轮上行 URL 乱码问题
修复开启 incremental_response 之后错误关闭 speech 的问题
修复 c_visual_update_url memcpy 越界写入
下行限速上限由 u16 扩展为 u32
修复 sdk deinit 后仍发送心跳包问题
优化 ringbuffer 锁机制

# v1.3.0
## 主要更新
新增 c_mmi_set_instruction ，支持TTS 合成指令（方言/情感/角色控制）
新增 c_mmi_set_replace_words ，支持设置纠错词表
优化license接口，调用license相关接口不再需要传入时间戳字符串
增加支持多license模式
优化后付费设置接口，可同时支持后付费与license模式

# v1.2.1
## 主要更新
支持百炼网关最新安全认证机制。
支持workspace license模式
增加incremental_response、response_text配置选项，transmit_rate_limit增加自适应默认配置。
增加海外接入节点c_mmi_get_wss_host_global。
优化存储结构，统一license与非license存储结构。
修复 token 未过期时返回错误问题。
增加agent子模块，支持图片问答、生图等功能。
优化requestToRespond业务逻辑，支持任意时刻调用打断当前交互。

## 芯片平台相关注意事项
ec718pm和asr1606芯片，在获取llm返回文本的时候，原默认取text字段；
更新SDK后需要通过mmi_user_config_t.response_text进行配置。

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
