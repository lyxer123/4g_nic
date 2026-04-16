/*
 * STM32 Keil PPP over UART 示例
 *
 * 依赖：HAL UART、lwIP PPP API、lwIP sockets
 */

#include "main.h"
#include "lwip/opt.h"
#include "lwip/pppapi.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "uart_ppp.h"

ppp_pcb *ppp = NULL;
static struct netif ppp_netif;

static void PppStatusCallback(ppp_pcb *pcb, int err_code, void *ctx);
static u32_t PppOutputCallback(ppp_pcb *pcb, u8_t *data, u32_t len, void *ctx);
static void NetworkTestTask(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_LWIP_Init();

    uart_ppp_init();

    ppp = pppapi_pppos_create(&ppp_netif, PppOutputCallback, PppStatusCallback, NULL);
    if (ppp == NULL) {
        Error_Handler();
    }

    ppp_set_auth(ppp, PPPAUTHTYPE_NONE, NULL, NULL);
    pppapi_connect(ppp, 0);

    while (1) {
        uart_ppp_poll();
        if (ppp_netif.ip_addr.u_addr.ip4.addr != 0) {
            NetworkTestTask();
            HAL_Delay(5000);
        }
    }
}

static u32_t PppOutputCallback(ppp_pcb *pcb, u8_t *data, u32_t len, void *ctx)
{
    return uart_ppp_send(data, len) == HAL_OK ? len : 0;
}

static void PppStatusCallback(ppp_pcb *pcb, int err_code, void *ctx)
{
    switch (err_code) {
        case PPPERR_NONE:
            printf("PPP connected\r\n");
            printf("Local IP: %s\r\n", ipaddr_ntoa(&ppp_netif.ip_addr));
            break;
        case PPPERR_USER:
            printf("PPP disconnected by user\r\n");
            break;
        default:
            printf("PPP error %d\r\n", err_code);
            break;
    }
}

static void NetworkTestTask(void)
{
    int sock;
    struct sockaddr_in server;
    char buffer[128];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Socket create failed\r\n");
        return;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(80);
    server.sin_addr.s_addr = inet_addr("8.8.8.8");

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == 0) {
        printf("TCP connect to 8.8.8.8:80 success\r\n");
    } else {
        printf("TCP connect failed\r\n");
        closesocket(sock);
        return;
    }

    const char *request = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
    send(sock, request, strlen(request), 0);

    int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (len > 0) {
        buffer[len] = '\0';
        printf("HTTP response:\r\n%s\r\n", buffer);
    }
    closesocket(sock);
}
