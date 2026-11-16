# 通用 WebSocket

单实例传输服务，一次运行客户端或服务端；服务端一次接待一个对端。
业务通过 `ws_service.h` 配置和使用，模块不保存凭证，不解析上层业务消息。

在 `config/app_config.h` 中将 `D_FEATURE_WEBSOCKET` 设为 `1` 编入模块。
默认值为 `0`。启用后只初始化服务，业务调用 `configure`、`start` 才会连接或监听。
旧设备云 `D_DEVICE_CLOUD_ENABLE` 继续退役，Qwen 使用自己的 WSS。

## 当前能力

| 能力 | 接口／实现 | 说明 |
| --- | --- | --- |
| WS 客户端 | `WS_CLIENT`，`ws://host[:port]/path` | 支持普通 TCP 连接 |
| WS 服务端 | `WS_SERVER`、`listen_port`、精确匹配的路径 | 对端退出后继续监听；不要求 STA DHCP，允许已就绪的本地网络接口 |
| WSS 客户端 | `wss://…`、调用方 CA | SDK mbedTLS，强制证书链、主机名与有效期验证；可信联网时间不足时等待 |
| WSS 服务端 | 证书、私钥字段已预留 | **当前产品库不可用**；等待原厂提供兼容的服务端 TLS 库，配置返回未就绪 |
| 文本／二进制 | `ws_service_send`、`WS_EVENT_DATA` | 文本验证 UTF-8；二进制允许零字节 |
| 分片收发 | `fragment_send`、`fragment_size`、接收边界标志 | 接收按块回调，不按对端声明长度申请整包；允许控制帧插入分片之间 |
| Ping／Pong | `ws_service_ping`、`ws_service_send(WS_PONG,…)` | 模块自动回答 Ping，只有工作线程生成周期心跳；匹配心跳超时断开 |
| Close | `ws_service_close` | 校验状态码和 UTF-8 原因；主动关闭等待回应或 I/O 超时；客户端不再重连，服务端继续监听 |
| 参数与缓冲 | Origin、User-Agent、I/O 超时、接收切片、消息上限 | URL 上限 1024 字节（含结尾零）；HTTP 头上限 2048 字节；主机名支持 DNS／IPv4 |
| 客户端 TLS 会话复用 | `resume_tls_session` | 只缓存验证成功的会话，检查证书链有效期；复用失败尝试完整握手；停止、维护、重配时清空 |
| 暂停与恢复 | `maintenance_begin/poll/release/cancel` | OTA、关机各有独立占用；清理完成后才解除，显式停止优先于恢复 |

能力位以 `ws_service_capabilities()` 为准。当前 WL82 适配返回客户端 WS/WSS、服务端 WS，
不包含 `WS_CAP_SERVER_WSS`。不能通过只改头文件宏启用 WSS 服务端。

## 使用

业务初始化阶段先确认服务已初始化，再配置。示例中的 `on_websocket` 由业务实现：

```c
ws_service_config_t config;
ws_service_defaults(&config);
config.url = "ws://192.168.1.10:8000/";
config.callback = on_websocket;
if (ws_service_configure(&config) == D_OK) {
    ws_service_start();
}
```

Origin 和 User-Agent 只用于客户端握手，不作为服务端访问控制。
WSS 客户端另传入 `ca` 和 `ca_length`。PEM 长度包含结尾零，DER 长度使用原始字节数。
证书、私钥和回调上下文采用只读借用，调用方须保留到停止确认完成。字符串配置会复制。
服务端配置 `role=WS_SERVER`、`url="/"` 和 `listen_port`；当前仅支持普通 WS 监听。

回调运行在传输工作线程中，数据只在本次回调期间有效。处理必须有界；需要异步处理时，
复制到业务自己的有界队列。回调内不得调用同步发送，发送会返回忙。
`WS_RX_MESSAGE_START/END` 表示消息边界，`WS_RX_FRAME_START/END` 表示帧边界；
一个帧可能多次回调，也可能有零长度边界通知。流式前缀不代表整个消息已验证完成。

在业务任务中发送。返回成功只表示本地发送完成，不表示对端业务确认。
大消息可用自动发送分片；接收消息上限默认 1 MB，单次接收切片默认 1024 字节，
最大 2048 字节。超长、非法掩码、非法 UTF-8 或不支持的帧会使连接失败，不能按截断成功处理。
不支持压缩、扩展协商、子协议协商、IPv6 地址字面量和多个同时在线的服务端对端。

`stop` 是停止请求。查询快照，直到 `worker_active=0` 且 `resources_owned=0`，才能重配、
切换角色或释放借用材料。连接失败按 5、10、20、40、60 秒上限退避；显式停止不会重连。
底层 SDK 调用若迟迟不返回，保持工作线程和资源占用，不以业务超时伪造停止成功。

## 实现边界

| 文件 | 职责 |
| --- | --- |
| `ws_service.c` | 连接意图、代次、发送串行化、心跳、回调和维护占用 |
| `ws_http.c` | 增量 HTTP Upgrade、字段和握手校验，保留尾随帧数据 |
| `ws_protocol.c` | 有界帧解码、掩码、分片、控制帧及 UTF-8 |
| `ws_port_wl82.c` | SDK socket、严格 TLS、私有会话缓存、任务和资源释放 |

服务端不使用 SDK 的危险接收函数、演示证书绑定函数和全局会话缓存。
监听 socket 与对端 socket 分开释放。发送与接收共用 I/O 锁，关闭等待正在发送的调用退出。
OTA 和关机协调层持有维护占用；服务本身不反向依赖 Qwen、页面或闹钟。

当前产品链接的 `libmbedtls_3_4_0.a` 仅含客户端握手，服务端分支返回错误。
头文件或 `websocket.a` 中存在服务端函数不代表实际 TLS 依赖可用。
启用服务端模式前需取得匹配同版本的服务端 TLS 库，不能仅凭头文件中的声明判定可用。

线程按需创建，沿用 4096 words，即 **16 KB** 栈配置；默认关闭时不创建线程。
协议缓冲固定有界，TLS 对象内部申请由 mbedTLS 管理。
