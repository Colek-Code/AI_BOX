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
#define SERVER_TCP_IP "192.168.1.100"  //服务端ip
#endif
#ifndef SERVER_TCP_PORT
#define SERVER_TCP_PORT 8080           //服务端端口号
#endif

//初始化
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

//发送数据
static int tcp_send_data(const void *sock_hdl, const void *buf, const u32 len)
{
    return sock_send(sock_hdl, buf, len, 0);
}

//接收数据
static int tcp_recv_data(const void *sock_hdl, void *buf, u32 len)
{
    return sock_recvfrom(sock_hdl, buf, len, 0, NULL, NULL);
}

//tcp client任务
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
