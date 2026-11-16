/** @file d_tcp_client.c @brief 按网络事件启动的 TCP 客户端诊断，不属于生产传输服务。 */
#include "config/app_config.h"

#ifdef D_TCP_CLIENT_TEST

#include "sock_api/sock_api.h"
#include "system/includes.h"
#include "event/net_event.h"
#include "wifi/wifi_connect.h"
#include "lwip.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#define CLIENT_TCP_PORT 0      			//客户端端口号
static void *sock = NULL;
static volatile u8 tcp_client_running;
static volatile u8 tcp_client_stop_req;
#ifndef SERVER_TCP_IP
#define SERVER_TCP_IP "10.17.40.160"   //服务端ip
#endif
#ifndef SERVER_TCP_PORT
#define SERVER_TCP_PORT 8080           //服务端端口号
#endif

/**
 * @brief 创建并连接一个 SDK TCP 套接字，失败时注销。
 * @param[in] server_ip IPv4 文本地址。
 * @param[in] server_port 服务端端口号。
 * @return 连接成功返回 0；注册或连接失败返回 -1。
 */
static int tcp_client_init(const char *server_ip, const int server_port)
{
    //struct sockaddr_in local;
    struct sockaddr_in dest;

    //创建socket
    sock = sock_reg(AF_INET, SOCK_STREAM, 0, NULL, NULL);
    if (sock == NULL) {
        printf("sock_reg fail.\n");
        return -1;
    }

    sock_set_connect_to(sock, 15);

    //连接指定地址服务端
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(server_ip);
    dest.sin_port = htons(server_port);
    if (0 != sock_connect(sock, (struct sockaddr *)&dest, sizeof(struct sockaddr_in))) {
        int so_err = sock_get_error(sock);
        printf("[D_TCP] sock_connect fail %s:%d, err=%d\n", server_ip, server_port, so_err);
        sock_unreg(sock);
        sock = NULL;
        return -1;
    }

    return 0;
}

/**
 * @brief 经 SDK 套接字发送诊断数据。
 * @param[in] sock_hdl SDK 套接字句柄。
 * @param[in] buf 发送数据，调用期借用。
 * @param[in] len 字节数。
 * @return sock_send 的原始发送字节数或错误。
 */
static int tcp_send_data(const void *sock_hdl, const void *buf, const u32 len)
{
    return sock_send(sock_hdl, buf, len, 0);
}

/**
 * @brief 经 SDK 套接字接收诊断数据。
 * @param[in] sock_hdl SDK 套接字句柄。
 * @param[out] buf 接收缓冲。
 * @param[in] len 接收容量。
 * @return sock_recvfrom 原始接收字节数或错误。
 */
static int tcp_recv_data(const void *sock_hdl, void *buf, u32 len)
{
    return sock_recvfrom(sock_hdl, buf, len, 0, NULL, NULL);
}

/**
 * @brief 最多尝试五次连接，再发送提示并接收确认数据，最后注销套接字。
 * @param[in] priv 任务兼容参数，本实现不使用。
 * @note 断网通过停止标志与 sock_set_quit 促使任务退出；状态标志不构成跨任务线程安全保证。
 */
static void tcp_client_task(void *priv)
{
    int  err;
    char *payload = "Please send me some data!";
    char recv_buf[1024];
    int  recv_len;
    int  send_len;
    int  attempt;

    tcp_client_stop_req = 0;
    attempt = 1;
    while (attempt <= 5) {
        if (tcp_client_stop_req) {
            tcp_client_running = 0;
            return;
        }

        printf("[D_TCP] connect attempt %d\n", attempt);
        err = tcp_client_init(SERVER_TCP_IP, SERVER_TCP_PORT);
        if (!err) {
            break;
        }

        if (attempt == 5) {
            printf("[D_TCP] tcp_client_init failed\n");
            tcp_client_running = 0;
            return;
        }
        os_time_dly(1000);
        attempt++;
    }

    send_len = tcp_send_data(sock, payload, strlen(payload));
    if (send_len == -1) {
        int so_err = sock_get_error(sock);
        printf("[D_TCP] sock_send err=%d\n", so_err);
        sock_unreg(sock);
        sock = NULL;
        tcp_client_running = 0;
        return;
    }

    for (;;) {
        if (tcp_client_stop_req) {
            break;
        }
        recv_len = tcp_recv_data(sock, recv_buf, sizeof(recv_buf));
        if ((recv_len != -1) && (recv_len != 0)) {
            recv_buf[recv_len] = '\0';
            printf("Received %d bytes, data: %s\n\r", recv_len, recv_buf);
            tcp_send_data(sock, "Data received successfully!", strlen("Data received successfully!"));
        } else {
            int so_err = sock_get_error(sock);
            printf("[D_TCP] sock_recv err=%d\n", so_err);
            break;
        }
    }

    if (sock) {
        sock_unreg(sock);
        sock = NULL;
    }

    tcp_client_running = 0;
}

/**
 * @brief 按联网、断网及重连事件创建任务或请求退出。
 * @param[in] sys_event 非空系统事件，网络事件载荷按 net_event 解释。
 * @note 重连兼容入口由 SDK 提供；断网设置 running 为 0 不证明旧任务已经返回。
 */
static void d_tcp_client_net_event_handler(struct sys_event *sys_event)
{
    struct net_event *net_event = (struct net_event *)sys_event->payload;

    if (sys_event->type != SYS_NET_EVENT) {
        return;
    }
    if (ASCII_StrCmp(net_event->arg, "net", 4)) {
        return;
    }

    switch (net_event->event) {
    case NET_EVENT_CONNECTED:
        if (tcp_client_running) {
            break;
        }
        puts("[D_TCP] NET_EVENT_CONNECTED\n");
        tcp_client_stop_req = 0;
        tcp_client_running = 1;
        if (thread_fork("tcp_client_task", 10, 1024, 0, NULL, tcp_client_task, NULL) != OS_NO_ERR) {
            tcp_client_running = 0;
            printf("thread fork fail\n");
        }
        break;
    case NET_EVENT_DISCONNECTED:
        puts("[D_TCP] NET_EVENT_DISCONNECTED\n");
        tcp_client_stop_req = 1;
        if (sock) {
            sock_set_quit(sock);
        }
        tcp_client_running = 0;
        break;
    case NET_EVENT_DISCONNECTED_AND_REQ_CONNECT:
        puts("[D_TCP] NET_EVENT_DISCONNECTED_AND_REQ_CONNECT\n");
        tcp_client_stop_req = 1;
        if (sock) {
            sock_set_quit(sock);
        }
        extern void wifi_return_sta_mode(void);
        wifi_return_sta_mode();
        break;
    default:
        break;
    }
}

/**
 * @brief 注册网络事件诊断，在当前已 DHCP 在线时尝试启动客户端。
 * @return 当前实现固定返回 0，线程创建错误只记录日志。
 */
static int d_tcp_client_init(void)
{
    register_sys_event_handler(SYS_NET_EVENT, NET_EVENT_FROM_USER, 0, d_tcp_client_net_event_handler);

    if (wifi_is_on() && wifi_get_sta_connect_state() == WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
        if (!tcp_client_running) {
            tcp_client_running = 1;
            if (thread_fork("tcp_client_task", 10, 1024, 0, NULL, tcp_client_task, NULL) != OS_NO_ERR) {
                tcp_client_running = 0;
                printf("thread fork fail\n");
            }
        }
    }

    return 0;
}

late_initcall(d_tcp_client_init);

#endif //D_TCP_CLIENT_TEST
