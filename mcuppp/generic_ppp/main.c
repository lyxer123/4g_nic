#include <stdio.h>
#include <string.h>
#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"
#include "lwip/sockets.h"
#include "lwip/pppapi.h"
#include "lwip/pppos.h"
#include "uart_ppp.h"

static struct netif ppp_netif;
static ppp_pcb *ppp = NULL;

static void ppp_status_callback(ppp_pcb *pcb, int err_code, void *ctx);
static void network_test(void);

int main(void)
{
    lwip_init();
    uart_ppp_init();

    ppp = pppapi_pppos_create(&ppp_netif, uart_ppp_output, ppp_status_callback, NULL);
    if (ppp == NULL) {
        printf("pppos create failed\n");
        return -1;
    }

    ppp_set_auth(ppp, PPPAUTHTYPE_NONE, NULL, NULL);
    pppapi_connect(ppp, 0);

    while (1) {
        uart_ppp_poll();
        if (ppp_netif.ip_addr.u_addr.ip4.addr != 0) {
            network_test();
            break;
        }
    }

    while (1) {
        uart_ppp_poll();
    }

    return 0;
}

static void ppp_status_callback(ppp_pcb *pcb, int err_code, void *ctx)
{
    switch (err_code) {
        case PPPERR_NONE:
            printf("PPP established\n");
            printf("Local IP: %s\n", ipaddr_ntoa(&ppp_netif.ip_addr));
            break;
        case PPPERR_USER:
            printf("PPP disconnected by user\n");
            break;
        default:
            printf("PPP error %d\n", err_code);
            break;
    }
}

static void network_test(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("socket create failed\n");
        return;
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(80);
    server.sin_addr.s_addr = inet_addr("8.8.8.8");

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == 0) {
        printf("Connected to 8.8.8.8:80\n");
        const char *request = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
        send(sock, request, strlen(request), 0);

        char buf[256];
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len > 0) {
            buf[len] = '\0';
            printf("Response:\n%s\n", buf);
        }
    } else {
        printf("connect failed\n");
    }

    closesocket(sock);
}
