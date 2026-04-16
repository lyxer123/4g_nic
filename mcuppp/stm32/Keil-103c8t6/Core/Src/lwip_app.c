/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : lwip_app.c
  * @brief          : Simple lwIP network tests for PPPoS.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "lwip_app.h"
#include "pppos_lwip.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/icmp.h"
#include "lwip/raw.h"
#include "lwip/inet.h"
#include "lwip/ip.h"
#include "lwip/ip_addr.h"
#include <string.h>
#include <stdio.h>

static void ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(pcb);
  LWIP_UNUSED_ARG(addr);

  if (p != NULL)
  {
    pbuf_free(p);
  }
}

void LWIP_PingTest(const char *ipv4_addr)
{
  struct raw_pcb *ping_pcb;
  ip_addr_t dest;
  struct pbuf *p;
  struct icmp_echo_hdr *iecho;
  size_t ping_size = sizeof(struct icmp_echo_hdr);

  if (!ipaddr_aton(ipv4_addr, &dest))
  {
    return;
  }

  ping_pcb = raw_new(IP_PROTO_ICMP);
  if (ping_pcb == NULL)
  {
    return;
  }

  raw_recv(ping_pcb, ping_recv, NULL);
  raw_bind(ping_pcb, IP_ADDR_ANY);

  p = pbuf_alloc(PBUF_IP, (u16_t)ping_size, PBUF_POOL);
  if (p == NULL)
  {
    raw_remove(ping_pcb);
    return;
  }

  iecho = (struct icmp_echo_hdr *)p->payload;
  ICMPH_TYPE_SET(iecho, ICMP_ECHO);
  ICMPH_CODE_SET(iecho, 0);
  iecho->chksum = 0;
  iecho->id = lwip_htons(0xAFAF);
  iecho->seqno = lwip_htons(1);
  iecho->chksum = inet_chksum(iecho, ping_size);

  raw_sendto(ping_pcb, p, &dest);
  pbuf_free(p);
}

void LWIP_HttpGetTest(const char *host, const char *path)
{
  struct sockaddr_in server_addr;
  int sock;
  char request[256];
  char recv_buf[512];
  int ret;
  struct hostent *h;

  h = gethostbyname(host);
  if (h == NULL || h->h_addr_list == NULL || h->h_addr_list[0] == NULL)
  {
    return;
  }

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
  {
    return;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(80);
  memcpy(&server_addr.sin_addr, h->h_addr_list[0], h->h_length);

  ret = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (ret < 0)
  {
    closesocket(sock);
    return;
  }

  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
           path, host);
  send(sock, request, strlen(request), 0);

  ret = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
  if (ret > 0)
  {
    recv_buf[ret] = '\0';
  }

  closesocket(sock);
}
